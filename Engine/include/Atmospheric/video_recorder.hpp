#pragma once

#include "imgui.h"
#include "renderer.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class AudioSubsystem;

// Records rendered frames to an AV1 video file via FFmpeg, optionally muxing
// an AAC audio track from caller-supplied PCM.
// Requires AE_HAS_FFMPEG (set by CMake when FFmpeg is found).
//
// Video-only usage:
//   VideoRecorder rec;
//   rec.startRecording(renderer, {.outputPath = "output.mp4", .fps = 60});
//   // each frame:
//   rec.captureFrame();
//   // when done:
//   rec.stopRecording();
//
// With audio:
//   VideoRecorder::Config cfg;
//   cfg.outputPath      = "output.mp4";
//   cfg.captureAudio    = true;
//   cfg.audioSampleRate = 44100;
//   cfg.audioChannels   = 2;
//   rec.startRecording(&renderer, cfg);
//   // each frame:
//   rec.captureFrame();
//   // on your audio thread / wherever you have the final mixed PCM:
//   rec.writeAudio(floatPcmBuffer, frameCount); // interleaved float32
//   // when done:
//   rec.stopRecording();
class VideoRecorder {
public:
    enum class CaptureResult {
        Captured,// frame queued for encoding
        Dropped,// encoder queue full; frame silently dropped to preserve FPS
    };

    struct Config {
        std::string outputPath = "recording.mp4";
        int fps = 30;
        // CRF quality for SW AV1 encoders (0=lossless, 63=worst). Lower = better.
        // Ignored for HW encoders (VideoToolbox, NVENC), which use fixed quality.
        int crf = 35;
        // Encoder probe order: h264_videotoolbox → h264_nvenc → libsvtav1 →
        // libaom-av1 → libx264. Set to empty string to use the probe order.
        std::string encoder = "h264_videotoolbox";
        // Enable AAC audio track. Caller must supply PCM via writeAudio().
        // atmospheric's AudioSubsystem wraps raudio which has no PCM tap, so
        // audio must be fed manually from wherever the application has the
        // final mixed output (e.g. a custom raudio callback, a mixer thread).
        bool captureAudio = false;
        // AAC bitrate in bits/sec.
        int audioBitrate = 128000;
        // Must match the PCM supplied to writeAudio().
        int audioSampleRate = 44100;
        int audioChannels = 2;
    };

    VideoRecorder();
    ~VideoRecorder();

    // Start recording. Returns false if already recording or FFmpeg unavailable.
    bool startRecording(Renderer* renderer, const Config& config);
    bool startRecording(Renderer* renderer) {
        return startRecording(renderer, Config{});
    }

    // Stop recording and flush remaining frames to disk.
    void stopRecording();

    bool isRecording() const {
        return m_recording.load();
    }

    // Schedule capture of the current rendered frame. Call once per frame
    // from the render/GL thread. Uses a 3-PBO async ring buffer so the GPU
    // pipeline never stalls. Returns Dropped if the encoder queue is full.
    CaptureResult captureFrame();

    // Feed interleaved float32 PCM into the audio track.
    // frameCount is the number of multi-channel frames (NOT total samples).
    // No-op unless captureAudio was true in the Config passed to startRecording().
    void writeAudio(const float* frames, uint32_t frameCount);

    // Wire this recorder to an AudioSubsystem so startRecording/stopRecording
    // automatically attach/detach raudio's mixed processor.
    // Call once after construction (e.g. in Application::Run).
    void setAudioManager(AudioSubsystem* mgr) {
        m_audioManager = mgr;
    }

    // Set the directory where timestamped recordings are saved.
    void setBaseOutputDir(const std::string& dir) {
        m_baseOutputDir = dir;
        refreshOutputPath();
    }

    // Draw the recording section inside whatever ImGui window is currently open.
    void drawImGui(Renderer& renderer) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##recpath", m_outputBuf, sizeof(m_outputBuf));

        if (!isRecording()) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.15f, 1.0f));
            if (ImGui::Button("Start##rec", ImVec2(-1.0f, 0.0f))) {
                std::error_code ec;
                std::filesystem::create_directories(std::filesystem::path(m_outputBuf).parent_path(), ec);
                Config cfg;
                cfg.outputPath = m_outputBuf;
                cfg.captureAudio = (m_audioManager != nullptr);
                cfg.audioSampleRate = 44100;
                cfg.audioChannels = 2;
                if (startRecording(&renderer, cfg))
                    m_status.clear();
                else
                    m_status = "Failed to start (FFmpeg unavailable?)";
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Stop##rec", ImVec2(-1.0f, 0.0f))) {
                stopRecording();
                m_status = fmt::format("Saved: {}", m_outputBuf);
                refreshOutputPath();
            }
            ImGui::PopStyleColor();
            auto elapsed = std::chrono::steady_clock::now() - m_recordingStart;
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "REC  %02d:%02d",
                static_cast<int>(secs / 60),
                static_cast<int>(secs % 60)
            );
        }
        if (!m_status.empty()) ImGui::TextDisabled("%s", m_status.c_str());
    }

private:
    struct RawFrame {
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        double timestamp;// seconds since recording started (wall-clock)
        bool bottomUp = false;// true when coming from PBO; encodeFrame uses negative stride
    };

    struct FFmpegContext;

    bool initEncoder(uint32_t width, uint32_t height);
    bool encodeFrame(const RawFrame& frame);
    void flushEncoder();
    void cleanup();
    void encoderThreadFunc();

    bool initAudioEncoder();
    void drainAudio(bool flush);
    void encodeAudioFrame(int nbSamples);
    void flushAudioEncoder();

    std::string m_baseOutputDir = "output";
    char m_outputBuf[256] = {};
    std::string m_status;

    void refreshOutputPath() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char filename[64];
        std::strftime(filename, sizeof(filename), "recording_%Y%m%d_%H%M%S.mp4", &tm);
        auto path = (std::filesystem::path(m_baseOutputDir) / filename).string();
        strncpy(m_outputBuf, path.c_str(), sizeof(m_outputBuf) - 1);
        m_outputBuf[sizeof(m_outputBuf) - 1] = '\0';
    }

    Renderer* m_renderer = nullptr;
    Config m_config;
    std::chrono::steady_clock::time_point m_recordingStart;

    std::unique_ptr<FFmpegContext> m_ffmpeg;

    std::queue<RawFrame> m_frameQueue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_encoderThread;

    std::atomic<bool> m_recording{ false };
    std::atomic<bool> m_stop{ false };

    static constexpr size_t MAX_QUEUE_FRAMES = 16;

    AudioSubsystem* m_audioManager = nullptr;

    // ── Audio capture state ──────────────────────────────────────────────────
    bool m_audioActive = false;// true when this session records an audio track

    // Interleaved float32 PCM from writeAudio(), consumed by the encoder thread.
    // Bounded to ~5 s to prevent unbounded growth if the encoder falls behind.
    std::deque<float> m_audioQueue;
    std::mutex m_audioMutex;
};
