#include "video_recorder.hpp"
#include "log.hpp"
#include "audio_subsystem.hpp"
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
    AVCodecContext* codecCtx = nullptr;
    AVFormatContext* fmtCtx = nullptr;
    AVStream* stream = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* swsCtx = nullptr;
    // Audio
    AVCodecContext* audioCodecCtx = nullptr;
    AVStream* audioStream = nullptr;
    AVPacket* audioPacket = nullptr;
    AVFrame* audioFrame = nullptr;
    SwrContext* swrCtx = nullptr;
    AVAudioFifo* audioFifo = nullptr;
    int64_t audioNextPts = 0;// cumulative samples (per channel)
#endif
};

// ─── Lifecycle ──────────────────────────────────────────────────────────────────────────────────

VideoRecorder::VideoRecorder() = default;

VideoRecorder::~VideoRecorder() {
    if (_recording) {
        stopRecording();
    }
}

bool VideoRecorder::startRecording(Renderer* renderer, const Config& config) {
#ifndef AE_HAS_FFMPEG
    Log::Error("[VideoRecorder] Built without FFmpeg support — recording unavailable");
    return false;
#else
    if (_recording) {
        return false;
    }

    _renderer = renderer;
    _config = config;
    _ffmpeg = std::make_unique<FFmpegContext>();
    _recordingStart = std::chrono::steady_clock::now();
    _stop = false;

    _audioActive = false;
    if (_config.captureAudio && _config.audioSampleRate > 0 && _config.audioChannels > 0) {
        _audioActive = true;
        std::lock_guard<std::mutex> lock(_audioMutex);
        _audioQueue.clear();
    }

#ifndef __EMSCRIPTEN__
    if (_config.captureAudio && _audioManager) _audioManager->attachVideoRecorder(this);
#endif

    _recording = true;

    _encoderThread = std::thread(&VideoRecorder::encoderThreadFunc, this);
    return true;
#endif
}

void VideoRecorder::stopRecording() {
    if (!_recording) {
        return;
    }

    _stop = true;
    _cv.notify_all();

#ifndef __EMSCRIPTEN__
    if (_audioManager) _audioManager->detachVideoRecorder();
#endif

    // All captureFrame() calls are done; PBO pixel data was already copied into
    // RawFrame.pixels, so it's safe to release the GL resources now.
    if (_renderer) _renderer->destroyReadbackPBOs();

    if (_encoderThread.joinable()) {
        _encoderThread.join();
    }

    _recording = false;
    _audioActive = false;
    _renderer = nullptr;
}

VideoRecorder::CaptureResult VideoRecorder::captureFrame() {
    if (!_recording) {
        return CaptureResult::Dropped;
    }

    // Phase 1: issue this frame's async DMA into the next PBO slot (non-blocking).
    _renderer->schedulePixelReadback();

    // Phase 2: collect the PBO written 2 frames ago (nullopt while priming).
    auto imgOpt = _renderer->collectPixelReadback();
    if (!imgOpt) {
        return CaptureResult::Dropped;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_frameQueue.size() >= MAX_QUEUE_FRAMES) {
            return CaptureResult::Dropped;// encoder lagging; pixel data discarded
        }
    }

    const double timestamp = std::chrono::duration<double>(std::chrono::steady_clock::now() - _recordingStart).count();

    RawFrame frame;
    frame.pixels = std::move(imgOpt->data);// move: zero extra memcpy
    frame.width = imgOpt->width;
    frame.height = imgOpt->height;
    frame.timestamp = timestamp;
    frame.bottomUp = imgOpt->bottomUp;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _frameQueue.push(std::move(frame));
    }
    _cv.notify_one();
    return CaptureResult::Captured;
}

// ─── Audio capture (caller thread) ─────────────────────────────────────────────────────────

void VideoRecorder::writeAudio(const float* frames, uint32_t frameCount) {
    if (!_audioActive || !_recording.load() || frameCount == 0) {
        return;
    }

    const size_t incoming = static_cast<size_t>(frameCount) * _config.audioChannels;
    // Cap at ~5 seconds to prevent unbounded growth if the encoder falls behind.
    const size_t cap = static_cast<size_t>(_config.audioSampleRate) * _config.audioChannels * 5;

    std::lock_guard<std::mutex> lock(_audioMutex);
    if (_audioQueue.size() + incoming > cap) {
        size_t overflow = std::min(_audioQueue.size() + incoming - cap, _audioQueue.size());
        _audioQueue.erase(_audioQueue.begin(), _audioQueue.begin() + overflow);
    }
    _audioQueue.insert(_audioQueue.end(), frames, frames + incoming);
}

// ─── Encoder thread ────────────────────────────────────────────────────────────────────────────────

void VideoRecorder::encoderThreadFunc() {
    bool encoderInitialized = false;

    while (true) {
        RawFrame frame;

        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [this] { return !_frameQueue.empty() || _stop.load(); });

            if (_frameQueue.empty()) {
                break;
            }

            frame = std::move(_frameQueue.front());
            _frameQueue.pop();
        }

        if (!encoderInitialized) {
            if (!initEncoder(frame.width, frame.height)) {
                Log::Error("[VideoRecorder] Encoder initialization failed");
                _recording = false;
                return;
            }
            encoderInitialized = true;
        }

        encodeFrame(frame);
        // Interleave audio: drain whatever PCM has accumulated since last frame.
        drainAudio(/*flush=*/false);
    }

    if (encoderInitialized) {
        drainAudio(/*flush=*/true);// resample + encode remaining audio
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
    auto& ff = *_ffmpeg;

    // AV1 requires even-numbered dimensions; crop one pixel if needed.
    width &= ~1u;
    height &= ~1u;
    if (width == 0 || height == 0) {
        Log::Error("[VideoRecorder] Frame dimensions too small after alignment");
        return false;
    }

    // Probe order: HW encoders first (lowest CPU overhead), then SW fallbacks.
    static constexpr const char* gkEncoderCandidates[] = {
        "h264_videotoolbox",// macOS VideoToolbox (always available on Mac)
        "h264_nvenc",// NVIDIA GPU
        "libsvtav1",// SW AV1, fastest
        "libaom-av1",// SW AV1
        "librav1e",// SW AV1
        "libx264",// SW H.264, widest compatibility
        nullptr
    };
    const AVCodec* codec = nullptr;
    std::string usedEncoder;
    // Try configured encoder first, then walk the candidate list.
    if (!_config.encoder.empty()) {
        codec = avcodec_find_encoder_by_name(_config.encoder.c_str());
        if (codec) usedEncoder = _config.encoder;
    }
    if (!codec) {
        for (int i = 0; gkEncoderCandidates[i]; ++i) {
            if (_config.encoder == gkEncoderCandidates[i]) continue;
            codec = avcodec_find_encoder_by_name(gkEncoderCandidates[i]);
            if (codec) {
                usedEncoder = gkEncoderCandidates[i];
                Log::Info("[VideoRecorder] Encoder '{}' not found, using '{}'", _config.encoder, usedEncoder);
                break;
            }
        }
    }
    if (!codec) {
        Log::Error("[VideoRecorder] No video encoder found (tried '{}' and all fallbacks)", _config.encoder);
        return false;
    }

    if (avformat_alloc_output_context2(&ff.fmtCtx, nullptr, nullptr, _config.outputPath.c_str()) < 0) {
        Log::Error("[VideoRecorder] Failed to create output context for '{}'", _config.outputPath);
        return false;
    }

    ff.codecCtx = avcodec_alloc_context3(codec);
    if (!ff.codecCtx) {
        return false;
    }

    ff.codecCtx->width = static_cast<int>(width);
    ff.codecCtx->height = static_cast<int>(height);
    ff.codecCtx->time_base = AVRational{ 1, 1'000'000 };
    ff.codecCtx->framerate = AVRational{ _config.fps, 1 };
    ff.codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    ff.codecCtx->gop_size = _config.fps;

    if (usedEncoder == "h264_videotoolbox") {
        ff.codecCtx->bit_rate = 8'000'000;// 8 Mbps — good quality at 30fps 1080p
    } else if (usedEncoder == "h264_nvenc") {
        av_opt_set(ff.codecCtx->priv_data, "preset", "p4", 0);// balanced speed/quality
        av_opt_set(ff.codecCtx->priv_data, "rc", "vbr", 0);
        av_opt_set_int(ff.codecCtx->priv_data, "cq", 23, 0);
    } else if (usedEncoder == "libsvtav1") {
        av_opt_set_int(ff.codecCtx->priv_data, "crf", _config.crf, 0);
        av_opt_set_int(ff.codecCtx->priv_data, "preset", 8, 0);
    } else if (usedEncoder == "libaom-av1") {
        av_opt_set_int(ff.codecCtx->priv_data, "crf", _config.crf, 0);
        av_opt_set_int(ff.codecCtx->priv_data, "cpu-used", 6, 0);
        av_opt_set(ff.codecCtx->priv_data, "usage", "realtime", 0);
    } else if (usedEncoder == "librav1e") {
        av_opt_set_int(ff.codecCtx->priv_data, "crf", _config.crf, 0);
        av_opt_set_int(ff.codecCtx->priv_data, "speed", 8, 0);
    } else if (usedEncoder == "libx264") {
        av_opt_set(ff.codecCtx->priv_data, "preset", "fast", 0);
        av_opt_set_int(ff.codecCtx->priv_data, "crf", 23, 0);// x264 scale: 0-51
    }

    if (ff.fmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        ff.codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(ff.codecCtx, codec, nullptr) < 0) {
        Log::Error("[VideoRecorder] Failed to open codec '{}'", usedEncoder);
        return false;
    }

    ff.stream = avformat_new_stream(ff.fmtCtx, nullptr);
    if (!ff.stream) {
        return false;
    }
    ff.stream->time_base = ff.codecCtx->time_base;
    avcodec_parameters_from_context(ff.stream->codecpar, ff.codecCtx);

    // Audio stream must be added before avformat_write_header.
    if (_audioActive) {
        if (!initAudioEncoder()) {
            Log::Error("[VideoRecorder] Audio encoder init failed — recording video only");
            _audioActive = false;
        }
    }

    if (!(ff.fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ff.fmtCtx->pb, _config.outputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
            Log::Error("[VideoRecorder] Cannot open output file '{}'", _config.outputPath);
            return false;
        }
    }

    if (avformat_write_header(ff.fmtCtx, nullptr) < 0) {
        Log::Error("[VideoRecorder] Failed to write container header");
        return false;
    }

    ff.frame = av_frame_alloc();
    ff.frame->format = AV_PIX_FMT_YUV420P;
    ff.frame->width = ff.codecCtx->width;
    ff.frame->height = ff.codecCtx->height;
    if (av_frame_get_buffer(ff.frame, 0) < 0) {
        return false;
    }

    ff.packet = av_packet_alloc();

    ff.swsCtx = sws_getContext(
        static_cast<int>(width),
        static_cast<int>(height),
        AV_PIX_FMT_RGBA,
        ff.codecCtx->width,
        ff.codecCtx->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );
    if (!ff.swsCtx) {
        Log::Error("[VideoRecorder] Failed to create color space converter");
        return false;
    }

    Log::Info("[VideoRecorder] Started: {} ({}x{} @ {} fps, encoder: {}{}",
        _config.outputPath,
        width,
        height,
        _config.fps,
        usedEncoder,
        _audioActive ? ", AAC audio)" : ")");
    return true;
#endif
}

bool VideoRecorder::encodeFrame(const RawFrame& rawFrame) {
#ifndef AE_HAS_FFMPEG
    return false;
#else
    auto& ff = *_ffmpeg;

    if (av_frame_make_writable(ff.frame) < 0) {
        return false;
    }

    const int rowBytes = static_cast<int>(rawFrame.width) * 4;
    const int srcHeight = std::min(static_cast<int>(rawFrame.height), ff.codecCtx->height);

    const uint8_t* srcPlanes[1];
    int srcStrides[1];
    if (rawFrame.bottomUp) {
        // Point to the last row and walk backwards — eliminates the row-flip memcpy.
        srcPlanes[0] = rawFrame.pixels.data() + static_cast<size_t>(srcHeight - 1) * rowBytes;
        srcStrides[0] = -rowBytes;
    } else {
        srcPlanes[0] = rawFrame.pixels.data();
        srcStrides[0] = rowBytes;
    }
    sws_scale(ff.swsCtx, srcPlanes, srcStrides, 0, srcHeight, ff.frame->data, ff.frame->linesize);

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
    auto& ff = *_ffmpeg;
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
    auto& ff = *_ffmpeg;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        Log::Error("[VideoRecorder] AAC encoder not available");
        return false;
    }

    ff.audioCodecCtx = avcodec_alloc_context3(codec);
    if (!ff.audioCodecCtx) {
        return false;
    }

    ff.audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;// native AAC encoder format
    ff.audioCodecCtx->sample_rate = _config.audioSampleRate;
    ff.audioCodecCtx->bit_rate = _config.audioBitrate;
    av_channel_layout_default(&ff.audioCodecCtx->ch_layout, _config.audioChannels);
    ff.audioCodecCtx->time_base = AVRational{ 1, _config.audioSampleRate };

    if (ff.fmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        ff.audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(ff.audioCodecCtx, codec, nullptr) < 0) {
        Log::Error("[VideoRecorder] Failed to open AAC encoder");
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
    av_channel_layout_default(&inLayout, _config.audioChannels);
    av_channel_layout_default(&outLayout, _config.audioChannels);
    int swrErr = swr_alloc_set_opts2(
        &ff.swrCtx,
        &outLayout,
        AV_SAMPLE_FMT_FLTP,
        _config.audioSampleRate,
        &inLayout,
        AV_SAMPLE_FMT_FLT,
        _config.audioSampleRate,
        0,
        nullptr
    );
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    if (swrErr < 0 || !ff.swrCtx || swr_init(ff.swrCtx) < 0) {
        Log::Error("[VideoRecorder] Failed to init audio resampler");
        return false;
    }

    ff.audioFifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, _config.audioChannels, 1);
    ff.audioFrame = av_frame_alloc();
    ff.audioPacket = av_packet_alloc();
    if (!ff.audioFifo || !ff.audioFrame || !ff.audioPacket) {
        return false;
    }

    Log::Info("[VideoRecorder] Audio track: AAC {} Hz, {} ch, {} kbps",
        _config.audioSampleRate,
        _config.audioChannels,
        _config.audioBitrate / 1000);
    return true;
#endif
}

void VideoRecorder::drainAudio(bool flush) {
#ifdef AE_HAS_FFMPEG
    if (!_audioActive || !_ffmpeg || !_ffmpeg->audioCodecCtx) {
        return;
    }
    auto& ff = *_ffmpeg;

    // Move queued PCM out under lock; resample and encode outside it.
    std::vector<float> input;
    {
        std::lock_guard<std::mutex> lock(_audioMutex);
        if (!_audioQueue.empty()) {
            input.assign(_audioQueue.begin(), _audioQueue.end());
            _audioQueue.clear();
        }
    }

    if (!input.empty()) {
        const int inSamples = static_cast<int>(input.size()) / _config.audioChannels;
        const uint8_t* inData[1] = { reinterpret_cast<const uint8_t*>(input.data()) };

        int outCount = swr_get_out_samples(ff.swrCtx, inSamples);
        if (outCount > 0) {
            uint8_t** outData = nullptr;
            int outLinesize = 0;
            if (av_samples_alloc_array_and_samples(
                    &outData, &outLinesize, _config.audioChannels, outCount, AV_SAMPLE_FMT_FLTP, 0
                )
                >= 0) {
                int converted = swr_convert(ff.swrCtx, outData, outCount, inData, inSamples);
                if (converted > 0) {
                    av_audio_fifo_write(ff.audioFifo, reinterpret_cast<void**>(outData), converted);
                }
                if (outData) {
                    av_freep(&outData[0]);
                }
                av_freep(&outData);
            }
        }
    }

    const int frameSize = ff.audioCodecCtx->frame_size > 0 ? ff.audioCodecCtx->frame_size : 1024;

    while (av_audio_fifo_size(ff.audioFifo) >= frameSize) {
        encodeAudioFrame(frameSize);
    }

    if (flush) {
        // Drain the resampler's internal delay buffer.
        int outCount = swr_get_out_samples(ff.swrCtx, 0);
        if (outCount > 0) {
            uint8_t** outData = nullptr;
            int outLinesize = 0;
            if (av_samples_alloc_array_and_samples(
                    &outData, &outLinesize, _config.audioChannels, outCount, AV_SAMPLE_FMT_FLTP, 0
                )
                >= 0) {
                int converted = swr_convert(ff.swrCtx, outData, outCount, nullptr, 0);
                if (converted > 0) {
                    av_audio_fifo_write(ff.audioFifo, reinterpret_cast<void**>(outData), converted);
                }
                if (outData) {
                    av_freep(&outData[0]);
                }
                av_freep(&outData);
            }
        }
        while (av_audio_fifo_size(ff.audioFifo) >= frameSize) {
            encodeAudioFrame(frameSize);
        }
        int remaining = av_audio_fifo_size(ff.audioFifo);
        if (remaining > 0) {
            encodeAudioFrame(remaining);// final, possibly short, frame
        }
    }
#else
    (void)flush;
#endif
}

void VideoRecorder::encodeAudioFrame(int nbSamples) {
#ifdef AE_HAS_FFMPEG
    auto& ff = *_ffmpeg;

    av_frame_unref(ff.audioFrame);
    ff.audioFrame->nb_samples = nbSamples;
    ff.audioFrame->format = ff.audioCodecCtx->sample_fmt;
    ff.audioFrame->sample_rate = ff.audioCodecCtx->sample_rate;
    av_channel_layout_copy(&ff.audioFrame->ch_layout, &ff.audioCodecCtx->ch_layout);
    if (av_frame_get_buffer(ff.audioFrame, 0) < 0) {
        return;
    }

    av_audio_fifo_read(ff.audioFifo, reinterpret_cast<void**>(ff.audioFrame->data), nbSamples);

    ff.audioFrame->pts = ff.audioNextPts;
    ff.audioNextPts += nbSamples;

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
        av_packet_rescale_ts(ff.audioPacket, ff.audioCodecCtx->time_base, ff.audioStream->time_base);
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
    if (!_audioActive || !_ffmpeg || !_ffmpeg->audioCodecCtx) {
        return;
    }
    auto& ff = *_ffmpeg;

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
        av_packet_rescale_ts(ff.audioPacket, ff.audioCodecCtx->time_base, ff.audioStream->time_base);
        ff.audioPacket->stream_index = ff.audioStream->index;
        av_interleaved_write_frame(ff.fmtCtx, ff.audioPacket);
        av_packet_unref(ff.audioPacket);
    }
#endif
}

void VideoRecorder::cleanup() {
#ifdef AE_HAS_FFMPEG
    if (!_ffmpeg) {
        return;
    }
    auto& ff = *_ffmpeg;

    if (ff.fmtCtx) {
        if (ff.fmtCtx->pb) {
            av_write_trailer(ff.fmtCtx);
            avio_closep(&ff.fmtCtx->pb);
        }
        avformat_free_context(ff.fmtCtx);// also frees stream objects
        ff.fmtCtx = nullptr;
    }

    // Video resources
    if (ff.frame) {
        av_frame_free(&ff.frame);
    }
    if (ff.packet) {
        av_packet_free(&ff.packet);
    }
    if (ff.codecCtx) {
        avcodec_free_context(&ff.codecCtx);
    }
    if (ff.swsCtx) {
        sws_freeContext(ff.swsCtx);
        ff.swsCtx = nullptr;
    }

    // Audio resources
    if (ff.audioFrame) {
        av_frame_free(&ff.audioFrame);
    }
    if (ff.audioPacket) {
        av_packet_free(&ff.audioPacket);
    }
    if (ff.audioCodecCtx) {
        avcodec_free_context(&ff.audioCodecCtx);
    }
    if (ff.swrCtx) {
        swr_free(&ff.swrCtx);
    }
    if (ff.audioFifo) {
        av_audio_fifo_free(ff.audioFifo);
        ff.audioFifo = nullptr;
    }

    _ffmpeg.reset();
    Log::Info("[VideoRecorder] Finished: {}", _config.outputPath);
#endif
}
