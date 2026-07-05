#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using WsConnectionID = uint32_t;

enum class WsState { Connecting, Open, Closed, Failed };

struct WsCallbacks {
    std::function<void()> onOpen;
    std::function<void(std::vector<uint8_t>)> onMessage;
    std::function<void(int code, std::string reason)> onClose;
};

struct WsOptions {
    // Accept self-signed certificates and skip hostname verification on wss://
    // connections. Development convenience only — never enable against
    // production servers. Ignored on Emscripten (the browser owns TLS).
    bool allowInsecureTls = false;
};

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    // url must be ws:// or wss://
    WsConnectionID Connect(const std::string& url, WsCallbacks callbacks, const WsOptions& options = {});
    void Send(WsConnectionID id, const std::vector<uint8_t>& data);
    void SendText(WsConnectionID id, const std::string& text);
    void Close(WsConnectionID id);
    WsState GetState(WsConnectionID id) const;
    int ConnectionCount() const;

    // Called by NetworkSubsystem::Process — drives lws_service on native,
    // no-op on Emscripten (browser fires WebSocket callbacks asynchronously).
    void Pump();

    // pimpl — forward declaration is public so the C callback shims in the
    // .cpp can name it; the definition never leaves the translation unit.
    struct Impl;

private:
    std::unique_ptr<Impl> _impl;
};
