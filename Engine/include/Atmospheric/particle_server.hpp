#pragma once

#include <glm/glm.hpp>
#include <vector>

// Forward declarations
// Forward declarations
class GraphicsServer;
class Renderer;
class ShaderProgram;
class Mesh;

namespace Atmospheric {
    struct CameraInfo {
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 position;
    };
    class ParticleEmitterComponent;

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
}// namespace Atmospheric