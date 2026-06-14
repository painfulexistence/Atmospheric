// file_system.cpp — Platform-agnostic async file I/O with transparent caching.
//
// Web (Emscripten)
//   ReadAsync  : emscripten_fetch → IndexedDB → WASM heap → callback
//   Prefetch   : batch fetch; text files also written to MEMFS so
//                fopen() / Lua require() / std::filesystem::exists() work.
//   Note: PERSIST_FILE causes onsuccess to fire twice (HTTP + IndexedDB store).
//         FetchCtx::done guards against double-processing.
//
// Native (Linux / macOS / Windows)
//   ReadAsync  : synchronous fread; callback fires before ReadAsync returns.
//   Prefetch   : parallel fread via JobSystem; onDone fires before Prefetch
//                returns (synchronous completion on native).
//
// The in-process cache (g_cache) is a file-scope global so it is reachable
// from C-style callbacks (Emscripten) and JobSystem lambdas without needing
// a captured 'this' pointer.

#include "file_system.hpp"
#include "console.hpp"

#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// In-process cache
// ─────────────────────────────────────────────────────────────────────────────
static std::unordered_map<std::string, FileSystem::Bytes> g_cache;
// Protects g_cache on native builds where JobSystem worker threads write it.
// On Emscripten (single-threaded), the mutex is a no-op but keeps the code
// correct if pthreads are ever enabled.
static std::mutex g_cacheMutex;

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
FileSystem* FileSystem::_instance = nullptr;

FileSystem& FileSystem::Get() {
    if (!_instance) _instance = new FileSystem();
    return *_instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared helper: synchronous read from the OS filesystem (or Emscripten MEMFS).
// Returns an empty vector on error.
// ─────────────────────────────────────────────────────────────────────────────
static FileSystem::Bytes ReadFromDisk(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return {}; }
    long len = ftell(f);
    rewind(f);
    if (len <= 0) { fclose(f); return {}; }
    FileSystem::Bytes buf(static_cast<size_t>(len));
    if (fread(buf.data(), 1, static_cast<size_t>(len), f) != static_cast<size_t>(len)) {
        fclose(f); return {};
    }
    fclose(f);
    return buf;
}

static std::string NormalizePath(const std::string& path) {
    if (path.size() >= 2 && path[0] == '.' && path[1] == '/') {
        return path.substr(2);
    }
    return path;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cache helpers — identical on all platforms
// ─────────────────────────────────────────────────────────────────────────────
bool FileSystem::IsCached(const std::string& path) const {
    std::string normPath = NormalizePath(path);
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    return g_cache.count(normPath) != 0;
}

void FileSystem::EvictCache(const std::string& path) {
    std::string normPath = NormalizePath(path);
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    g_cache.erase(normPath);
}

void FileSystem::ClearCache() {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    g_cache.clear();
}

bool FileSystem::Exists(const std::string& path) const {
    std::string normPath = NormalizePath(path);
    if (IsCached(normPath)) return true;
    return std::filesystem::exists(normPath);
}

FileSystem::Bytes FileSystem::ReadSync(const std::string& path) {
    std::string normPath = NormalizePath(path);
    {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        auto it = g_cache.find(normPath);
        if (it != g_cache.end()) return it->second; // copy from cache
    }
#ifdef __EMSCRIPTEN__
    // On web a cache-miss means Prefetch was not called — log and fail.
    ENGINE_LOG("[FileSystem] ReadSync cache miss on web: '{}' — call Prefetch first", normPath);
    return {};
#else
    auto bytes = ReadFromDisk(normPath);
    if (bytes.empty())
        ENGINE_LOG("[FileSystem] ReadSync: failed to read '{}'", normPath);
    return bytes;
#endif
}

FileSystem::Bytes FileSystem::ConsumeSync(const std::string& path) {
    std::string normPath = NormalizePath(path);
    {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        auto it = g_cache.find(normPath);
        if (it != g_cache.end()) {
            FileSystem::Bytes result = std::move(it->second);
            g_cache.erase(it);
            return result;
        }
    }
#ifndef __EMSCRIPTEN__
    // Native: fall back to disk read on cache miss.
    return ReadFromDisk(normPath);
#else
    ENGINE_LOG("[FileSystem] ConsumeSync cache miss on web: '{}' — call Prefetch first", normPath);
    return {};
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
// Platform-specific implementations
// ═════════════════════════════════════════════════════════════════════════════

#ifdef __EMSCRIPTEN__
// ─────────────────────────────────────────────────────────────────────────────
// Web / Emscripten implementation
// ─────────────────────────────────────────────────────────────────────────────
#include <emscripten/fetch.h>
#include <emscripten.h>
#include <atomic>
#include <memory>

// ── MEMFS write helper ────────────────────────────────────────────────────────
// Runs inside the WASM module's JS closure; HEAPU8, FS, UTF8ToString are
// available without a Module. prefix.
EM_JS(void, fs_js_write_memfs, (const char* path_ptr, const uint8_t* data_ptr, int data_len), {
    var path  = UTF8ToString(path_ptr);
    // Recursively create parent directories (ignores "already exists")
    var parts = path.split('/');
    var dir   = '';
    for (var i = 0; i < parts.length - 1; ++i) {
        dir += (i === 0 && parts[i] === '' ? '' : '/') + parts[i];
        if (dir !== '') { try { FS.mkdir(dir); } catch(e) {} }
    }
    // Write (creates or overwrites); HEAPU8.subarray is a JS view, no extra copy
    FS.writeFile(path, HEAPU8.subarray(data_ptr, data_ptr + data_len));
});

// ── Text-file detection ───────────────────────────────────────────────────────
// Files with these extensions are written to MEMFS after fetching so that
// fopen() / Lua require() / std::filesystem::exists() find them.
static bool NeedsMemFS(const std::string& path) {
    static const char* kTextExts[] = {
        ".lua", ".json", ".glsl", ".vert", ".frag", ".tesc", ".tese",
        ".txt",  ".csv", ".xml",  ".yaml", ".toml",
        ".ogg",  ".mp3", ".wav",
    };
    for (const char* ext : kTextExts) {
        size_t elen = strlen(ext);
        if (path.size() >= elen &&
            path.compare(path.size() - elen, elen, ext) == 0)
            return true;
    }
    return false;
}

// ── Async batch state ─────────────────────────────────────────────────────────
namespace {

struct PrefetchQueue;

struct BatchState {
    std::atomic<int>               remaining{0};
    FileSystem::CompletionCallback onComplete;
};

struct PrefetchQueue {
    std::vector<std::string>         pendingPaths;
    size_t                           nextIndex{0};
    std::mutex                       mutex;
};

struct FetchCtx {
    std::string                      path;
    bool                             writeToMemFS;
    FileSystem::ReadCallback         singleCB; // set for ReadAsync, null for Prefetch
    std::shared_ptr<BatchState>      batch;    // set for Prefetch, null for ReadAsync
    std::shared_ptr<PrefetchQueue>   queue;    // set for Prefetch, null for ReadAsync
    // PERSIST_FILE causes onsuccess to fire twice (HTTP fetch + IndexedDB store).
    // Guard ensures we only process the first invocation.
    bool                             done{false};
};

void LaunchFetch(const std::string& path, FetchCtx* ctx);

void PumpQueue(const std::shared_ptr<PrefetchQueue>& queue, const std::shared_ptr<BatchState>& batch) {
    if (!queue) return;
    std::string nextPath;
    {
        std::lock_guard<std::mutex> lk(queue->mutex);
        if (queue->nextIndex < queue->pendingPaths.size()) {
            nextPath = queue->pendingPaths[queue->nextIndex++];
        }
    }
    if (!nextPath.empty()) {
        auto* ctx = new FetchCtx{nextPath, NeedsMemFS(nextPath), nullptr, batch, queue};
        LaunchFetch(nextPath, ctx);
    }
}

void EM_OnSuccess(emscripten_fetch_t* f) {
    auto* ctx = static_cast<FetchCtx*>(f->userData);

    // PERSIST_FILE fires onsuccess twice: once on HTTP fetch, once on IndexedDB
    // store completion. Only process the first invocation.
    if (ctx->done) {
        emscripten_fetch_close(f);
        return;
    }
    ctx->done = true;

    auto queue = ctx->queue;
    auto batch = ctx->batch;

    // Build byte vector from the fetch buffer
    FileSystem::Bytes bytes;
    if (f->numBytes > 0) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(f->data);
        bytes.assign(data, data + static_cast<size_t>(f->numBytes));
    }

    if (!bytes.empty()) {
        // Optionally mirror to MEMFS for text-format assets
        if (ctx->writeToMemFS) {
            fs_js_write_memfs(ctx->path.c_str(), bytes.data(), (int)bytes.size());
            ENGINE_LOG("[FileSystem] MEMFS + cache: '{}' ({} bytes)", ctx->path, f->numBytes);
        } else {
            ENGINE_LOG("[FileSystem] Cached: '{}' ({} bytes)", ctx->path, f->numBytes);
        }
        // Store in in-process cache
        {
            std::lock_guard<std::mutex> lk(g_cacheMutex);
            g_cache[ctx->path] = bytes;
        }
    }

    // Fire single-read callback (ReadAsync path) — outside the lock
    if (ctx->singleCB) ctx->singleCB(std::move(bytes), true);

    // Batch completion (Prefetch path)
    if (batch && --batch->remaining == 0) {
        if (batch->onComplete) batch->onComplete();
    }

    emscripten_fetch_close(f);
    delete ctx;

    // Pump next download in the queue
    PumpQueue(queue, batch);
}

void EM_OnError(emscripten_fetch_t* f) {
    auto* ctx = static_cast<FetchCtx*>(f->userData);
    ENGINE_LOG("[FileSystem] HTTP {} — failed to fetch '{}'", f->status, f->url);
    
    auto queue = ctx->queue;
    auto batch = ctx->batch;

    if (ctx->singleCB) ctx->singleCB({}, false);
    
    // Batch errors are non-fatal; decrement so Prefetch can still complete.
    if (batch && --batch->remaining == 0) {
        if (batch->onComplete) batch->onComplete();
    }
    
    emscripten_fetch_close(f);
    delete ctx;

    // Pump next download in the queue
    PumpQueue(queue, batch);
}

void LaunchFetch(const std::string& path, FetchCtx* ctx) {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    // LOAD_TO_MEMORY : make fetch->data available in the callback
    // PERSIST_FILE   : cache bytes in IndexedDB; subsequent loads skip network
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_PERSIST_FILE;
    attr.onsuccess  = EM_OnSuccess;
    attr.onerror    = EM_OnError;
    attr.userData   = ctx;
    emscripten_fetch(&attr, path.c_str());
}

} // anonymous namespace

// ── Public API (web) ──────────────────────────────────────────────────────────

void FileSystem::ReadAsync(const std::string& path, ReadCallback cb) {
    std::string normPath = NormalizePath(path);
    // Cache hit → immediate
    {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        auto it = g_cache.find(normPath);
        if (it != g_cache.end()) { cb(it->second, true); return; }
    }
    // Cache miss → fetch asynchronously; callback fires in the browser event loop
    auto* ctx = new FetchCtx{normPath, NeedsMemFS(normPath), std::move(cb), nullptr, nullptr};
    LaunchFetch(normPath, ctx);
}

void FileSystem::Prefetch(const std::vector<std::string>& paths,
                          CompletionCallback onDone) {
    if (paths.empty()) { if (onDone) onDone(); return; }

    // Skip paths that are already in cache
    std::vector<std::string> pending;
    {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        for (const auto& p : paths) {
            std::string normPath = NormalizePath(p);
            if (!g_cache.count(normPath)) pending.push_back(normPath);
        }
    }
    if (pending.empty()) { if (onDone) onDone(); return; }

    auto batch        = std::make_shared<BatchState>();
    batch->remaining  = static_cast<int>(pending.size());
    batch->onComplete = std::move(onDone);

    // Instantiate our thread-safe prefetch queue
    auto queue         = std::make_shared<PrefetchQueue>();
    queue->pendingPaths = std::move(pending);

    // Capping concurrency at 4 for high-performance sliding window
    constexpr size_t MAX_CONCURRENT_FETCHES = 4;
    size_t initialFetches = std::min(queue->pendingPaths.size(), MAX_CONCURRENT_FETCHES);
    
    {
        std::lock_guard<std::mutex> lk(queue->mutex);
        queue->nextIndex = initialFetches;
    }

    for (size_t i = 0; i < initialFetches; ++i) {
        const auto& p = queue->pendingPaths[i];
        auto* ctx = new FetchCtx{p, NeedsMemFS(p), nullptr, batch, queue};
        LaunchFetch(p, ctx);
    }
    // Returns immediately; onDone fires asynchronously via the browser event loop
}

#else
// ─────────────────────────────────────────────────────────────────────────────
// Native implementation (Linux / macOS / Windows)
// ─────────────────────────────────────────────────────────────────────────────
#include "job_system.hpp"

void FileSystem::ReadAsync(const std::string& path, ReadCallback cb) {
    std::string normPath = NormalizePath(path);
    // Cache hit → immediate
    {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        auto it = g_cache.find(normPath);
        if (it != g_cache.end()) { cb(it->second, true); return; }
    }
    // Native: synchronous disk read; callback fires before ReadAsync returns
    auto bytes = ReadFromDisk(normPath);
    bool ok    = !bytes.empty();
    if (ok) {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        g_cache[normPath] = bytes;
    } else {
        ENGINE_LOG("[FileSystem] ReadAsync: failed to read '{}'", normPath);
    }
    cb(std::move(bytes), ok);
}

void FileSystem::Prefetch(const std::vector<std::string>& paths,
                          CompletionCallback onDone) {
    if (paths.empty()) { if (onDone) onDone(); return; }

    // Parallel disk reads via JobSystem worker threads
    for (const auto& p : paths) {
        std::string normPath = NormalizePath(p);
        if (IsCached(normPath)) continue; // already cached, skip
        auto pathCopy = normPath;
        JobSystem::Get()->Execute([pathCopy](int /*threadID*/) {
            auto bytes = ReadFromDisk(pathCopy);
            if (!bytes.empty()) {
                std::lock_guard<std::mutex> lk(g_cacheMutex);
                g_cache[pathCopy] = std::move(bytes);
            } else {
                ENGINE_LOG("[FileSystem] Prefetch: failed to read '{}'", pathCopy);
            }
        });
    }
    JobSystem::Get()->Wait(); // blocks until all reads complete

    // onDone fires synchronously on native (before Prefetch returns)
    if (onDone) onDone();
}

#endif // __EMSCRIPTEN__
