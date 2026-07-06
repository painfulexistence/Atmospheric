#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using HttpRequestID = uint32_t;
using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct MultipartField {
    std::string name;
    std::string filename;// empty → regular text field
    std::string contentType;// e.g. "image/jpeg"; ignored when filename is empty
    std::vector<uint8_t> data;
};

struct HttpRequest {
    std::string method = "GET";// GET, POST, PUT, PATCH, DELETE, ...
    std::string url;
    HttpHeaders headers;// e.g. {{"Authorization", "Bearer <token>"}}
    std::vector<uint8_t> body;// sent when non-empty
    std::string contentType;// convenience — appended as a Content-Type header
    int timeoutSeconds = 30;
    // Transfer progress (transferred, total). Native reports upload progress
    // while a body is being sent, then download progress; Emscripten reports
    // download progress only (fetch has no upload-progress hook).
    std::function<void(uint64_t transferred, uint64_t total)> onProgress;
};

struct HttpResponse {
    int statusCode = 0;
    std::vector<uint8_t> body;
    HttpHeaders headers;
    bool success = false;
    std::string error;
};

using HttpCallback = std::function<void(HttpResponse)>;

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // General entry point — full control over method, headers, body, progress.
    HttpRequestID Send(const HttpRequest& request, HttpCallback cb);

    // Convenience wrappers over Send().
    HttpRequestID Get(const std::string& url, HttpCallback cb, const HttpHeaders& headers = {});
    HttpRequestID
        PostJson(const std::string& url, const std::string& json, HttpCallback cb, const HttpHeaders& headers = {});
    // Multipart upload keeps its own path (curl_mime on native).
    HttpRequestID PostMultipart(
        const std::string& url,
        const std::vector<MultipartField>& fields,
        HttpCallback cb,
        const HttpHeaders& headers = {}
    );

    void Cancel(HttpRequestID id);
    int ActiveRequestCount() const;

    // Called by NetworkSubsystem::Process — drives curl_multi on native,
    // no-op on Emscripten (browser fires fetch callbacks asynchronously).
    void Pump();

    // pimpl — forward declaration is public so the C callback shims in the
    // .cpp can name it; the definition never leaves the translation unit.
    struct Impl;

private:
    std::unique_ptr<Impl> _impl;
};
