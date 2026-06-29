#pragma once
#include "globals.hpp"
#include <cstring>

// GL_TIMESTAMP / glQueryCounter require OpenGL ≥ 3.3 (GL_ARB_timer_query,
// promoted to core in 3.3). Atmospheric targets GL 4.1+ on desktop so this
// is always available there.
//
// Disabled on targets that lack support:
//   - WebGL 1/2 (__EMSCRIPTEN__): no GL_TIMESTAMP; EXT_disjoint_timer_query
//     exists but has unreliable disjoint semantics — not worth handling.
//   - Android GLES (__ANDROID__): extension not guaranteed.
//   - iOS GLES (TARGET_OS_IOS): same.
// On these platforms all GpuTimer methods are inlined no-ops and GetMs()
// always returns 0.
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && \
    !(defined(__APPLE__) && TARGET_OS_IOS)
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
        ms    = o.ms;     o.ms   = 0.0f;
        return *this;
    }
#endif

    float ms = 0.0f;

    void Init() {
#ifdef AE_GPU_TIMER_ENABLED
        glGenQueries(2, startQ);
        glGenQueries(2, endQ);
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
