# VideoPlayer Example

Demonstrates `VideoPlayer` and `VideoRecorder` using a fullscreen 2D sprite.
Requires the engine to be built with `-DAE_USE_FFMPEG=ON`.

## Demo video

Sprite Fright (CC-BY 4.0, Blender Studio) is downloaded automatically on first build (~150 MB).
See [ATTRIBUTION.txt](ATTRIBUTION.txt) for full credit.

## Building

```bash
cmake -B build -DAE_USE_FFMPEG=ON
cmake --build build --target VideoPlayer
```

## Usage

```bash
# Default: plays the bundled Sprite Fright demo
./VideoPlayer

# Local file (any format FFmpeg supports)
./VideoPlayer /path/to/video.mp4

# HTTP / HTTPS stream
./VideoPlayer https://download.blender.org/demo/movies/sintel-hd.avi

# HLS playlist
./VideoPlayer https://example.com/stream.m3u8

# RTSP (e.g. IP camera)
./VideoPlayer rtsp://camera.local/stream
```

## Controls

| Key | Action |
|-----|--------|
| `SPACE` | Play / pause |
| `ESC` | Quit |

## Supported formats

VideoPlayer uses FFmpeg internally (`avformat` + `avcodec`), so it supports any container and codec that your FFmpeg build includes:

| Category | Examples |
|----------|----------|
| Containers | MP4, MKV, WebM, MOV, AVI, TS, FLV |
| Video codecs | H.264, H.265/HEVC, AV1, VP9, VP8 |
| Protocols | file, http, https, rtsp, hls |

> **Note:** YouTube URLs (`youtube.com/watch?v=...`) are not directly supported.
> Use `yt-dlp --get-url <youtube-url>` to extract a direct stream URL first.

## Notes

- Decoding is software-only (no hardware acceleration). 1080p H.264/VP9 plays
  comfortably on any modern CPU; 4K AV1 may drop frames.
- The video texture is updated each frame via `glTexSubImage2D` — no CPU copy
  is needed when the resolution stays constant across frames.
- `VideoRecorder` encodes to AV1 (libsvtav1 / libaom-av1); see `video_recorder.hpp`.
