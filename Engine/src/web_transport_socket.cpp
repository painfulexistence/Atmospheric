#include "web_transport_socket.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

// ── JS glue: one WebTransport session, datagram mode ───────────────────────────
// State machine mirrored to C++ via js_wt_state(): 0 connecting, 1 open,
// -1 failed/closed. Incoming datagrams are queued on window._wtRecvQueue and
// drained by js_wt_recv, exactly like the WebRTC DataChannel path in
// net_lockstep.cpp.
// clang-format off
// NOLINTBEGIN
EM_JS(void, js_wt_connect, (const char* urlPtr), {
    var url = UTF8ToString(urlPtr);
    window._wtRecvQueue = [];
    window._wtWriter = null;
    window._wtState = 0; // connecting
    try {
        // With a CA-signed cert for the URL's host, no options are needed. For a
        // self-signed cert, pass { serverCertificateHashes: [{ algorithm:'sha-256',
        // value: <ArrayBuffer> }] } here instead.
        var wt = new WebTransport(url);
        window._wt = wt;
        wt.closed.then(function() { window._wtState = -1; })
                 .catch(function() { window._wtState = -1; });
        wt.ready.then(function() {
            window._wtState = 1; // open
            window._wtWriter = wt.datagrams.writable.getWriter();
            var reader = wt.datagrams.readable.getReader();
            function pump() {
                reader.read().then(function(res) {
                    if (res.done) { window._wtState = -1; return; }
                    window._wtRecvQueue.push(res.value); // Uint8Array
                    pump();
                }).catch(function() { window._wtState = -1; });
            }
            pump();
        }).catch(function() { window._wtState = -1; }); // handshake failed
    } catch (e) {
        window._wtState = -1;
    }
});

EM_JS(int, js_wt_state, (), {
    return (window._wtState === undefined) ? -1 : window._wtState;
});

EM_JS(void, js_wt_send, (const uint8_t* data, int len), {
    var w = window._wtWriter;
    if (!w) return;
    // Copy out of the (SharedArrayBuffer-backed, under -sUSE_PTHREADS) heap into a
    // fresh, non-shared Uint8Array — WebTransport's writer rejects SharedArrayBuffer
    // views, the same gotcha as RTCDataChannel.send (see net_lockstep.cpp).
    var copy = new Uint8Array(len);
    copy.set(HEAPU8.subarray(data, data + len));
    w.write(copy); // datagrams are fire-and-forget; ignore the returned promise
});

EM_JS(int, js_wt_recv, (uint8_t* buf, int maxLen), {
    var q = window._wtRecvQueue;
    if (!q || !q.length) return 0;
    var src = q.shift(); // Uint8Array
    var n = Math.min(src.byteLength, maxLen);
    HEAPU8.set(src.subarray(0, n), buf);
    return n;
});

EM_JS(void, js_wt_close, (), {
    try { if (window._wt) window._wt.close(); } catch (e) {}
    window._wt = null;
    window._wtWriter = null;
    window._wtRecvQueue = [];
    window._wtState = -1;
});
// NOLINTEND
// clang-format on

WebTransportSocket::~WebTransportSocket() {
    Close();
}

bool WebTransportSocket::Connect(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return false;// WebTransport requires https
    js_wt_connect(url.c_str());
    return true;
}

void WebTransportSocket::Close() {
    js_wt_close();
}

bool WebTransportSocket::IsOpen() const {
    return js_wt_state() == 1;
}

bool WebTransportSocket::IsConnecting() const {
    return js_wt_state() == 0;
}

bool WebTransportSocket::Failed() const {
    return js_wt_state() < 0;
}

void WebTransportSocket::Send(const uint8_t* data, int len) {
    if (len > 0) js_wt_send(data, len);
}

int WebTransportSocket::Recv(uint8_t* buf, int maxLen) {
    return js_wt_recv(buf, maxLen);
}

#endif// __EMSCRIPTEN__
