#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// FileSystem
//
// Platform-agnostic async file I/O with transparent caching.
//
// Platform implementations
// ────────────────────────
//   Native (Linux / macOS / Windows)
//     • ReadAsync  : synchronous disk read; callback invoked before returning.
//     • Prefetch   : parallel disk reads via JobSystem; warms in-memory cache.
//                    fopen() already works natively — no extra magic needed.
//
//   Web (Emscripten / WebAssembly)
//     • ReadAsync  : emscripten_fetch → IndexedDB cache → async callback.
//     • Prefetch   : batch emscripten_fetch with EMSCRIPTEN_FETCH_PERSIST_FILE;
//                    bytes stored in the in-memory cache.
//                    Text/script files (.lua .json .glsl …) are ALSO written
//                    into Emscripten's MEMFS so that fopen() / Lua's require()
//                    / std::filesystem::exists() find them after prefetching.
//
//   Future platforms (Android AAsset, iOS NSBundle, custom VFS, …)
//     Implement the two private _PlatformReadAsync / _PlatformPrefetch hooks
//     in a new platform-specific file_system_<platform>.cpp translation unit.
//
// Typical startup pattern (identical on every platform):
//
//   // main() — before creating Application
//   FileSystem::Get().Prefetch(
//     { "assets/textures/hero.ktx2",   // binary: stays in cache only
//       "assets/scripts/main.lua",      // text: cache + MEMFS (web)
//       "assets/config/settings.json" },// text: cache + MEMFS (web)
//     []() {
//         // All files are now in cache. ReadSync() is instant.
//         // On web, fopen() / Lua require() also work for text files.
//         static MyApp app({...});
//         app.Run();
//     });
//   // main() returns; browser event loop (or OS) drives the rest.
//
// Memory notes (web)
// ──────────────────
//   Binary assets (KTX2 …):  cache entry lives until ConsumeSync() is called
//                             (which erases it — zero-copy hand-off pattern).
//   Text assets (Lua …):      cache entry + MEMFS copy both held in WASM heap.
//                             Text files are small; this is acceptable.
//   IndexedDB:                emscripten_fetch persists the raw bytes.
//                             Subsequent page-loads skip the network entirely.
// ─────────────────────────────────────────────────────────────────────────────
class FileSystem {
public:
    using Bytes              = std::vector<uint8_t>;
    using ReadCallback       = std::function<void(Bytes data, bool success)>;
    using CompletionCallback = std::function<void()>;

    static FileSystem& Get();

    // ── Async read ────────────────────────────────────────────────────────────
    // Cache-hit → callback fires immediately (same call stack).
    // Cache-miss on web    → emscripten_fetch; callback fires asynchronously.
    // Cache-miss on native → synchronous disk read; callback fires inline.
    // The callback is always invoked, even on failure (check `success`).
    void ReadAsync(const std::string& path, ReadCallback cb);

    // ── Sync read ─────────────────────────────────────────────────────────────
    // Returns cached bytes if available; otherwise reads from disk (native) or
    // returns {} with an error log (web — must Prefetch first).
    Bytes ReadSync(const std::string& path);

    // Like ReadSync but moves the cached entry out and erases it.
    // Use for one-shot binary consumption (e.g., KTX2 → GPU upload):
    //   auto bytes = FileSystem::Get().ConsumeSync("hero.ktx2");
    //   // upload bytes to GPU
    //   // bytes is freed when it goes out of scope; cache entry already gone
    Bytes ConsumeSync(const std::string& path);

    // ── Batch prefetch ────────────────────────────────────────────────────────
    // Fetches / reads every path and prepares it for fast access.
    // onDone is called once all operations have settled (success or error).
    //
    // After onDone fires:
    //   • ReadSync / ConsumeSync return instantly from cache.
    //   • fopen(path) and Lua require() work for text-format files.
    //
    // Behaviour per platform:
    //   Web    : non-blocking; onDone fires asynchronously in the event loop.
    //   Native : blocking parallel reads via JobSystem; onDone fires before
    //            Prefetch() returns (synchronous completion on native).
    //
    // Already-cached paths are skipped without launching a new fetch / read.
    void Prefetch(const std::vector<std::string>& paths, CompletionCallback onDone);

    // ── Utilities ─────────────────────────────────────────────────────────────
    // Checks in-memory cache then std::filesystem (MEMFS on web, disk native).
    bool Exists(const std::string& path) const;

    bool IsCached(const std::string& path) const;

    // Release a cached entry to reclaim memory (after ConsumeSync is preferred).
    void EvictCache(const std::string& path);

    // Drop the entire cache. Call after a loading phase completes and you no
    // longer need the cached bytes (e.g., between level loads).
    void ClearCache();

private:
    FileSystem()                             = default;
    ~FileSystem()                            = default;
    FileSystem(const FileSystem&)            = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    static FileSystem* _instance;
};
