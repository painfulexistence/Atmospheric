#include "Atmospheric.hpp"
#include "Atmospheric/gfx_factory.hpp"

// Path or HTTP/HTTPS/RTSP URL passed in as the first CLI argument.
// Example: ./VideoPlayer my_clip.mp4
// Example: ./VideoPlayer https://example.com/stream.m3u8
// Default: Sprite Fright (CC-BY, Blender Studio) — auto-downloaded at build time.
static std::string gVideoPath = "assets/video/sprite_fright.webm";

class VideoPlayerDemo : public Application {
    using Application::Application;

    VideoPlayer _m_player;

    uint32_t _m_videoTex = 0;
    uint32_t _m_texWidth = 0;
    uint32_t _m_texHeight = 0;
    bool _m_texReady = false;// true once the first frame has been uploaded

    FontHandle _m_fontID = 0;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        _m_fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);

        // Allocate a texture via GfxFactory (works on both the GL and WebGPU
        // backends); upload a 1x1 black pixel so the state is well-defined
        // before the first video frame arrives.
        uint8_t black[4] = { 0, 0, 0, 255 };
        _m_videoTex = GfxFactory::UploadTexture2D(black, 1, 1);

        // Open video -- supports local paths AND HTTP / HTTPS / RTSP / HLS URLs.
        if (_m_player.open(gVideoPath)) {
            _m_player.play();
            ConsoleSubsystem::Get()->Info(fmt::format("Playing '{}' ({:.1f} s)", gVideoPath, _m_player.getDuration()));
        } else {
            ConsoleSubsystem::Get()->Warn(
                fmt::format(
                    "Could not open '{}'. "
                    "Make sure the engine was built with FFmpeg support "
                    "and that the path / URL is valid.",
                    gVideoPath
                )
            );
        }

        // Fullscreen 2D sprite.  CanvasPass uses top-left origin, Y-down, pixels.
        // flipY=true corrects the GL bottom-up vs. video top-down convention.
        auto* win = Window::Get();
        auto sz = win->GetLogicalSize();
        auto winW = static_cast<float>(sz.width);
        auto winH = static_cast<float>(sz.height);

        auto* videoObj = CreateGameObject(glm::vec3(0.0f, 0.0f, 0.0f));
        videoObj->AddComponent<SpriteComponent>(SpriteProps{
            .size = glm::vec2(winW, winH),
            .pivot = glm::vec2(0.0f, 0.0f),// top-left anchor
            .color = glm::vec4(1.0f),
            .texture = _m_videoTex,
            .layer = CanvasLayer::LAYER_WORLD_2D,
            .flipY = false,// video rows are top-down
        });

        // HUD overlay
        auto* hud = CreateGameObject(glm::vec3(10.0f, 10.0f, 0.0f));
        hud->AddComponent<Text2DComponent>(Text2DProps{
            .text = "SPACE: play / pause   |   ESC: quit",
            .font = _m_fontID,
            .size = glm::vec2(600.0f, 40.0f),
            .color = glm::vec4(1.0f),
            .layer = CanvasLayer::LAYER_WORLD_2D,
        });

        ConsoleSubsystem::Get()->Info("Controls: SPACE = play/pause, ESC = quit");
    }

    void OnUpdate(float dt, float /*time*/) override {
        if (InputSubsystem::Get()->IsKeyPressed(Key::ESCAPE)) {
            Quit();
        }

        if (InputSubsystem::Get()->IsKeyPressed(Key::SPACE)) {
            if (_m_player.isPlaying())
                _m_player.pause();
            else
                _m_player.play();
        }

        // Upload the newest decoded frame via GfxFactory when available.
        // UpdateTexture2D reallocates storage itself if the size changed, so
        // no separate first-frame/resize branch is needed here.
        if (_m_player.update(dt)) {
            const VideoPlayer::Frame* frame = _m_player.getCurrentFrame();
            if (frame && !frame->pixels.empty()) {
                _m_texWidth = frame->width;
                _m_texHeight = frame->height;
                _m_texReady = true;
                GfxFactory::UpdateTexture2D(
                    _m_videoTex, frame->pixels.data(), static_cast<int>(_m_texWidth), static_cast<int>(_m_texHeight)
                );
            }
        }
    }

public:
    ~VideoPlayerDemo() override {
        _m_player.close();
        if (_m_videoTex) {
            GfxFactory::ReleaseTexture(_m_videoTex);
            _m_videoTex = 0;
        }
    }
};

// Native entry point -- no Emscripten support (FFmpeg is unavailable on WASM).
int main(int argc, char* argv[]) {
    if (argc > 1) gVideoPath = argv[1];

    VideoPlayerDemo game(
        {
            .windowWidth = 1024,
            .windowHeight = 429,
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}
