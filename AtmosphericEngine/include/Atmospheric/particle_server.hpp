#pragma once

#include <vector>

// Forward declarations
namespace Atmospheric {
    class GraphicsServer;
    class Renderer;
    struct CameraInfo;
    class ParticleEmitterComponent;
    class ShaderProgram;
    class Mesh;

    class ParticleServer {
    public:
        static ParticleServer& GetInstance() {
            static ParticleServer instance;
            return instance;
        }

        ParticleServer(const ParticleServer&) = delete;
        void operator=(const ParticleServer&) = delete;

        void Init(GraphicsServer* graphicsServer);
        void Shutdown();

        void Register(ParticleEmitterComponent* emitter);
        void Unregister(ParticleEmitterComponent* emitter);

        // Called by emitter
        void CreateEmitterResources(ParticleEmitterComponent* emitter);
        void ReleaseEmitterResources(ParticleEmitterComponent* emitter);

        void Simulate(float deltaTime);
        void Draw(const CameraInfo& camInfo);

    private:
        ParticleServer() = default;
        ~ParticleServer() = default;

        GraphicsServer* graphics_server = nullptr;
        Renderer* renderer = nullptr;
        std::vector<ParticleEmitterComponent*> emitters;

        ShaderProgram* simulation_shader = nullptr;
        ShaderProgram* drawing_shader = nullptr;
        
        Mesh* quad_mesh = nullptr;

        void CreatePipelines();
        void CreateSharedResources();
    };
}