#pragma once
#include "console.hpp"
#include "globals.hpp"
#include <cstring>

// GL_TIMESTAMP / glQueryCounter require OpenGL ≥ 3.3 (GL_ARB_timer_query,
// promoted to core in 3.3). Atmospheric targets GL 4.1+ on desktop so this
// is always available there -- except Apple's OpenGL driver, which is the
// odd one out: it advertises GL_ARB_timer_query and GL_QUERY_RESULT_AVAILABLE
// flips true almost immediately, but the timestamp itself is never actually
// written (observed in practice: both start and end queries read back as 0
// on macOS). Apple deprecated OpenGL entirely in 10.14 and this extension
// never got a real backing implementation on either macOS or iOS GLES.
//
// Disabled on targets that lack support:
//   - WebGL 1/2 (__EMSCRIPTEN__): no GL_TIMESTAMP; EXT_disjoint_timer_query
//     exists but has unreliable disjoint semantics — not worth handling.
//   - Android GLES (__ANDROID__): extension not guaranteed.
//   - All Apple targets (__APPLE__, macOS + iOS): present but non-functional.
// On these platforms all GpuTimer methods are inlined no-ops and GetMs()
// always returns 0.
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !defined(__APPLE__)
#define AE_GPU_TIMER_ENABLED 1
#endif

// Double-buffered per-pass GPU timer using GL_TIMESTAMP queries.
//
// Usage:
//   timer.Begin();          // records start timestamp into write-buffer
//   ... GPU commands ...
//   timer.End();            // records end timestamp; reads back *previous* frame
//   float ms = timer.GetMs();
//
// The read-back is always one frame behind, so it never stalls the pipeline.
struct GpuTimer {
#ifdef AE_GPU_TIMER_ENABLED
    GLuint startQ[2] = {0, 0};
    GLuint endQ[2]   = {0, 0};
    int    write     = 0;
    bool   valid     = false;
    int    pendingFrames = 0;  // consecutive End() calls where last frame's result wasn't available yet

    // Move-only: GL handles must not be shared.
    GpuTimer() = default;
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    GpuTimer(GpuTimer&& o) noexcept { *this = std::move(o); }
    GpuTimer& operator=(GpuTimer&& o) noexcept {
        if (this == &o) return *this;
        std::memcpy(startQ, o.startQ, sizeof(startQ));
        std::memcpy(endQ,   o.endQ,   sizeof(endQ));
        std::memset(o.startQ, 0, sizeof(o.startQ));
        std::memset(o.endQ,   0, sizeof(o.endQ));
        write = o.write;  o.write = 0;
        valid = o.valid;  o.valid = false;
        pendingFrames = o.pendingFrames; o.pendingFrames = 0;
        ms    = o.ms;     o.ms   = 0.0f;
        return *this;
    }
#endif

    float ms = 0.0f;

    void Init() {
#ifdef AE_GPU_TIMER_ENABLED
        glGetError();  // clear any stale error so the check below is meaningful
        glGenQueries(2, startQ);
        glGenQueries(2, endQ);
        GLenum err = glGetError();

        static bool loggedOnce = false;
        if (!loggedOnce) {
            loggedOnce = true;
            if (err != GL_NO_ERROR || startQ[0] == 0 || endQ[0] == 0) {
                ENGINE_LOG(
                    "GpuTimer: glGenQueries failed (GL error 0x{:x}) on '{}' / GL {} -- "
                    "per-pass GPU timings will read 0.",
                    static_cast<unsigned>(err),
                    reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                    reinterpret_cast<const char*>(glGetString(GL_VERSION))
                );
            }
        }
#endif
    }

    void Begin() {
#ifdef AE_GPU_TIMER_ENABLED
        glQueryCounter(startQ[write], GL_TIMESTAMP);
#endif
    }

    void End() {
#ifdef AE_GPU_TIMER_ENABLED
        glQueryCounter(endQ[write], GL_TIMESTAMP);

        // Read back the other buffer (previous frame) without stalling.
        int read = 1 - write;
        if (valid) {
            GLint avail = 0;
            glGetQueryObjectiv(endQ[read], GL_QUERY_RESULT_AVAILABLE, &avail);
            if (avail) {
                GLuint64 t0 = 0, t1 = 0;
                glGetQueryObjectui64v(startQ[read], GL_QUERY_RESULT, &t0);
                glGetQueryObjectui64v(endQ[read],   GL_QUERY_RESULT, &t1);
                ms = static_cast<float>(t1 - t0) / 1e6f;
                pendingFrames = 0;

                // Ground-truth proof the pipeline is actually producing readings,
                // so a stuck-at-zero panel can be told apart from "never logged".
                static bool loggedFirstResult = false;
                if (!loggedFirstResult) {
                    loggedFirstResult = true;
                    ENGINE_LOG(
                        "GpuTimer: first result -- t0={} t1={} delta={}ns ({:.3f}ms)",
                        t0, t1, t1 - t0, ms
                    );
                }
            } else if (++pendingFrames == 120) {
                // ~2s at 60fps with nothing ever becoming available — the driver is
                // probably not actually completing GL_TIMESTAMP queries.
                static bool loggedStall = false;
                if (!loggedStall) {
                    loggedStall = true;
                    ENGINE_LOG(
                        "GpuTimer: GL_QUERY_RESULT_AVAILABLE never became true after {} frames on '{}' / GL {} -- "
                        "driver may not support GL_TIMESTAMP queries despite reporting GL_ARB_timer_query.",
                        pendingFrames,
                        reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                        reinterpret_cast<const char*>(glGetString(GL_VERSION))
                    );
                }
            }
        }
        valid = true;
        write = read;
#endif
    }

    void Destroy() {
#ifdef AE_GPU_TIMER_ENABLED
        if (startQ[0]) glDeleteQueries(2, startQ);
        if (endQ[0])   glDeleteQueries(2, endQ);
        std::memset(startQ, 0, sizeof(startQ));
        std::memset(endQ,   0, sizeof(endQ));
        valid = false;
#endif
    }

    float GetMs() const { return ms; }
};
