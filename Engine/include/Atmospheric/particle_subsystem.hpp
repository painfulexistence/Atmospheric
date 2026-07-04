#pragma once

#include "globals.hpp"
#include <glm/glm.hpp>
#include <vector>

// Forward declarations
class GraphicsSubsystem;
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

    class ParticleSubsystem {
    public:
        static ParticleSubsystem& GetInstance() {
            static ParticleSubsystem instance;
            return instance;
        }

        ParticleSubsystem(const ParticleSubsystem&) = delete;
        void operator=(const ParticleSubsystem&) = delete;

        void Init(GraphicsSubsystem* graphicsServer);
        void Shutdown();

        void Register(ParticleEmitterComponent* emitter);
        void Unregister(ParticleEmitterComponent* emitter);

        // Called by emitter
        void CreateEmitterResources(ParticleEmitterComponent* emitter);
        void ReleaseEmitterResources(ParticleEmitterComponent* emitter);

        void Simulate(float deltaTime);
        void Draw(const CameraInfo& camInfo);

    private:
        ParticleSubsystem() = default;
        ~ParticleSubsystem() = default;

        GraphicsSubsystem* graphics_server = nullptr;
        Renderer* renderer = nullptr;
        std::vector<ParticleEmitterComponent*> emitters;

        // Stable references; the shaders are scene-tier assets that can be
        // destroyed on scene switches, so we resolve per use instead of
        // caching pointers.
        ShaderHandle simulation_shader;
        ShaderHandle drawing_shader;

        MeshHandle quad_mesh;

        void CreatePipelines();
        void CreateSharedResources();
    };
}// namespace Atmospheric