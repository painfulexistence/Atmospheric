#include "http_client.hpp"
#include <spdlog/spdlog.h>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Native implementation (libcurl multi, HTTP/2 + OpenSSL)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef __EMSCRIPTEN__

#include <curl/curl.h>
#include <cctype>

struct HttpClient::Impl {
    CURLM* multi = nullptr;

    struct Request {
        HttpRequestID id;
        HttpCallback  callback;
        HttpResponse  response;
        std::vector<uint8_t> body;  // owned copy for CURLOPT_POSTFIELDS
        curl_slist*   headers = nullptr;
        curl_mime*    mime    = nullptr;
        std::function<void(uint64_t, uint64_t)> onProgress;
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

    CURL* makeHandle(const std::string& url, int timeoutSeconds) {
        CURL* h = curl_easy_init();
        curl_easy_setopt(h, CURLOPT_URL,            url.c_str());
        curl_easy_setopt(h, CURLOPT_HTTP_VERSION,   CURL_HTTP_VERSION_2TLS);
        curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(h, CURLOPT_TIMEOUT,        (long)timeoutSeconds);
        return h;
    }

    // Registers the request and binds all curl options that point into the
    // Request. Must run AFTER the emplace: the map node's address is stable
    // for the lifetime of the entry (unordered_map never moves nodes), while
    // the stack-local Request the caller built is about to die.
    HttpRequestID start(CURL* h, Request&& req) {
        const HttpRequestID id = req.id;
        Request& stored = pending.emplace(h, std::move(req)).first->second;
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION,  writeCallback);
        curl_easy_setopt(h, CURLOPT_WRITEDATA,      &stored.response.body);
        curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(h, CURLOPT_HEADERDATA,     &stored.response.headers);
        if (stored.headers)
            curl_easy_setopt(h, CURLOPT_HTTPHEADER, stored.headers);
        if (!stored.body.empty()) {
            curl_easy_setopt(h, CURLOPT_POSTFIELDS,
                              reinterpret_cast<const char*>(stored.body.data()));
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)stored.body.size());
        }
        if (stored.mime)
            curl_easy_setopt(h, CURLOPT_MIMEPOST, stored.mime);
        if (stored.onProgress) {
            curl_easy_setopt(h, CURLOPT_NOPROGRESS,       0L);
            curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xferCallback);
            curl_easy_setopt(h, CURLOPT_XFERINFODATA,     &stored);
        }
        curl_multi_add_handle(multi, h);
        return id;
    }

    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* ud) {
        auto* body = static_cast<std::vector<uint8_t>*>(ud);
        body->insert(body->end(), ptr, ptr + size * nmemb);
        return size * nmemb;
    }

    static size_t headerCallback(char* ptr, size_t size, size_t nitems, void* ud) {
        auto* headers = static_cast<HttpHeaders*>(ud);
        const size_t total = size * nitems;
        std::string line(ptr, total);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            auto trim = [](std::string s) {
                while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
                while (!s.empty() && std::isspace((unsigned char)s.back()))  s.pop_back();
                return s;
            };
            headers->emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
        }
        return total;
    }

    static int xferCallback(void* ud, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
        auto* req = static_cast<Request*>(ud);
        if (req->onProgress) {
            if (ultotal > 0 && ulnow < ultotal)
                req->onProgress((uint64_t)ulnow, (uint64_t)ultotal);
            else if (dltotal > 0)
                req->onProgress((uint64_t)dlnow, (uint64_t)dltotal);
        }
        return 0; // non-zero would abort the transfer
    }
};

HttpClient::HttpClient() : _impl(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

HttpRequestID HttpClient::Send(const HttpRequest& r, HttpCallback cb) {
    Impl::Request req;
    req.id         = _impl->nextId++;
    req.callback   = std::move(cb);
    req.body       = r.body;
    req.onProgress = r.onProgress;
    for (const auto& [key, value] : r.headers)
        req.headers = curl_slist_append(req.headers, (key + ": " + value).c_str());
    if (!r.contentType.empty())
        req.headers = curl_slist_append(req.headers,
                                         ("Content-Type: " + r.contentType).c_str());

    CURL* h = _impl->makeHandle(r.url, r.timeoutSeconds);
    if (r.method != "GET")
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, r.method.c_str());
    // Body-less POST/PUT/PATCH still need curl switched into upload mode,
    // otherwise CUSTOMREQUEST alone sends GET semantics with a renamed verb.
    if (r.body.empty() && (r.method == "POST" || r.method == "PUT" || r.method == "PATCH")) {
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, 0L);
    }
    return _impl->start(h, std::move(req));
}

HttpRequestID HttpClient::PostMultipart(const std::string& url,
                                         const std::vector<MultipartField>& fields,
                                         HttpCallback cb, const HttpHeaders& headers) {
    Impl::Request req;
    req.id       = _impl->nextId++;
    req.callback = std::move(cb);
    for (const auto& [key, value] : headers)
        req.headers = curl_slist_append(req.headers, (key + ": " + value).c_str());

    CURL* h = _impl->makeHandle(url, 30);
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
    return _impl->start(h, std::move(req));
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
        resp.headers    = std::move(req.response.headers);
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
        std::string              bodyOwned;
        std::vector<std::string> headerStrings; // flat [key, value, key, value, ...]
        std::vector<const char*> headerPtrs;    // null-terminated, points into headerStrings
        std::function<void(uint64_t, uint64_t)> onProgress;

        // Call after ALL headerStrings pushes: growing the vector moves the
        // strings, which would invalidate previously captured c_str pointers.
        void finalizeHeaders() {
            headerPtrs.clear();
            for (const auto& s : headerStrings) headerPtrs.push_back(s.c_str());
            headerPtrs.push_back(nullptr);
        }
    };
};

static HttpHeaders parseResponseHeaders(emscripten_fetch_t* fetch) {
    HttpHeaders out;
    const size_t len = emscripten_fetch_get_response_headers_length(fetch);
    if (len == 0) return out;
    std::string raw(len + 1, '\0');
    emscripten_fetch_get_response_headers(fetch, raw.data(), len + 1);
    size_t pos = 0;
    while (pos < len) {
        size_t eol = raw.find("\r\n", pos);
        if (eol == std::string::npos) eol = len;
        const std::string line = raw.substr(pos, eol - pos);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            auto trim = [](std::string s) {
                while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
                while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
                return s;
            };
            out.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
        }
        pos = eol + 2;
    }
    return out;
}

static void onFetchSuccess(emscripten_fetch_t* fetch) {
    auto* state = static_cast<HttpClient::Impl::FetchState*>(fetch->userData);
    HttpResponse resp;
    resp.statusCode = fetch->status;
    resp.success    = (fetch->status >= 200 && fetch->status < 300);
    resp.headers    = parseResponseHeaders(fetch);
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

static void onFetchProgress(emscripten_fetch_t* fetch) {
    auto* state = static_cast<HttpClient::Impl::FetchState*>(fetch->userData);
    if (state->onProgress && fetch->totalBytes > 0)
        state->onProgress(fetch->dataOffset, fetch->totalBytes);
}

static emscripten_fetch_t* launchFetch(const std::string& method,
                                        const std::string& url,
                                        HttpClient::Impl::FetchState* state,
                                        int timeoutSeconds) {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strncpy(attr.requestMethod, method.c_str(), sizeof(attr.requestMethod) - 1);
    attr.attributes    = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.timeoutMSecs  = (unsigned long)timeoutSeconds * 1000;
    attr.onsuccess     = onFetchSuccess;
    attr.onerror       = onFetchError;
    if (state->onProgress) attr.onprogress = onFetchProgress;
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

HttpRequestID HttpClient::Send(const HttpRequest& r, HttpCallback cb) {
    auto* state = new Impl::FetchState{_impl->nextId++, std::move(cb)};
    state->bodyOwned.assign(r.body.begin(), r.body.end());
    state->onProgress = r.onProgress;
    for (const auto& [key, value] : r.headers) {
        state->headerStrings.push_back(key);
        state->headerStrings.push_back(value);
    }
    if (!r.contentType.empty()) {
        state->headerStrings.push_back("Content-Type");
        state->headerStrings.push_back(r.contentType);
    }
    if (!state->headerStrings.empty()) state->finalizeHeaders();
    HttpRequestID id = state->id;
    launchFetch(r.method, r.url, state, r.timeoutSeconds);
    return id;
}

HttpRequestID HttpClient::PostMultipart(const std::string& url,
                                         const std::vector<MultipartField>& fields,
                                         HttpCallback cb, const HttpHeaders& headers) {
    const std::string boundary = "AtmosphericBoundary7MA4YWxkTrZu0gW";
    auto* state      = new Impl::FetchState{_impl->nextId++, std::move(cb)};
    state->bodyOwned = buildMultipartBody(fields, boundary);
    for (const auto& [key, value] : headers) {
        state->headerStrings.push_back(key);
        state->headerStrings.push_back(value);
    }
    state->headerStrings.push_back("Content-Type");
    state->headerStrings.push_back("multipart/form-data; boundary=" + boundary);
    state->finalizeHeaders();
    HttpRequestID id = state->id;
    launchFetch("POST", url, state, 30);
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

// ─────────────────────────────────────────────────────────────────────────────
// Shared convenience wrappers
// ─────────────────────────────────────────────────────────────────────────────

HttpRequestID HttpClient::Get(const std::string& url, HttpCallback cb,
                                const HttpHeaders& headers) {
    HttpRequest r;
    r.url     = url;
    r.headers = headers;
    return Send(r, std::move(cb));
}

HttpRequestID HttpClient::PostJson(const std::string& url, const std::string& json,
                                     HttpCallback cb, const HttpHeaders& headers) {
    HttpRequest r;
    r.method      = "POST";
    r.url         = url;
    r.headers     = headers;
    r.contentType = "application/json";
    r.body.assign(json.begin(), json.end());
    return Send(r, std::move(cb));
}
