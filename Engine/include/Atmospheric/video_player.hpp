#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Decodes an AV1 (or any FFmpeg-supported) video file for use as cutscene
// material.  Frame data is delivered as RGBA pixels — the caller is
// responsible for uploading to the GPU each time update() returns true.
//
// Requires AE_HAS_FFMPEG (set by CMake when FFmpeg is found).
//
// Typical usage:
//   VideoPlayer player;
//   player.open("cutscene.mkv");
//   player.play();
//
//   GLuint texture = 0;
//   // game loop:
//   if (player.update(deltaTime)) {
//       const auto* f = player.getCurrentFrame();
//       if (!texture) {
//           glGenTextures(1, &texture);
//           glBindTexture(GL_TEXTURE_2D, texture);
//           glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, f->width, f->height,
//                        0, GL_RGBA, GL_UNSIGNED_BYTE, f->pixels.data());
//       } else {
//           glBindTexture(GL_TEXTURE_2D, texture);
//           glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height,
//                           GL_RGBA, GL_UNSIGNED_BYTE, f->pixels.data());
//       }
//   }
//   // draw with texture …
class VideoPlayer {
public:
    struct Frame {
        std::vector<uint8_t> pixels;// RGBA, width*height*4 bytes
        uint32_t width = 0;
        uint32_t height = 0;
        double pts = 0.0;// presentation time in seconds
    };

    VideoPlayer();
    ~VideoPlayer();

    // Open a video file and prepare the decoder. Does not start playback.
    bool open(const std::string& path);
    void close();

    void play();
    void pause();

    // Advance playback clock by deltaTime seconds.
    // Returns true if the current frame changed (caller should re-upload texture).
    bool update(double deltaTime);

    // Returns the latest frame ready for display, or nullptr before the first frame.
    const Frame* getCurrentFrame() const;

    bool isOpen() const {
        return m_open.load();
    }
    bool isPlaying() const {
        return m_playing.load();
    }
    bool isFinished() const {
        return m_finished.load();
    }

    double getDuration() const {
        return m_duration;
    }
    double getCurrentTime() const {
        return m_currentTime;
    }

private:
    struct FFmpegDecodeContext;

    void decodeThreadFunc();
    bool initDecoder(const std::string& path);
    void cleanup();
    bool convertAndEnqueue(void* avframe);

    std::unique_ptr<FFmpegDecodeContext> m_ffmpeg;

    std::deque<Frame> m_frameQueue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_decodeThread;

    std::atomic<bool> m_open{ false };
    std::atomic<bool> m_playing{ false };
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_finished{ false };

    double m_currentTime = 0.0;
    double m_duration = 0.0;

    Frame m_currentFrame;
    bool m_hasCurrentFrame = false;

    static constexpr size_t MAX_BUFFERED_FRAMES = 4;
};
