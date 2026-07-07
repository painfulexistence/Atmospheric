#include "websocket_client.hpp"
#include <spdlog/spdlog.h>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Native implementation (libwebsockets, OpenSSL)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef __EMSCRIPTEN__

#include <cstring>
#include <libwebsockets.h>
#include <queue>
#include <string>

struct WsConnection {
    WsCallbacks callbacks;
    WsState state = WsState::Connecting;
    struct lws* wsi = nullptr;
    std::queue<std::vector<uint8_t>> sendQueue;
};

// Per-session data allocated by lws for each connection.
struct WsPerSessionData {
    WsConnectionID id;
};

struct WebSocketClient::Impl {
    lws_context* ctx = nullptr;
    WsConnectionID nextId = 1;
    std::unordered_map<WsConnectionID, WsConnection> connections;
    std::unordered_map<struct lws*, WsConnectionID> wsiToId;
};

static int lwsCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
    auto* psd = static_cast<WsPerSessionData*>(user);
    auto* impl = static_cast<WebSocketClient::Impl*>(lws_context_user(lws_get_context(wsi)));
    if (!impl) return 0;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED: {
        // Resolve id from wsi (populated when we called lws_client_connect_via_info).
        auto it = impl->wsiToId.find(wsi);
        if (it == impl->wsiToId.end()) break;
        WsConnectionID id = it->second;
        if (psd) psd->id = id;
        auto cit = impl->connections.find(id);
        if (cit == impl->connections.end()) break;
        cit->second.state = WsState::Open;
        if (cit->second.callbacks.onOpen) cit->second.callbacks.onOpen();
        break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE: {
        if (!psd) break;
        auto cit = impl->connections.find(psd->id);
        if (cit == impl->connections.end()) break;
        if (cit->second.callbacks.onMessage) {
            const auto* d = static_cast<const uint8_t*>(in);
            cit->second.callbacks.onMessage(std::vector<uint8_t>(d, d + len));
        }
        break;
    }
    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        if (!psd) break;
        auto cit = impl->connections.find(psd->id);
        if (cit == impl->connections.end()) break;
        auto& q = cit->second.sendQueue;
        if (q.empty()) break;
        const auto& msg = q.front();
        // lws requires LWS_PRE bytes of padding before the payload.
        std::vector<uint8_t> buf(LWS_PRE + msg.size());
        std::memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());
        lws_write(wsi, buf.data() + LWS_PRE, msg.size(), LWS_WRITE_BINARY);
        q.pop();
        if (!q.empty()) lws_callback_on_writable(wsi);
        break;
    }
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
        WsConnectionID id = psd ? psd->id : 0;
        if (id == 0) {
            // ESTABLISHED was never reached; look up by wsi.
            auto it = impl->wsiToId.find(wsi);
            if (it != impl->wsiToId.end()) id = it->second;
        }
        auto cit = impl->connections.find(id);
        if (cit != impl->connections.end()) {
            cit->second.state = (reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR) ? WsState::Failed : WsState::Closed;
            if (cit->second.callbacks.onClose) cit->second.callbacks.onClose(0, "");
            impl->connections.erase(cit);
        }
        impl->wsiToId.erase(wsi);
        break;
    }
    default:
        break;
    }
    return 0;
}

static const struct lws_protocols kProtocols[] = {
    { "ws", lwsCallback, sizeof(WsPerSessionData), 65536, 0, nullptr, 0 }, LWS_PROTOCOL_LIST_TERM
};

struct ParsedWsUrl {
    std::string host;
    std::string path;
    int port;
    bool ssl;
};

static ParsedWsUrl parseWsUrl(const std::string& url) {
    ParsedWsUrl r;
    r.ssl = (url.rfind("wss://", 0) == 0);
    r.port = r.ssl ? 443 : 80;
    const size_t schemeEnd = url.find("://") + 3;
    const std::string rest = url.substr(schemeEnd);
    const size_t pathStart = rest.find('/');
    r.path = (pathStart == std::string::npos) ? "/" : rest.substr(pathStart);
    const std::string hostPort = (pathStart == std::string::npos) ? rest : rest.substr(0, pathStart);
    const size_t colon = hostPort.find(':');
    if (colon != std::string::npos) {
        r.host = hostPort.substr(0, colon);
        r.port = std::stoi(hostPort.substr(colon + 1));
    } else {
        r.host = hostPort;
    }
    return r;
}

WebSocketClient::WebSocketClient() : _impl(std::make_unique<Impl>()) {
    lws_context_creation_info info{};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = kProtocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.user = _impl.get();
    _impl->ctx = lws_create_context(&info);
    if (!_impl->ctx) spdlog::error("WebSocketClient: failed to create lws context");
}

WebSocketClient::~WebSocketClient() {
    if (_impl->ctx) lws_context_destroy(_impl->ctx);
}

WsConnectionID WebSocketClient::Connect(const std::string& url, WsCallbacks cbs, const WsOptions& options) {
    if (!_impl->ctx) return 0;
    const WsConnectionID id = _impl->nextId++;
    WsConnection conn;
    conn.callbacks = std::move(cbs);
    conn.state = WsState::Connecting;
    _impl->connections.emplace(id, std::move(conn));

    const auto parsed = parseWsUrl(url);
    lws_client_connect_info ccinfo{};
    ccinfo.context = _impl->ctx;
    ccinfo.address = parsed.host.c_str();
    ccinfo.port = parsed.port;
    ccinfo.path = parsed.path.c_str();
    ccinfo.host = parsed.host.c_str();
    ccinfo.origin = parsed.host.c_str();
    ccinfo.protocol = kProtocols[0].name;
    ccinfo.ssl_connection = 0;
    if (parsed.ssl) {
        ccinfo.ssl_connection = LCCSCF_USE_SSL;
        if (options.allowInsecureTls) {
            spdlog::warn(
                "WebSocketClient: TLS verification disabled for {} "
                "(WsOptions::allowInsecureTls) — dev only",
                url
            );
            ccinfo.ssl_connection |= LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
        }
    }

    struct lws* wsi = lws_client_connect_via_info(&ccinfo);
    if (wsi) {
        _impl->wsiToId[wsi] = id;
        _impl->connections[id].wsi = wsi;
    } else {
        spdlog::error("WebSocketClient: lws_client_connect_via_info failed for {}", url);
        auto& c = _impl->connections[id];
        c.state = WsState::Failed;
        if (c.callbacks.onClose) c.callbacks.onClose(-1, "connect failed");
        _impl->connections.erase(id);
    }
    return id;
}

void WebSocketClient::Send(WsConnectionID id, const std::vector<uint8_t>& data) {
    auto it = _impl->connections.find(id);
    if (it == _impl->connections.end()) return;
    it->second.sendQueue.push(data);
    if (it->second.wsi && it->second.state == WsState::Open) lws_callback_on_writable(it->second.wsi);
}

void WebSocketClient::SendText(WsConnectionID id, const std::string& text) {
    Send(id, std::vector<uint8_t>(text.begin(), text.end()));
}

void WebSocketClient::Close(WsConnectionID id) {
    auto it = _impl->connections.find(id);
    if (it == _impl->connections.end() || !it->second.wsi) return;
    lws_close_reason(it->second.wsi, LWS_CLOSE_STATUS_NORMAL, nullptr, 0);
    // lws will call LWS_CALLBACK_CLIENT_CLOSED, which cleans up _connections.
}

WsState WebSocketClient::GetState(WsConnectionID id) const {
    auto it = _impl->connections.find(id);
    return (it != _impl->connections.end()) ? it->second.state : WsState::Closed;
}

int WebSocketClient::ConnectionCount() const {
    return static_cast<int>(_impl->connections.size());
}

void WebSocketClient::Pump() {
    if (_impl->ctx) lws_service(_impl->ctx, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Emscripten implementation (emscripten/websocket.h, enabled via -sWEBSOCKET=1)
// ─────────────────────────────────────────────────────────────────────────────
#else

#include <emscripten/websocket.h>

struct WsEmConn {
    WsCallbacks callbacks;
    WsState state = WsState::Connecting;
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
};

struct WebSocketClient::Impl {
    WsConnectionID nextId = 1;
    std::unordered_map<WsConnectionID, WsEmConn> connections;
    std::unordered_map<EMSCRIPTEN_WEBSOCKET_T, WsConnectionID> socketToId;
};

// userData passed to emscripten callbacks is a pair<Impl*, WsConnectionID>*
// heap-allocated; deleted when the connection closes.
struct EmWsRef {
    WebSocketClient::Impl* impl;
    WsConnectionID id;
};

static EM_BOOL emWsOnOpen(int, const EmscriptenWebSocketOpenEvent* e, void* ud) {
    auto* ref = static_cast<EmWsRef*>(ud);
    auto it = ref->impl->connections.find(ref->id);
    if (it != ref->impl->connections.end()) {
        it->second.state = WsState::Open;
        if (it->second.callbacks.onOpen) it->second.callbacks.onOpen();
    }
    return EM_TRUE;
}

static EM_BOOL emWsOnMessage(int, const EmscriptenWebSocketMessageEvent* e, void* ud) {
    auto* ref = static_cast<EmWsRef*>(ud);
    auto it = ref->impl->connections.find(ref->id);
    if (it == ref->impl->connections.end()) return EM_TRUE;
    if (it->second.callbacks.onMessage) {
        const auto* d = static_cast<const uint8_t*>(e->data);
        it->second.callbacks.onMessage(std::vector<uint8_t>(d, d + e->numBytes));
    }
    return EM_TRUE;
}

static EM_BOOL emWsOnError(int, const EmscriptenWebSocketErrorEvent*, void* ud) {
    auto* ref = static_cast<EmWsRef*>(ud);
    auto it = ref->impl->connections.find(ref->id);
    if (it != ref->impl->connections.end()) {
        it->second.state = WsState::Failed;
        if (it->second.callbacks.onClose) it->second.callbacks.onClose(-1, "error");
        ref->impl->connections.erase(it);
    }
    ref->impl->socketToId.erase(it != ref->impl->connections.end() ? it->second.socket : 0);
    delete ref;
    return EM_TRUE;
}

static EM_BOOL emWsOnClose(int, const EmscriptenWebSocketCloseEvent* e, void* ud) {
    auto* ref = static_cast<EmWsRef*>(ud);
    auto it = ref->impl->connections.find(ref->id);
    if (it != ref->impl->connections.end()) {
        it->second.state = WsState::Closed;
        if (it->second.callbacks.onClose) it->second.callbacks.onClose(e->code, e->reason);
        ref->impl->socketToId.erase(it->second.socket);
        ref->impl->connections.erase(it);
    }
    delete ref;
    return EM_TRUE;
}

WebSocketClient::WebSocketClient() : _impl(std::make_unique<Impl>()) {
}

WebSocketClient::~WebSocketClient() {
    for (auto& [id, conn] : _impl->connections) {
        if (conn.socket) emscripten_websocket_close(conn.socket, 1000, "shutdown");
    }
}

WsConnectionID
    WebSocketClient::Connect(const std::string& url, WsCallbacks cbs, const WsOptions& /*options: browser owns TLS*/) {
    EmscriptenWebSocketCreateAttributes attr{ url.c_str(), nullptr, EM_TRUE };
    EMSCRIPTEN_WEBSOCKET_T sock = emscripten_websocket_new(&attr);
    if (sock <= 0) {
        spdlog::error("WebSocketClient: emscripten_websocket_new failed for {}", url);
        return 0;
    }
    const WsConnectionID id = _impl->nextId++;
    WsEmConn conn;
    conn.callbacks = std::move(cbs);
    conn.state = WsState::Connecting;
    conn.socket = sock;
    _impl->connections.emplace(id, std::move(conn));
    _impl->socketToId[sock] = id;

    auto* ref = new EmWsRef{ _impl.get(), id };
    emscripten_websocket_set_onopen_callback(sock, ref, emWsOnOpen);
    emscripten_websocket_set_onmessage_callback(sock, ref, emWsOnMessage);
    emscripten_websocket_set_onerror_callback(sock, ref, emWsOnError);
    emscripten_websocket_set_onclose_callback(sock, ref, emWsOnClose);
    return id;
}

void WebSocketClient::Send(WsConnectionID id, const std::vector<uint8_t>& data) {
    auto it = _impl->connections.find(id);
    if (it == _impl->connections.end() || it->second.state != WsState::Open) return;
    emscripten_websocket_send_binary(
        it->second.socket, const_cast<void*>(static_cast<const void*>(data.data())), static_cast<uint32_t>(data.size())
    );
}

void WebSocketClient::SendText(WsConnectionID id, const std::string& text) {
    auto it = _impl->connections.find(id);
    if (it == _impl->connections.end() || it->second.state != WsState::Open) return;
    emscripten_websocket_send_utf8_text(it->second.socket, text.c_str());
}

void WebSocketClient::Close(WsConnectionID id) {
    auto it = _impl->connections.find(id);
    if (it == _impl->connections.end()) return;
    emscripten_websocket_close(it->second.socket, 1000, "");
}

WsState WebSocketClient::GetState(WsConnectionID id) const {
    auto it = _impl->connections.find(id);
    return (it != _impl->connections.end()) ? it->second.state : WsState::Closed;
}

int WebSocketClient::ConnectionCount() const {
    return static_cast<int>(_impl->connections.size());
}

void WebSocketClient::Pump() {
    // No-op: browser fires WebSocket callbacks asynchronously without polling.
}

#endif// __EMSCRIPTEN__
