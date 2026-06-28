#include "Atmospheric.hpp"
#if defined(__EMSCRIPTEN__) || defined(ANDROID)
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif

// Path or HTTP/HTTPS/RTSP URL passed in as the first CLI argument.
// Example: ./VideoPlayer my_clip.mp4
// Example: ./VideoPlayer https://example.com/stream.m3u8
// Default: Sprite Fright (CC-BY, Blender Studio) — auto-downloaded at build time.
static std::string g_videoPath = "assets/video/sprite_fright.webm";

class VideoPlayerDemo : public Application {
    using Application::Application;

    VideoPlayer m_player;

    GLuint   m_videoTex    = 0;
    uint32_t m_texWidth    = 0;
    uint32_t m_texHeight   = 0;
    bool     m_texReady    = false; // true once glTexImage2D has been called

    FontHandle m_fontID = 0;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        m_fontID = GraphicsServer::Get()->LoadFont(
            "assets/fonts/NotoSans-SemiBold.ttf", 24.0f);

        // Allocate a GL texture; upload a 1x1 black pixel so the state is
        // well-defined before the first video frame arrives.
        glGenTextures(1, &m_videoTex);
        glBindTexture(GL_TEXTURE_2D, m_videoTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        uint8_t black[4] = {0, 0, 0, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, black);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Open video -- supports local paths AND HTTP / HTTPS / RTSP / HLS URLs.
        if (m_player.open(g_videoPath)) {
            m_player.play();
            console.Info(fmt::format("Playing '{}' ({:.1f} s)",
                                     g_videoPath, m_player.getDuration()));
        } else {
            console.Warn(fmt::format(
                "Could not open '{}'. "
                "Make sure the engine was built with FFmpeg support "
                "and that the path / URL is valid.",
                g_videoPath));
        }

        // Fullscreen 2D sprite.  CanvasPass uses top-left origin, Y-down, pixels.
        // flipY=true corrects the GL bottom-up vs. video top-down convention.
        auto* win  = Window::Get();
        auto  sz   = win->GetLogicalSize();
        float winW = static_cast<float>(sz.width);
        float winH = static_cast<float>(sz.height);

        auto* videoObj = CreateGameObject(glm::vec3(0.0f, 0.0f, 0.0f));
        videoObj->AddComponent<SpriteComponent>(SpriteProps{
            .size      = glm::vec2(winW, winH),
            .pivot     = glm::vec2(0.0f, 0.0f), // top-left anchor
            .color     = glm::vec4(1.0f),
            .texture   = m_videoTex,
            .layer     = CanvasLayer::LAYER_WORLD_2D,
            .flipY     = false, // video rows are top-down
        });

        // HUD overlay
        auto* hud = CreateGameObject(glm::vec3(10.0f, 10.0f, 0.0f));
        hud->AddComponent<Text2DComponent>(Text2DProps{
            .text    = "SPACE: play / pause   |   ESC: quit",
            .font    = m_fontID,
            .size    = glm::vec2(600.0f, 40.0f),
            .color   = glm::vec4(1.0f),
            .layer   = CanvasLayer::LAYER_WORLD_2D,
        });

        console.Info("Controls: SPACE = play/pause, ESC = quit");
    }

    void OnUpdate(float dt, float /*time*/) override {
        if (input.IsKeyPressed(Key::ESCAPE)) {
            Quit();
        }

        if (input.IsKeyPressed(Key::SPACE)) {
            if (m_player.isPlaying())
                m_player.pause();
            else
                m_player.play();
        }

        // Upload the newest decoded frame to the GL texture when available.
        if (m_player.update(dt)) {
            const VideoPlayer::Frame* frame = m_player.getCurrentFrame();
            if (frame && !frame->pixels.empty()) {
                glBindTexture(GL_TEXTURE_2D, m_videoTex);

                if (!m_texReady ||
                    frame->width  != m_texWidth ||
                    frame->height != m_texHeight)
                {
                    // (Re)allocate texture storage for this frame size.
                    m_texWidth  = frame->width;
                    m_texHeight = frame->height;
                    m_texReady  = true;
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                                 static_cast<GLsizei>(m_texWidth),
                                 static_cast<GLsizei>(m_texHeight),
                                 0, GL_RGBA, GL_UNSIGNED_BYTE,
                                 frame->pixels.data());
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                    static_cast<GLsizei>(m_texWidth),
                                    static_cast<GLsizei>(m_texHeight),
                                    GL_RGBA, GL_UNSIGNED_BYTE,
                                    frame->pixels.data());
                }

                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
    }

public:
    ~VideoPlayerDemo() override {
        m_player.close();
        if (m_videoTex) {
            glDeleteTextures(1, &m_videoTex);
            m_videoTex = 0;
        }
    }
};

// Native entry point -- no Emscripten support (FFmpeg is unavailable on WASM).
int main(int argc, char* argv[]) {
    if (argc > 1)
        g_videoPath = argv[1];

    VideoPlayerDemo game({
        .windowWidth        = 1024,
        .windowHeight       = 429,
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
    return 0;
}
