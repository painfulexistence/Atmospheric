#include "video_recorder.hpp"
#include "audio_manager.hpp"
#include <fmt/format.h>

#ifdef AE_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

// ─── FFmpegContext ────────────────────────────────────────────────────────────────────────────────

struct VideoRecorder::FFmpegContext {
#ifdef AE_HAS_FFMPEG
    // Video
    AVCodecContext*  codecCtx = nullptr;
    AVFormatContext* fmtCtx   = nullptr;
    AVStream*        stream   = nullptr;
    AVPacket*        packet   = nullptr;
    AVFrame*         frame    = nullptr;
    SwsContext*      swsCtx   = nullptr;
    // Audio
    AVCodecContext*  audioCodecCtx = nullptr;
    AVStream*        audioStream   = nullptr;
    AVPacket*        audioPacket   = nullptr;
    AVFrame*         audioFrame    = nullptr;
    SwrContext*      swrCtx        = nullptr;
    AVAudioFifo*     audioFifo     = nullptr;
    int64_t          audioNextPts  = 0; // cumulative samples (per channel)
#endif
};

// ─── Lifecycle ──────────────────────────────────────────────────────────────────────────────────

VideoRecorder::VideoRecorder() = default;

VideoRecorder::~VideoRecorder() {
    if (m_recording) {
        stopRecording();
    }
}

bool VideoRecorder::startRecording(Renderer* renderer, const Config& config) {
#ifndef AE_HAS_FFMPEG
    fmt::print(stderr, "[VideoRecorder] Built without FFmpeg support — recording unavailable\n");
    return false;
#else
    if (m_recording) {
        return false;
    }

    m_renderer       = renderer;
    m_config         = config;
    m_ffmpeg         = std::make_unique<FFmpegContext>();
    m_recordingStart = std::chrono::steady_clock::now();
    m_stop           = false;

    m_audioActive = false;
    if (m_config.captureAudio
        && m_config.audioSampleRate > 0
        && m_config.audioChannels > 0) {
        m_audioActive = true;
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioQueue.clear();
    }

#ifndef __EMSCRIPTEN__
    if (m_config.captureAudio && m_audioManager)
        m_audioManager->attachVideoRecorder(this);
#endif

    m_recording = true;

    m_encoderThread = std::thread(&VideoRecorder::encoderThreadFunc, this);
    return true;
#endif
}

void VideoRecorder::stopRecording() {
    if (!m_recording) {
        return;
    }

    m_stop = true;
    m_cv.notify_all();

#ifndef __EMSCRIPTEN__
    if (m_audioManager)
        m_audioManager->detachVideoRecorder();
#endif

    // All captureFrame() calls are done; PBO pixel data was already copied into
    // RawFrame.pixels, so it's safe to release the GL resources now.
    if (m_renderer)
        m_renderer->destroyReadbackPBOs();

    if (m_encoderThread.joinable()) {
        m_encoderThread.join();
    }

    m_recording   = false;
    m_audioActive = false;
    m_renderer    = nullptr;
}

VideoRecorder::CaptureResult VideoRecorder::captureFrame() {
    if (!m_recording) {
        return CaptureResult::Dropped;
    }

    // Phase 1: issue this frame's async DMA into the next PBO slot (non-blocking).
    m_renderer->schedulePixelReadback();

    // Phase 2: collect the PBO written 2 frames ago (nullopt while priming).
    auto imgOpt = m_renderer->collectPixelReadback();
    if (!imgOpt) {
        return CaptureResult::Dropped;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_frameQueue.size() >= MAX_QUEUE_FRAMES) {
            return CaptureResult::Dropped; // encoder lagging; pixel data discarded
        }
    }

    const double timestamp = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_recordingStart).count();

    RawFrame frame;
    frame.pixels    = std::move(imgOpt->data); // move: zero extra memcpy
    frame.width     = imgOpt->width;
    frame.height    = imgOpt->height;
    frame.timestamp = timestamp;
    frame.bottomUp  = imgOpt->bottomUp;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameQueue.push(std::move(frame));
    }
    m_cv.notify_one();
    return CaptureResult::Captured;
}

// ─── Audio capture (caller thread) ─────────────────────────────────────────────────────────

void VideoRecorder::writeAudio(const float* frames, uint32_t frameCount) {
    if (!m_audioActive || !m_recording.load() || frameCount == 0) {
        return;
    }

    const size_t incoming = static_cast<size_t>(frameCount) * m_config.audioChannels;
    // Cap at ~5 seconds to prevent unbounded growth if the encoder falls behind.
    const size_t cap = static_cast<size_t>(m_config.audioSampleRate)
                       * m_config.audioChannels * 5;

    std::lock_guard<std::mutex> lock(m_audioMutex);
    if (m_audioQueue.size() + incoming > cap) {
        size_t overflow = std::min(m_audioQueue.size() + incoming - cap,
                                   m_audioQueue.size());
        m_audioQueue.erase(m_audioQueue.begin(), m_audioQueue.begin() + overflow);
    }
    m_audioQueue.insert(m_audioQueue.end(), frames, frames + incoming);
}

// ─── Encoder thread ────────────────────────────────────────────────────────────────────────────────

void VideoRecorder::encoderThreadFunc() {
    bool encoderInitialized = false;

    while (true) {
        RawFrame frame;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_frameQueue.empty() || m_stop.load(); });

            if (m_frameQueue.empty()) {
                break;
            }

            frame = std::move(m_frameQueue.front());
            m_frameQueue.pop();
        }

        if (!encoderInitialized) {
            if (!initEncoder(frame.width, frame.height)) {
                fmt::print(stderr, "[VideoRecorder] Encoder initialization failed\n");
                m_recording = false;
                return;
            }
            encoderInitialized = true;
        }

        encodeFrame(frame);
        // Interleave audio: drain whatever PCM has accumulated since last frame.
        drainAudio(/*flush=*/false);
    }

    if (encoderInitialized) {
        drainAudio(/*flush=*/true); // resample + encode remaining audio
        flushAudioEncoder();
        flushEncoder();
    }
    cleanup();
}

// ─── FFmpeg video implementation ────────────────────────────────────────────────────────────────

bool VideoRecorder::initEncoder(uint32_t width, uint32_t height) {
#ifndef AE_HAS_FFMPEG
    return false;
#else
    auto& ff = *m_ffmpeg;

    // AV1 requires even-numbered dimensions; crop one pixel if needed.
    width  &= ~1u;
    height &= ~1u;
    if (width == 0 || height == 0) {
        fmt::print(stderr, "[VideoRecorder] Frame dimensions too small after alignment\n");
        return false;
    }

    // Probe order: HW encoders first (lowest CPU overhead), then SW fallbacks.
    static constexpr const char* kEncoderCandidates[] = {
        "h264_videotoolbox", // macOS VideoToolbox (always available on Mac)
        "h264_nvenc",        // NVIDIA GPU
        "libsvtav1",         // SW AV1, fastest
        "libaom-av1",        // SW AV1
        "librav1e",          // SW AV1
        "libx264",           // SW H.264, widest compatibility
        nullptr
    };
    const AVCodec* codec = nullptr;
    std::string usedEncoder;
    // Try configured encoder first, then walk the candidate list.
    if (!m_config.encoder.empty()) {
        codec = avcodec_find_encoder_by_name(m_config.encoder.c_str());
        if (codec) usedEncoder = m_config.encoder;
    }
    if (!codec) {
        for (int i = 0; kEncoderCandidates[i]; ++i) {
            if (m_config.encoder == kEncoderCandidates[i]) continue;
            codec = avcodec_find_encoder_by_name(kEncoderCandidates[i]);
            if (codec) {
                usedEncoder = kEncoderCandidates[i];
                fmt::print("[VideoRecorder] Encoder '{}' not found, using '{}'\n",
                           m_config.encoder, usedEncoder);
                break;
            }
        }
    }
    if (!codec) {
        fmt::print(stderr,
                   "[VideoRecorder] No video encoder found (tried '{}' and all fallbacks)\n",
                   m_config.encoder);
        return false;
    }

    if (avformat_alloc_output_context2(&ff.fmtCtx, nullptr, nullptr,
                                        m_config.outputPath.c_str()) < 0) {
        fmt::print(stderr, "[VideoRecorder] Failed to create output context for '{}'\n",
                   m_config.outputPath);
        return false;
    }

    ff.codecCtx = avcodec_alloc_context3(codec);
    if (!ff.codecCtx) {
        return false;
    }

    ff.codecCtx->width     = static_cast<int>(width);
    ff.codecCtx->height    = static_cast<int>(height);
    ff.codecCtx->time_base = AVRational{1, 1'000'000};
    ff.codecCtx->framerate = AVRational{m_config.fps, 1};
    ff.codecCtx->pix_fmt   = AV_PIX_FMT_YUV420P;
    ff.codecCtx->gop_size  = m_config.fps;

    if (usedEncoder == "h264_videotoolbox") {
        ff.codecCtx->bit_rate = 8'000'000; // 8 Mbps — good quality at 30fps 1080p
    } else if (usedEncoder == "h264_nvenc") {
        av_opt_set(ff.codecCtx->priv_data, "preset", "p4", 0);   // balanced speed/quality
        av_opt_set(ff.codecCtx->priv_data, "rc", "vbr", 0);
        av_opt_set_int(ff.codecCtx->priv_data, "cq", 23, 0);
    } else if (usedEncoder == "libsvtav1") {
        av_opt_set_int(ff.codecCtx->priv_data, "crf", m_config.crf, 0);
        av_opt_set_int(ff.codecCtx->priv_data, "preset", 8, 0);
    } else if (usedEncoder == "libaom-av1") {
        av_opt_set_int(ff.codecCtx->priv_data, "crf", m_config.crf, 0);
        av_opt_set_int(ff.codecCtx->priv_data, "cpu-used", 6, 0);
        av_opt_set(ff.codecCtx->priv_data, "usage", "realtime", 0);
    } else if (usedEncoder == "librav1e") {
        av_opt_set_int(ff.codecCtx->priv_data, "crf", m_config.crf, 0);
        av_opt_set_int(ff.codecCtx->priv_data, "speed", 8, 0);
    } else if (usedEncoder == "libx264") {
        av_opt_set(ff.codecCtx->priv_data, "preset", "fast", 0);
        av_opt_set_int(ff.codecCtx->priv_data, "crf", 23, 0); // x264 scale: 0-51
    }

    if (ff.fmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        ff.codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(ff.codecCtx, codec, nullptr) < 0) {
        fmt::print(stderr, "[VideoRecorder] Failed to open codec '{}'\n", usedEncoder);
        return false;
    }

    ff.stream = avformat_new_stream(ff.fmtCtx, nullptr);
    if (!ff.stream) {
        return false;
    }
    ff.stream->time_base = ff.codecCtx->time_base;
    avcodec_parameters_from_context(ff.stream->codecpar, ff.codecCtx);

    // Audio stream must be added before avformat_write_header.
    if (m_audioActive) {
        if (!initAudioEncoder()) {
            fmt::print(stderr,
                       "[VideoRecorder] Audio encoder init failed — recording video only\n");
            m_audioActive = false;
        }
    }

    if (!(ff.fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ff.fmtCtx->pb, m_config.outputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
            fmt::print(stderr, "[VideoRecorder] Cannot open output file '{}'\n",
                       m_config.outputPath);
            return false;
        }
    }

    if (avformat_write_header(ff.fmtCtx, nullptr) < 0) {
        fmt::print(stderr, "[VideoRecorder] Failed to write container header\n");
        return false;
    }

    ff.frame         = av_frame_alloc();
    ff.frame->format = AV_PIX_FMT_YUV420P;
    ff.frame->width  = ff.codecCtx->width;
    ff.frame->height = ff.codecCtx->height;
    if (av_frame_get_buffer(ff.frame, 0) < 0) {
        return false;
    }

    ff.packet = av_packet_alloc();

    ff.swsCtx = sws_getContext(
        static_cast<int>(width), static_cast<int>(height), AV_PIX_FMT_RGBA,
        ff.codecCtx->width, ff.codecCtx->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!ff.swsCtx) {
        fmt::print(stderr, "[VideoRecorder] Failed to create color space converter\n");
        return false;
    }

    fmt::print("[VideoRecorder] Started: {} ({}x{} @ {} fps, encoder: {}{}\n",
               m_config.outputPath, width, height, m_config.fps, usedEncoder,
               m_audioActive ? ", AAC audio)" : ")");
    return true;
#endif
}

bool VideoRecorder::encodeFrame(const RawFrame& rawFrame) {
#ifndef AE_HAS_FFMPEG
    return false;
#else
    auto& ff = *m_ffmpeg;

    if (av_frame_make_writable(ff.frame) < 0) {
        return false;
    }

    const int rowBytes  = static_cast<int>(rawFrame.width) * 4;
    const int srcHeight = std::min(static_cast<int>(rawFrame.height), ff.codecCtx->height);

    const uint8_t* srcPlanes[1];
    int            srcStrides[1];
    if (rawFrame.bottomUp) {
        // Point to the last row and walk backwards — eliminates the row-flip memcpy.
        srcPlanes[0]  = rawFrame.pixels.data() + static_cast<size_t>(srcHeight - 1) * rowBytes;
        srcStrides[0] = -rowBytes;
    } else {
        srcPlanes[0]  = rawFrame.pixels.data();
        srcStrides[0] = rowBytes;
    }
    sws_scale(ff.swsCtx, srcPlanes, srcStrides, 0, srcHeight,
              ff.frame->data, ff.frame->linesize);

    ff.frame->pts = static_cast<int64_t>(rawFrame.timestamp * 1'000'000.0);

    if (avcodec_send_frame(ff.codecCtx, ff.frame) < 0) {
        return false;
    }

    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(ff.codecCtx, ff.packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            return false;
        }
        av_packet_rescale_ts(ff.packet, ff.codecCtx->time_base, ff.stream->time_base);
        ff.packet->stream_index = ff.stream->index;
        av_interleaved_write_frame(ff.fmtCtx, ff.packet);
        av_packet_unref(ff.packet);
    }

    return true;
#endif
}

void VideoRecorder::flushEncoder() {
#ifdef AE_HAS_FFMPEG
    auto& ff = *m_ffmpeg;
    if (!ff.codecCtx) {
        return;
    }

    avcodec_send_frame(ff.codecCtx, nullptr);

    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(ff.codecCtx, ff.packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            break;
        }
        av_packet_rescale_ts(ff.packet, ff.codecCtx->time_base, ff.stream->time_base);
        ff.packet->stream_index = ff.stream->index;
        av_interleaved_write_frame(ff.fmtCtx, ff.packet);
        av_packet_unref(ff.packet);
    }
#endif
}

// ─── FFmpeg audio implementation ────────────────────────────────────────────────────────────────

bool VideoRecorder::initAudioEncoder() {
#ifndef AE_HAS_FFMPEG
    return false;
#else
    auto& ff = *m_ffmpeg;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        fmt::print(stderr, "[VideoRecorder] AAC encoder not available\n");
        return false;
    }

    ff.audioCodecCtx = avcodec_alloc_context3(codec);
    if (!ff.audioCodecCtx) {
        return false;
    }

    ff.audioCodecCtx->sample_fmt  = AV_SAMPLE_FMT_FLTP; // native AAC encoder format
    ff.audioCodecCtx->sample_rate = m_config.audioSampleRate;
    ff.audioCodecCtx->bit_rate    = m_config.audioBitrate;
    av_channel_layout_default(&ff.audioCodecCtx->ch_layout, m_config.audioChannels);
    ff.audioCodecCtx->time_base = AVRational{1, m_config.audioSampleRate};

    if (ff.fmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        ff.audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(ff.audioCodecCtx, codec, nullptr) < 0) {
        fmt::print(stderr, "[VideoRecorder] Failed to open AAC encoder\n");
        return false;
    }

    ff.audioStream = avformat_new_stream(ff.fmtCtx, nullptr);
    if (!ff.audioStream) {
        return false;
    }
    ff.audioStream->time_base = ff.audioCodecCtx->time_base;
    avcodec_parameters_from_context(ff.audioStream->codecpar, ff.audioCodecCtx);

    // Resampler: interleaved float (FLT) → planar float (FLTP) required by AAC.
    // Sample rate and channel count match, so this is a layout conversion only.
    AVChannelLayout inLayout, outLayout;
    av_channel_layout_default(&inLayout,  m_config.audioChannels);
    av_channel_layout_default(&outLayout, m_config.audioChannels);
    int swrErr = swr_alloc_set_opts2(&ff.swrCtx,
                                     &outLayout, AV_SAMPLE_FMT_FLTP, m_config.audioSampleRate,
                                     &inLayout,  AV_SAMPLE_FMT_FLT,  m_config.audioSampleRate,
                                     0, nullptr);
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    if (swrErr < 0 || !ff.swrCtx || swr_init(ff.swrCtx) < 0) {
        fmt::print(stderr, "[VideoRecorder] Failed to init audio resampler\n");
        return false;
    }

    ff.audioFifo   = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, m_config.audioChannels, 1);
    ff.audioFrame  = av_frame_alloc();
    ff.audioPacket = av_packet_alloc();
    if (!ff.audioFifo || !ff.audioFrame || !ff.audioPacket) {
        return false;
    }

    fmt::print("[VideoRecorder] Audio track: AAC {} Hz, {} ch, {} kbps\n",
               m_config.audioSampleRate, m_config.audioChannels,
               m_config.audioBitrate / 1000);
    return true;
#endif
}

void VideoRecorder::drainAudio(bool flush) {
#ifdef AE_HAS_FFMPEG
    if (!m_audioActive || !m_ffmpeg || !m_ffmpeg->audioCodecCtx) {
        return;
    }
    auto& ff = *m_ffmpeg;

    // Move queued PCM out under lock; resample and encode outside it.
    std::vector<float> input;
    {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        if (!m_audioQueue.empty()) {
            input.assign(m_audioQueue.begin(), m_audioQueue.end());
            m_audioQueue.clear();
        }
    }

    if (!input.empty()) {
        const int inSamples = static_cast<int>(input.size()) / m_config.audioChannels;
        const uint8_t* inData[1] = { reinterpret_cast<const uint8_t*>(input.data()) };

        int outCount = swr_get_out_samples(ff.swrCtx, inSamples);
        if (outCount > 0) {
            uint8_t** outData = nullptr;
            int outLinesize   = 0;
            if (av_samples_alloc_array_and_samples(&outData, &outLinesize,
                                                   m_config.audioChannels, outCount,
                                                   AV_SAMPLE_FMT_FLTP, 0) >= 0) {
                int converted = swr_convert(ff.swrCtx, outData, outCount,
                                            inData, inSamples);
                if (converted > 0) {
                    av_audio_fifo_write(ff.audioFifo,
                                        reinterpret_cast<void**>(outData), converted);
                }
                if (outData) { av_freep(&outData[0]); }
                av_freep(&outData);
            }
        }
    }

    const int frameSize = ff.audioCodecCtx->frame_size > 0
                          ? ff.audioCodecCtx->frame_size : 1024;

    while (av_audio_fifo_size(ff.audioFifo) >= frameSize) {
        encodeAudioFrame(frameSize);
    }

    if (flush) {
        // Drain the resampler's internal delay buffer.
        int outCount = swr_get_out_samples(ff.swrCtx, 0);
        if (outCount > 0) {
            uint8_t** outData = nullptr;
            int outLinesize   = 0;
            if (av_samples_alloc_array_and_samples(&outData, &outLinesize,
                                                   m_config.audioChannels, outCount,
                                                   AV_SAMPLE_FMT_FLTP, 0) >= 0) {
                int converted = swr_convert(ff.swrCtx, outData, outCount, nullptr, 0);
                if (converted > 0) {
                    av_audio_fifo_write(ff.audioFifo,
                                        reinterpret_cast<void**>(outData), converted);
                }
                if (outData) { av_freep(&outData[0]); }
                av_freep(&outData);
            }
        }
        while (av_audio_fifo_size(ff.audioFifo) >= frameSize) {
            encodeAudioFrame(frameSize);
        }
        int remaining = av_audio_fifo_size(ff.audioFifo);
        if (remaining > 0) {
            encodeAudioFrame(remaining); // final, possibly short, frame
        }
    }
#else
    (void)flush;
#endif
}

void VideoRecorder::encodeAudioFrame(int nbSamples) {
#ifdef AE_HAS_FFMPEG
    auto& ff = *m_ffmpeg;

    av_frame_unref(ff.audioFrame);
    ff.audioFrame->nb_samples  = nbSamples;
    ff.audioFrame->format      = ff.audioCodecCtx->sample_fmt;
    ff.audioFrame->sample_rate = ff.audioCodecCtx->sample_rate;
    av_channel_layout_copy(&ff.audioFrame->ch_layout, &ff.audioCodecCtx->ch_layout);
    if (av_frame_get_buffer(ff.audioFrame, 0) < 0) {
        return;
    }

    av_audio_fifo_read(ff.audioFifo,
                       reinterpret_cast<void**>(ff.audioFrame->data), nbSamples);

    ff.audioFrame->pts = ff.audioNextPts;
    ff.audioNextPts   += nbSamples;

    if (avcodec_send_frame(ff.audioCodecCtx, ff.audioFrame) < 0) {
        return;
    }

    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(ff.audioCodecCtx, ff.audioPacket);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            return;
        }
        av_packet_rescale_ts(ff.audioPacket,
                             ff.audioCodecCtx->time_base,
                             ff.audioStream->time_base);
        ff.audioPacket->stream_index = ff.audioStream->index;
        av_interleaved_write_frame(ff.fmtCtx, ff.audioPacket);
        av_packet_unref(ff.audioPacket);
    }
#else
    (void)nbSamples;
#endif
}

void VideoRecorder::flushAudioEncoder() {
#ifdef AE_HAS_FFMPEG
    if (!m_audioActive || !m_ffmpeg || !m_ffmpeg->audioCodecCtx) {
        return;
    }
    auto& ff = *m_ffmpeg;

    avcodec_send_frame(ff.audioCodecCtx, nullptr);

    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(ff.audioCodecCtx, ff.audioPacket);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            break;
        }
        av_packet_rescale_ts(ff.audioPacket,
                             ff.audioCodecCtx->time_base,
                             ff.audioStream->time_base);
        ff.audioPacket->stream_index = ff.audioStream->index;
        av_interleaved_write_frame(ff.fmtCtx, ff.audioPacket);
        av_packet_unref(ff.audioPacket);
    }
#endif
}

void VideoRecorder::cleanup() {
#ifdef AE_HAS_FFMPEG
    if (!m_ffmpeg) {
        return;
    }
    auto& ff = *m_ffmpeg;

    if (ff.fmtCtx) {
        if (ff.fmtCtx->pb) {
            av_write_trailer(ff.fmtCtx);
            avio_closep(&ff.fmtCtx->pb);
        }
        avformat_free_context(ff.fmtCtx); // also frees stream objects
        ff.fmtCtx = nullptr;
    }

    // Video resources
    if (ff.frame)    { av_frame_free(&ff.frame); }
    if (ff.packet)   { av_packet_free(&ff.packet); }
    if (ff.codecCtx) { avcodec_free_context(&ff.codecCtx); }
    if (ff.swsCtx)   { sws_freeContext(ff.swsCtx); ff.swsCtx = nullptr; }

    // Audio resources
    if (ff.audioFrame)    { av_frame_free(&ff.audioFrame); }
    if (ff.audioPacket)   { av_packet_free(&ff.audioPacket); }
    if (ff.audioCodecCtx) { avcodec_free_context(&ff.audioCodecCtx); }
    if (ff.swrCtx)        { swr_free(&ff.swrCtx); }
    if (ff.audioFifo)     { av_audio_fifo_free(ff.audioFifo); ff.audioFifo = nullptr; }

    m_ffmpeg.reset();
    fmt::print("[VideoRecorder] Finished: {}\n", m_config.outputPath);
#endif
}
