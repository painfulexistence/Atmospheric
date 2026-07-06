#pragma once
#include "http_client.hpp"
#include "subsystem.hpp"
#include "websocket_client.hpp"

// NetworkSubsystem
//
// Engine-level subsystem providing HTTP and WebSocket networking.
// Register via Application::AddSubsystem<NetworkSubsystem>() and
// retrieve the same instance with Application::Get()->GetSubsystem<NetworkSubsystem>().
//
// Usage:
//   auto net = app->AddSubsystem<NetworkSubsystem>();
//   net->Http().PostJson("https://api.example.com/feed", body, [](HttpResponse r) { ... });
//   auto connId = net->Ws().Connect("wss://api.example.com/ws", callbacks);
//
// Process(dt) is called automatically each frame by the Application.
// It pumps the curl multi-handle (HTTP) and lws service loop (WebSocket).
class NetworkSubsystem : public Subsystem {
public:
    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;

    HttpClient& Http() {
        return _http;
    }
    WebSocketClient& Ws() {
        return _ws;
    }

private:
    HttpClient _http;
    WebSocketClient _ws;
};
