#include "video_player.hpp"
#include "Atmospheric/file_system.hpp"
#include "log.hpp"
#include <fmt/format.h>

#ifdef AE_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include "raudio.h"
#endif

#include <deque>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

extern "C" {
    void js_video_open(uintptr_t playerId, const char* pathStr);
    void js_video_close(uintptr_t playerId);
    void js_video_play(uintptr_t playerId);
    void js_video_pause(uintptr_t playerId);
    void js_video_get_info(uintptr_t playerId, double* outInfo);
    int js_video_grab_frame(uintptr_t playerId, uint8_t* outPixels);
}

// clang-format off
// NOLINTBEGIN
EM_JS(void, js_video_open, (uintptr_t playerId, const char* pathStr), {
    var path = UTF8ToString(pathStr);
    window.videoPlayers = window.videoPlayers || {};

    var video = document.createElement('video');
    video.src = path;
    if (path.indexOf('://') !== -1 || path.indexOf('//') === 0) {
        video.crossOrigin = "anonymous";
    }
    video.muted = false;// Enable audio playback
    video.playsInline = true;
    video.load();

    var canvas = document.createElement('canvas');
    var ctx = canvas.getContext('2d');

    window.videoPlayers[playerId] = { video : video, canvas : canvas, ctx : ctx, lastTime : -1, width : 0, height : 0 };
});
// NOLINTEND
// clang-format on

// clang-format off
// NOLINTBEGIN
EM_JS(void, js_video_close, (uintptr_t playerId), {
    if (window.videoPlayers && window.videoPlayers[playerId]) {
        var player = window.videoPlayers[playerId];
        player.video.pause();
        player.video.src = "";
        delete window.videoPlayers[playerId];
    }
});
// NOLINTEND
// clang-format on

// clang-format off
// NOLINTBEGIN
EM_JS(void, js_video_play, (uintptr_t playerId), {
    if (window.videoPlayers && window.videoPlayers[playerId]) {
        var video = window.videoPlayers[playerId].video;
        video.play().catch(function(e) {
            console.warn("Video autoplay with audio blocked. Registering interaction listeners to start audio.");
            var startVideo = function() {
                video.play().catch(function(err){});
                document.removeEventListener('click', startVideo);
                document.removeEventListener('touchend', startVideo);
                document.removeEventListener('keydown', startVideo);
            };
            document.addEventListener('click', startVideo);
            document.addEventListener('touchend', startVideo);
            document.addEventListener('keydown', startVideo);
        });
    }
});
// NOLINTEND
// clang-format on

// clang-format off
// NOLINTBEGIN
EM_JS(void, js_video_pause, (uintptr_t playerId), {
    if (window.videoPlayers && window.videoPlayers[playerId]) {
        window.videoPlayers[playerId].video.pause();
    }
});
// NOLINTEND
// clang-format on

// clang-format off
// NOLINTBEGIN
EM_JS(void, js_video_get_info, (uintptr_t playerId, double* outInfo), {
    if (!window.videoPlayers || !window.videoPlayers[playerId]) return;
    var video = window.videoPlayers[playerId].video;
    setValue(outInfo, video.videoWidth, 'double');
    setValue(outInfo + 8, video.videoHeight, 'double');
    setValue(outInfo + 16, video.duration || 0, 'double');
    setValue(outInfo + 24, video.currentTime, 'double');
    setValue(outInfo + 32, video.ended ? 1 : 0, 'double');
});
// NOLINTEND
// clang-format on

// clang-format off
// NOLINTBEGIN
EM_JS(int, js_video_grab_frame, (uintptr_t playerId, uint8_t* outPixels), {
    if (!window.videoPlayers || !window.videoPlayers[playerId]) return 0;
    var player = window.videoPlayers[playerId];
    var video = player.video;
    if (video.readyState < 2) return 0;
    if (video.currentTime === player.lastTime) return 0;

    var w = video.videoWidth;
    var h = video.videoHeight;
    if (w <= 0 || h <= 0) return 0;

    if (player.canvas.width !== w || player.canvas.height !== h) {
        player.canvas.width = w;
        player.canvas.height = h;
    }

    try {
        player.ctx.drawImage(video, 0, 0, w, h);
        var imgData = player.ctx.drawImage ? player.ctx.getImageData(0, 0, w, h) : null;
        if (imgData) {
            HEAPU8.set(imgData.data, outPixels);
        }
    } catch (e) {
        if (!player.hasTaintError) {
            console.error("Failed to grab video frame (CORS/security issue?):", e);
            player.hasTaintError = true;
        }
        return 0;
    }
    player.lastTime = video.currentTime;
    return 1;
});
// NOLINTEND
// clang-format on
#endif

// ─── FFmpegDecodeContext ───────────────────────────────────────────────────────────────────────────────────────

struct VideoPlayer::FFmpegDecodeContext {
#ifdef AE_HAS_FFMPEG
    // Video
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    SwsContext* swsCtx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* hwFrame = nullptr;// CPU staging frame for HW transfer
    AVPacket* packet = nullptr;
    int videoStreamIdx = -1;
    double timeBase = 0.0;

    // Hardware acceleration
    AVBufferRef* hwDeviceCtx = nullptr;
    AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;// HW pixel format for this session

    // Audio decode
    AVCodecContext* audioCodecCtx = nullptr;
    SwrContext* audioSwrCtx = nullptr;
    AVFrame* audioFrame = nullptr;
    int audioStreamIdx = -1;
    int audioSampleRate = 0;
    int audioChannels = 0;

    // Audio playback (raudio)
    AudioStream audioStream = {};
    bool audioReady = false;

    // PCM queue shared between decode thread and main thread
    std::deque<int16_t> audioPCM;
    std::mutex audioMutex;
#endif
};

// ─── Lifecycle ────────────────────────────────────────────────────────────────────────────────────

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() {
    close();
}

bool VideoPlayer::open(const std::string& path) {
#ifdef __EMSCRIPTEN__
    close();
    std::string resolvedPath = path;
    if (path.find("://") == std::string::npos) {
        resolvedPath = FileSystem::Get().ResolvePath(path).value_or(path);
    }
    js_video_open(reinterpret_cast<uintptr_t>(this), resolvedPath.c_str());
    _currentTime = 0.0;
    _hasCurrentFrame = false;
    _finished = false;
    _open = true;
    return true;
#else
#ifndef AE_HAS_FFMPEG
    Log::Error("[VideoPlayer] Built without FFmpeg support");
    return false;
#else
    close();

    std::string resolvedPath = path;
    if (path.find("://") == std::string::npos) {
        resolvedPath = FileSystem::Get().ResolvePath(path).value_or(path);
    }

    _ffmpeg = std::make_unique<FFmpegDecodeContext>();
    if (!initDecoder(resolvedPath)) {
        cleanup();
        return false;
    }

    _currentTime = 0.0;
    _hasCurrentFrame = false;
    _stop = false;
    _finished = false;
    _open = true;

    _decodeThread = std::thread(&VideoPlayer::decodeThreadFunc, this);
    return true;
#endif
#endif
}

void VideoPlayer::close() {
    if (!_open) {
        return;
    }

#ifdef __EMSCRIPTEN__
    js_video_close(reinterpret_cast<uintptr_t>(this));
    _open = false;
    _playing = false;
    _finished = false;
    _currentTime = 0.0;
    _duration = 0.0;
    _hasCurrentFrame = false;
    _frameQueue.clear();
    return;
#endif

    _stop = true;
    _cv.notify_all();

    if (_decodeThread.joinable()) {
        _decodeThread.join();
    }

#ifdef AE_HAS_FFMPEG
    if (_ffmpeg && _ffmpeg->audioReady) {
        StopAudioStream(_ffmpeg->audioStream);
        UnloadAudioStream(_ffmpeg->audioStream);
        _ffmpeg->audioReady = false;
    }
    if (_ffmpeg) {
        std::lock_guard<std::mutex> al(_ffmpeg->audioMutex);
        _ffmpeg->audioPCM.clear();
    }
#endif

    cleanup();

    _open = false;
    _playing = false;
    _finished = false;
    _currentTime = 0.0;
    _duration = 0.0;
    _hasCurrentFrame = false;

    std::lock_guard<std::mutex> lock(_mutex);
    _frameQueue.clear();
}

void VideoPlayer::play() {
    _playing = true;
#ifdef __EMSCRIPTEN__
    js_video_play(reinterpret_cast<uintptr_t>(this));
    return;
#endif
#ifdef AE_HAS_FFMPEG
    if (_ffmpeg && _ffmpeg->audioReady) {
        PlayAudioStream(_ffmpeg->audioStream);
    }
#endif
}

void VideoPlayer::pause() {
    _playing = false;
#ifdef __EMSCRIPTEN__
    js_video_pause(reinterpret_cast<uintptr_t>(this));
    return;
#endif
#ifdef AE_HAS_FFMPEG
    if (_ffmpeg && _ffmpeg->audioReady) {
        PauseAudioStream(_ffmpeg->audioStream);
    }
#endif
}

// ─── Main-thread update ────────────────────────────────────────────────────────────────────────────

bool VideoPlayer::update(double deltaTime) {
    if (!_open || !_playing) {
        return false;
    }

#ifdef __EMSCRIPTEN__
    double info[5] = { 0 };
    js_video_get_info(reinterpret_cast<uintptr_t>(this), info);

    uint32_t w = static_cast<uint32_t>(info[0]);
    uint32_t h = static_cast<uint32_t>(info[1]);
    _duration = info[2];
    _currentTime = info[3];
    _finished = (info[4] > 0.5);

    if (_finished) {
        _playing = false;
        return false;
    }

    if (w <= 0 || h <= 0) {
        return false;
    }

    if (_currentFrame.width != w || _currentFrame.height != h) {
        _currentFrame.pixels.resize(w * h * 4);
        _currentFrame.width = w;
        _currentFrame.height = h;
    }

    int updated = js_video_grab_frame(reinterpret_cast<uintptr_t>(this), _currentFrame.pixels.data());
    if (updated) {
        _hasCurrentFrame = true;
        _currentFrame.pts = _currentTime;
        return true;
    }
    return false;
#else
#ifdef AE_HAS_FFMPEG
    if (_ffmpeg && _ffmpeg->audioReady) {
        unsigned int played = GetAudioStreamFramesPlayed(_ffmpeg->audioStream);
        _currentTime = static_cast<double>(played) / _ffmpeg->audioSampleRate;
    } else {
        _currentTime += deltaTime;
    }
#else
    _currentTime += deltaTime;
#endif

    bool frameChanged = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);

        while (!_frameQueue.empty() && _frameQueue.front().pts <= _currentTime) {
            _currentFrame = std::move(_frameQueue.front());
            _frameQueue.pop_front();
            _hasCurrentFrame = true;
            frameChanged = true;
        }
    }

    _cv.notify_one();

#ifdef AE_HAS_FFMPEG
    if (_ffmpeg && _ffmpeg->audioReady) {
        if (IsAudioStreamProcessed(_ffmpeg->audioStream)) {
            std::lock_guard<std::mutex> al(_ffmpeg->audioMutex);
            static constexpr int gchunkFrames = 4096;
            int ch = _ffmpeg->audioChannels;
            int available = static_cast<int>(_ffmpeg->audioPCM.size()) / ch;
            int toSend = std::min(available, gchunkFrames);
            if (toSend > 0) {
                std::vector<int16_t> buf(static_cast<size_t>(toSend) * ch, 0);
                for (int i = 0; i < toSend * ch; ++i) {
                    buf[i] = _ffmpeg->audioPCM.front();
                    _ffmpeg->audioPCM.pop_front();
                }
                UpdateAudioStream(_ffmpeg->audioStream, buf.data(), toSend);
            }
        }
    }
#endif

    if (_finished && frameChanged) {
        bool queueEmpty;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            queueEmpty = _frameQueue.empty();
        }
        if (queueEmpty) {
            _playing = false;
        }
    }

    return frameChanged;
#endif
}

const VideoPlayer::Frame* VideoPlayer::getCurrentFrame() const {
    return _hasCurrentFrame ? &_currentFrame : nullptr;
}

// ─── Decode thread ────────────────────────────────────────────────────────────────────────────────

void VideoPlayer::decodeThreadFunc() {
#ifdef AE_HAS_FFMPEG
    auto& ff = *_ffmpeg;

    // Helper: decode one audio packet's frames and push PCM into the queue.
    auto enqueueAudio = [&](AVFrame* af) {
        int outSamples = swr_get_out_samples(ff.audioSwrCtx, af->nb_samples);
        if (outSamples <= 0) {
            return;
        }

        std::vector<int16_t> pcm(static_cast<size_t>(outSamples) * ff.audioChannels);
        uint8_t* outPlanes[1] = { reinterpret_cast<uint8_t*>(pcm.data()) };
        int converted =
            swr_convert(ff.audioSwrCtx, outPlanes, outSamples, const_cast<const uint8_t**>(af->data), af->nb_samples);
        if (converted <= 0) {
            return;
        }
        pcm.resize(static_cast<size_t>(converted) * ff.audioChannels);

        std::lock_guard<std::mutex> al(ff.audioMutex);
        // Cap at ~5 seconds of audio to prevent unbounded growth.
        static constexpr size_t gmaxPcm = 48000 * 2 * 5;
        if (ff.audioPCM.size() + pcm.size() <= gmaxPcm) {
            ff.audioPCM.insert(ff.audioPCM.end(), pcm.begin(), pcm.end());
        }
    };

    while (!_stop) {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [this] { return _frameQueue.size() < MAX_BUFFERED_FRAMES || _stop.load(); });
        }
        if (_stop) {
            break;
        }

        int ret = av_read_frame(ff.fmtCtx, ff.packet);
        if (ret == AVERROR_EOF) {
            // Flush video decoder.
            avcodec_send_packet(ff.codecCtx, nullptr);
            while (avcodec_receive_frame(ff.codecCtx, ff.frame) >= 0) {
                convertAndEnqueue(ff.frame);
            }
            // Flush audio decoder.
            if (ff.audioStreamIdx >= 0 && ff.audioCodecCtx) {
                avcodec_send_packet(ff.audioCodecCtx, nullptr);
                while (avcodec_receive_frame(ff.audioCodecCtx, ff.audioFrame) >= 0) {
                    enqueueAudio(ff.audioFrame);
                }
            }
            _finished = true;
            break;
        }
        if (ret < 0) {
            break;
        }

        if (ff.packet->stream_index == ff.videoStreamIdx) {
            avcodec_send_packet(ff.codecCtx, ff.packet);
            av_packet_unref(ff.packet);
            while (avcodec_receive_frame(ff.codecCtx, ff.frame) >= 0) {
                if (!convertAndEnqueue(ff.frame)) {
                    break;
                }
            }
        } else if (ff.audioStreamIdx >= 0 && ff.packet->stream_index == ff.audioStreamIdx && ff.audioCodecCtx) {
            avcodec_send_packet(ff.audioCodecCtx, ff.packet);
            av_packet_unref(ff.packet);
            while (avcodec_receive_frame(ff.audioCodecCtx, ff.audioFrame) >= 0) {
                enqueueAudio(ff.audioFrame);
            }
        } else {
            av_packet_unref(ff.packet);
        }
    }
#endif
}

// ─── FFmpeg helpers ───────────────────────────────────────────────────────────────────────────────────

bool VideoPlayer::initDecoder(const std::string& path) {
#ifndef AE_HAS_FFMPEG
    return false;
#else
    auto& ff = *_ffmpeg;

    if (avformat_open_input(&ff.fmtCtx, path.c_str(), nullptr, nullptr) < 0) {
        Log::Error("[VideoPlayer] Cannot open '{}'", path);
        return false;
    }
    if (avformat_find_stream_info(ff.fmtCtx, nullptr) < 0) {
        Log::Error("[VideoPlayer] Cannot read stream info from '{}'", path);
        return false;
    }

    // ── Video stream ────────────────────────────────────────────────────────────────────
    const AVCodec* codec = nullptr;
    ff.videoStreamIdx = av_find_best_stream(ff.fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (ff.videoStreamIdx < 0 || !codec) {
        Log::Error("[VideoPlayer] No video stream found in '{}'", path);
        return false;
    }

    AVStream* vstream = ff.fmtCtx->streams[ff.videoStreamIdx];
    ff.timeBase = av_q2d(vstream->time_base);

    if (ff.fmtCtx->duration != AV_NOPTS_VALUE) {
        _duration = static_cast<double>(ff.fmtCtx->duration) / AV_TIME_BASE;
    }

    ff.codecCtx = avcodec_alloc_context3(codec);
    if (!ff.codecCtx) {
        return false;
    }
    avcodec_parameters_to_context(ff.codecCtx, vstream->codecpar);

    // ── Hardware acceleration probe ──────────────────────────────────────────────────
    // Try each HW type in priority order; first success wins.
    // Falls through to pure software if nothing works.
    static constexpr AVHWDeviceType gkHwPriority[] = {
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX,// macOS
        AV_HWDEVICE_TYPE_CUDA,// NVIDIA
        AV_HWDEVICE_TYPE_VAAPI,// Linux / Intel
        AV_HWDEVICE_TYPE_D3D11VA,// Windows
        AV_HWDEVICE_TYPE_NONE,
    };
    for (const AVHWDeviceType hwType : gkHwPriority) {
        if (hwType == AV_HWDEVICE_TYPE_NONE) {
            break;
        }

        // Does the selected software codec advertise support for this HW type?
        AVPixelFormat hwPix = AV_PIX_FMT_NONE;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
            if (!cfg) {
                break;
            }
            if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && cfg->device_type == hwType) {
                hwPix = cfg->pix_fmt;
                break;
            }
        }
        if (hwPix == AV_PIX_FMT_NONE) {
            continue;
        }

        if (av_hwdevice_ctx_create(&ff.hwDeviceCtx, hwType, nullptr, nullptr, 0) < 0) {
            continue;// this HW type isn't available on this machine
        }

        ff.hwPixFmt = hwPix;
        ff.codecCtx->hw_device_ctx = av_buffer_ref(ff.hwDeviceCtx);

        // Captureless lambda — convertible to C function pointer.
        ff.codecCtx->opaque = &ff.hwPixFmt;
        ff.codecCtx->get_format = [](AVCodecContext* ctx, const AVPixelFormat* fmts) -> AVPixelFormat {
            const auto target = *static_cast<AVPixelFormat*>(ctx->opaque);
            for (const AVPixelFormat* f = fmts; *f != AV_PIX_FMT_NONE; ++f) {
                if (*f == target) {
                    return *f;
                }
            }
            return fmts[0];// codec didn't offer HW fmt — accept software fallback
        };

        Log::Info("[VideoPlayer] HW decode: {} ({})", av_hwdevice_get_type_name(hwType), av_get_pix_fmt_name(hwPix));
        break;
    }

    if (avcodec_open2(ff.codecCtx, codec, nullptr) < 0) {
        Log::Error("[VideoPlayer] Failed to open video decoder for '{}'", path);
        return false;
    }

    int w = ff.codecCtx->width;
    int h = ff.codecCtx->height;

    ff.frame = av_frame_alloc();
    ff.hwFrame = av_frame_alloc();// CPU staging frame for HW→CPU transfer
    ff.packet = av_packet_alloc();

    // swsCtx is created lazily in convertAndEnqueue() once we know the actual
    // pixel format — HW decoders report the real CPU format only after transfer.

    // ── Audio stream (optional) ────────────────────────────────────────────────────────
    const AVCodec* acodec = nullptr;
    int aIdx = av_find_best_stream(ff.fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &acodec, 0);
    if (aIdx >= 0 && acodec) {
        AVStream* astream = ff.fmtCtx->streams[aIdx];

        ff.audioCodecCtx = avcodec_alloc_context3(acodec);
        if (ff.audioCodecCtx) {
            avcodec_parameters_to_context(ff.audioCodecCtx, astream->codecpar);
            if (avcodec_open2(ff.audioCodecCtx, acodec, nullptr) >= 0) {
                ff.audioStreamIdx = aIdx;
                ff.audioSampleRate = ff.audioCodecCtx->sample_rate;
                ff.audioChannels = std::min(ff.audioCodecCtx->ch_layout.nb_channels, 2);
                ff.audioFrame = av_frame_alloc();

                // Convert any input audio format → S16 interleaved stereo.
                AVChannelLayout outLayout{};
                av_channel_layout_default(&outLayout, ff.audioChannels);

                int swrRet = swr_alloc_set_opts2(
                    &ff.audioSwrCtx,
                    &outLayout,
                    AV_SAMPLE_FMT_S16,
                    ff.audioSampleRate,
                    &ff.audioCodecCtx->ch_layout,
                    ff.audioCodecCtx->sample_fmt,
                    ff.audioCodecCtx->sample_rate,
                    0,
                    nullptr
                );

                av_channel_layout_uninit(&outLayout);

                if (swrRet >= 0 && swr_init(ff.audioSwrCtx) >= 0) {
                    // Ensure the sub-buffer is exactly CHUNK_FRAMES large so
                    // UpdateAudioStream never rejects our writes.
                    SetAudioStreamBufferSizeDefault(4096);
                    ff.audioStream = LoadAudioStream(
                        static_cast<unsigned int>(ff.audioSampleRate), 16, static_cast<unsigned int>(ff.audioChannels)
                    );
                    ff.audioReady = true;
                    Log::Info(
                        "[VideoPlayer] Audio: {} Hz, {} ch ({})", ff.audioSampleRate, ff.audioChannels, acodec->name
                    );
                } else {
                    Log::Error("[VideoPlayer] swr init failed; audio disabled");
                    if (ff.audioSwrCtx) {
                        swr_free(&ff.audioSwrCtx);
                    }
                    avcodec_free_context(&ff.audioCodecCtx);
                    av_frame_free(&ff.audioFrame);
                    ff.audioStreamIdx = -1;
                }
            } else {
                avcodec_free_context(&ff.audioCodecCtx);
            }
        }
    }

    Log::Info("[VideoPlayer] Opened '{}' — {}x{} {:.1f}s ({})", path, w, h, _duration, codec->name);
    return true;
#endif
}

bool VideoPlayer::convertAndEnqueue(void* avframePtr) {
#ifndef AE_HAS_FFMPEG
    return false;
#else
    auto& ff = *_ffmpeg;
    auto* avframe = static_cast<AVFrame*>(avframePtr);

    // If the frame is in hardware memory, transfer it to a CPU frame first.
    AVFrame* swFrame = avframe;
    if (ff.hwPixFmt != AV_PIX_FMT_NONE && avframe->format == static_cast<int>(ff.hwPixFmt)) {
        av_frame_unref(ff.hwFrame);
        if (av_hwframe_transfer_data(ff.hwFrame, avframe, 0) < 0) {
            return true;// skip this frame rather than hard-failing
        }
        ff.hwFrame->pts = avframe->pts;
        swFrame = ff.hwFrame;
    }

    int w = swFrame->width;
    int h = swFrame->height;

    // Lazy swsCtx init: pixel format is unknown until after the first HW transfer.
    if (!ff.swsCtx) {
        ff.swsCtx = sws_getContext(
            w,
            h,
            static_cast<AVPixelFormat>(swFrame->format),
            w,
            h,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr
        );
        if (!ff.swsCtx) {
            Log::Error("[VideoPlayer] Failed to create colour converter");
            return false;
        }
    }

    Frame decoded;
    decoded.pixels.resize(static_cast<size_t>(w) * h * 4);
    decoded.width = static_cast<uint32_t>(w);
    decoded.height = static_cast<uint32_t>(h);
    decoded.pts = (swFrame->pts != AV_NOPTS_VALUE) ? static_cast<double>(swFrame->pts) * ff.timeBase : 0.0;

    uint8_t* dstPlanes[1] = { decoded.pixels.data() };
    int dstStrides[1] = { w * 4 };
    sws_scale(ff.swsCtx, swFrame->data, swFrame->linesize, 0, h, dstPlanes, dstStrides);

    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [this] { return _frameQueue.size() < MAX_BUFFERED_FRAMES || _stop.load(); });
        if (_stop) {
            return false;
        }
        _frameQueue.push_back(std::move(decoded));
    }
    return true;
#endif
}

void VideoPlayer::cleanup() {
#ifdef AE_HAS_FFMPEG
    if (!_ffmpeg) {
        return;
    }
    auto& ff = *_ffmpeg;

    // Audio
    if (ff.audioSwrCtx) {
        swr_free(&ff.audioSwrCtx);
    }
    if (ff.audioFrame) {
        av_frame_free(&ff.audioFrame);
    }
    if (ff.audioCodecCtx) {
        avcodec_free_context(&ff.audioCodecCtx);
    }

    // Video
    if (ff.swsCtx) {
        sws_freeContext(ff.swsCtx);
        ff.swsCtx = nullptr;
    }
    if (ff.hwFrame) {
        av_frame_free(&ff.hwFrame);
    }
    if (ff.frame) {
        av_frame_free(&ff.frame);
    }
    if (ff.packet) {
        av_packet_free(&ff.packet);
    }
    if (ff.codecCtx) {
        avcodec_free_context(&ff.codecCtx);
    }
    if (ff.hwDeviceCtx) {
        av_buffer_unref(&ff.hwDeviceCtx);
    }
    if (ff.fmtCtx) {
        avformat_close_input(&ff.fmtCtx);
    }

    _ffmpeg.reset();
#endif
}
