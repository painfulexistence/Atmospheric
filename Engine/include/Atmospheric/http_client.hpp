#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using HttpRequestID = uint32_t;

struct MultipartField {
    std::string name;
    std::string filename;    // empty → regular text field
    std::string contentType; // e.g. "image/jpeg"; ignored when filename is empty
    std::vector<uint8_t> data;
};

struct HttpResponse {
    int statusCode = 0;
    std::vector<uint8_t> body;
    bool success = false;
    std::string error;
};

using HttpCallback = std::function<void(HttpResponse)>;

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpRequestID Get(const std::string& url, HttpCallback cb);
    HttpRequestID PostJson(const std::string& url,
                            const std::string& json,
                            HttpCallback cb);
    HttpRequestID PostMultipart(const std::string& url,
                                 const std::vector<MultipartField>& fields,
                                 HttpCallback cb);
    void Cancel(HttpRequestID id);
    int  ActiveRequestCount() const;

    // Called by NetworkSubsystem::Process — drives curl_multi on native,
    // no-op on Emscripten (browser fires fetch callbacks asynchronously).
    void Pump();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
