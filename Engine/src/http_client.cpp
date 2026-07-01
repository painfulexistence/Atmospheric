#include "http_client.hpp"
#include <spdlog/spdlog.h>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Native implementation (libcurl multi, HTTP/2 + OpenSSL)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef __EMSCRIPTEN__

#include <curl/curl.h>

struct HttpClient::Impl {
    CURLM* multi = nullptr;

    struct Request {
        HttpRequestID id;
        HttpCallback  callback;
        HttpResponse  response;
        std::string   body;         // owned copy for CURLOPT_POSTFIELDS
        curl_slist*   headers = nullptr;
        curl_mime*    mime    = nullptr;
    };

    std::unordered_map<CURL*, Request> pending; // CURL* → in-flight request
    HttpRequestID nextId = 1;

    Impl() {
        curl_global_init(CURL_GLOBAL_ALL);
        multi = curl_multi_init();
    }

    ~Impl() {
        for (auto& [handle, req] : pending) {
            curl_multi_remove_handle(multi, handle);
            if (req.headers) curl_slist_free_all(req.headers);
            if (req.mime)    curl_mime_free(req.mime);
            curl_easy_cleanup(handle);
        }
        curl_multi_cleanup(multi);
        curl_global_cleanup();
    }

    CURL* makeHandle(const std::string& url, Request& req) {
        CURL* h = curl_easy_init();
        curl_easy_setopt(h, CURLOPT_URL,          url.c_str());
        curl_easy_setopt(h, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
        curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(h, CURLOPT_TIMEOUT,      30L);
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(h, CURLOPT_WRITEDATA,    &req.response.body);
        return h;
    }

    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* ud) {
        auto* body = static_cast<std::vector<uint8_t>*>(ud);
        body->insert(body->end(), ptr, ptr + size * nmemb);
        return size * nmemb;
    }
};

HttpClient::HttpClient() : _impl(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

HttpRequestID HttpClient::Get(const std::string& url, HttpCallback cb) {
    Impl::Request req;
    req.id       = _impl->nextId++;
    req.callback = std::move(cb);
    CURL* h = _impl->makeHandle(url, req);
    HttpRequestID id = req.id;
    _impl->pending.emplace(h, std::move(req));
    curl_multi_add_handle(_impl->multi, h);
    return id;
}

HttpRequestID HttpClient::PostJson(const std::string& url,
                                    const std::string& json,
                                    HttpCallback cb) {
    Impl::Request req;
    req.id       = _impl->nextId++;
    req.callback = std::move(cb);
    req.body     = json;
    CURL* h = _impl->makeHandle(url, req);
    req.headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(h, CURLOPT_HTTPHEADER,   req.headers);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS,   req.body.c_str());
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
    HttpRequestID id = req.id;
    _impl->pending.emplace(h, std::move(req));
    curl_multi_add_handle(_impl->multi, h);
    return id;
}

HttpRequestID HttpClient::PostMultipart(const std::string& url,
                                         const std::vector<MultipartField>& fields,
                                         HttpCallback cb) {
    Impl::Request req;
    req.id       = _impl->nextId++;
    req.callback = std::move(cb);
    CURL* h = _impl->makeHandle(url, req);
    req.mime = curl_mime_init(h);
    for (const auto& field : fields) {
        curl_mimepart* part = curl_mime_addpart(req.mime);
        curl_mime_name(part, field.name.c_str());
        if (!field.filename.empty()) {
            curl_mime_filename(part, field.filename.c_str());
            curl_mime_type(part, field.contentType.c_str());
        }
        curl_mime_data(part,
                        reinterpret_cast<const char*>(field.data.data()),
                        field.data.size());
    }
    curl_easy_setopt(h, CURLOPT_MIMEPOST, req.mime);
    HttpRequestID id = req.id;
    _impl->pending.emplace(h, std::move(req));
    curl_multi_add_handle(_impl->multi, h);
    return id;
}

void HttpClient::Cancel(HttpRequestID id) {
    for (auto it = _impl->pending.begin(); it != _impl->pending.end(); ++it) {
        if (it->second.id != id) continue;
        CURL* h = it->first;
        curl_multi_remove_handle(_impl->multi, h);
        if (it->second.headers) curl_slist_free_all(it->second.headers);
        if (it->second.mime)    curl_mime_free(it->second.mime);
        curl_easy_cleanup(h);
        _impl->pending.erase(it);
        return;
    }
}

int HttpClient::ActiveRequestCount() const {
    return static_cast<int>(_impl->pending.size());
}

void HttpClient::Pump() {
    int running = 0;
    curl_multi_perform(_impl->multi, &running);

    CURLMsg* msg;
    int msgsLeft;
    while ((msg = curl_multi_info_read(_impl->multi, &msgsLeft))) {
        if (msg->msg != CURLMSG_DONE) continue;
        CURL* h = msg->easy_handle;
        auto it = _impl->pending.find(h);
        if (it == _impl->pending.end()) continue;

        auto& req = it->second;
        long status = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);

        HttpResponse resp;
        resp.statusCode = static_cast<int>(status);
        resp.body       = std::move(req.response.body);
        resp.success    = (msg->data.result == CURLE_OK);
        if (!resp.success)
            resp.error = curl_easy_strerror(msg->data.result);

        HttpCallback cb = std::move(req.callback);

        curl_multi_remove_handle(_impl->multi, h);
        if (req.headers) curl_slist_free_all(req.headers);
        if (req.mime)    curl_mime_free(req.mime);
        curl_easy_cleanup(h);
        _impl->pending.erase(it);

        cb(std::move(resp));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Emscripten implementation (emscripten_fetch — already enabled via -sFETCH=1)
// ─────────────────────────────────────────────────────────────────────────────
#else

#include <cstring>
#include <emscripten/fetch.h>

static std::string buildMultipartBody(const std::vector<MultipartField>& fields,
                                       const std::string& boundary) {
    // Returns the raw multipart body as a string (bytes are preserved).
    // The caller is responsible for setting Content-Type with the boundary.
    std::string body;
    for (const auto& field : fields) {
        body += "--" + boundary + "\r\n";
        if (field.filename.empty()) {
            body += "Content-Disposition: form-data; name=\"" + field.name + "\"\r\n\r\n";
        } else {
            body += "Content-Disposition: form-data; name=\"" + field.name
                 + "\"; filename=\"" + field.filename + "\"\r\n";
            body += "Content-Type: " + field.contentType + "\r\n\r\n";
        }
        body.append(reinterpret_cast<const char*>(field.data.data()), field.data.size());
        body += "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return body;
}

struct HttpClient::Impl {
    HttpRequestID nextId = 1;

    // Per-request state heap-allocated and passed as emscripten_fetch userData.
    // Deleted inside the static callbacks after firing.
    struct FetchState {
        HttpRequestID id;
        HttpCallback  callback;
        // Owns memory that must outlive the fetch (headers, body).
        std::string            bodyOwned;
        std::vector<std::string> headerStrings;
        std::vector<const char*> headerPtrs; // null-terminated, points into headerStrings
    };
};

static void onFetchSuccess(emscripten_fetch_t* fetch) {
    auto* state = static_cast<HttpClient::Impl::FetchState*>(fetch->userData);
    HttpResponse resp;
    resp.statusCode = fetch->status;
    resp.success    = (fetch->status >= 200 && fetch->status < 300);
    if (fetch->numBytes > 0) {
        const auto* d = reinterpret_cast<const uint8_t*>(fetch->data);
        resp.body.assign(d, d + fetch->numBytes);
    }
    state->callback(std::move(resp));
    delete state;
    emscripten_fetch_close(fetch);
}

static void onFetchError(emscripten_fetch_t* fetch) {
    auto* state = static_cast<HttpClient::Impl::FetchState*>(fetch->userData);
    HttpResponse resp;
    resp.statusCode = fetch->status;
    resp.success    = false;
    resp.error      = "fetch failed";
    state->callback(std::move(resp));
    delete state;
    emscripten_fetch_close(fetch);
}

static emscripten_fetch_t* launchFetch(const std::string& method,
                                        const std::string& url,
                                        HttpClient::Impl::FetchState* state) {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strncpy(attr.requestMethod, method.c_str(), sizeof(attr.requestMethod) - 1);
    attr.attributes    = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess     = onFetchSuccess;
    attr.onerror       = onFetchError;
    attr.userData      = state;
    if (!state->headerPtrs.empty())
        attr.requestHeaders = state->headerPtrs.data();
    if (!state->bodyOwned.empty()) {
        attr.requestData     = state->bodyOwned.data();
        attr.requestDataSize = state->bodyOwned.size();
    }
    return emscripten_fetch(&attr, url.c_str());
}

HttpClient::HttpClient() : _impl(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

HttpRequestID HttpClient::Get(const std::string& url, HttpCallback cb) {
    auto* state    = new Impl::FetchState{_impl->nextId++, std::move(cb)};
    HttpRequestID id = state->id;
    launchFetch("GET", url, state);
    return id;
}

HttpRequestID HttpClient::PostJson(const std::string& url,
                                    const std::string& json,
                                    HttpCallback cb) {
    auto* state      = new Impl::FetchState{_impl->nextId++, std::move(cb)};
    state->bodyOwned = json;
    state->headerStrings = {"Content-Type", "application/json"};
    state->headerPtrs    = {state->headerStrings[0].c_str(),
                             state->headerStrings[1].c_str(), nullptr};
    HttpRequestID id = state->id;
    launchFetch("POST", url, state);
    return id;
}

HttpRequestID HttpClient::PostMultipart(const std::string& url,
                                         const std::vector<MultipartField>& fields,
                                         HttpCallback cb) {
    const std::string boundary = "AtmosphericBoundary7MA4YWxkTrZu0gW";
    auto* state      = new Impl::FetchState{_impl->nextId++, std::move(cb)};
    state->bodyOwned = buildMultipartBody(fields, boundary);
    const std::string ctValue = "multipart/form-data; boundary=" + boundary;
    state->headerStrings = {"Content-Type", ctValue};
    state->headerPtrs    = {state->headerStrings[0].c_str(),
                             state->headerStrings[1].c_str(), nullptr};
    HttpRequestID id = state->id;
    launchFetch("POST", url, state);
    return id;
}

void HttpClient::Cancel(HttpRequestID) {
    // emscripten_fetch has no synchronous cancel; requests run to completion.
}

int HttpClient::ActiveRequestCount() const {
    return 0; // emscripten_fetch owns its own lifetime; we don't track in-flight count
}

void HttpClient::Pump() {
    // No-op: browser fires fetch callbacks asynchronously without polling.
}

#endif // __EMSCRIPTEN__
