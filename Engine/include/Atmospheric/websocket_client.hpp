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

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    // url must be ws:// or wss://
    WsConnectionID Connect(const std::string& url, WsCallbacks callbacks);
    void Send(WsConnectionID id, const std::vector<uint8_t>& data);
    void SendText(WsConnectionID id, const std::string& text);
    void Close(WsConnectionID id);
    WsState GetState(WsConnectionID id) const;
    int ConnectionCount() const;

    // Called by NetworkSubsystem::Process — drives lws_service on native,
    // no-op on Emscripten (browser fires WebSocket callbacks asynchronously).
    void Pump();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
