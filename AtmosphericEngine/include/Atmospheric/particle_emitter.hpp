#pragma once

#include "component.hpp"
#include "globals.hpp"
#include <string>
#include <glm/vec3.hpp>

// Matches the layout in the shader
// Using alignas is critical for buffer data structures
namespace Atmospheric {
    struct Particle {
        alignas(16) glm::vec3 position;
        alignas(16) glm::vec3 velocity;
        alignas(16) glm::vec4 color;
        float life = 0.0f;
        float size = 0.0f;
        // padding for std140/std430 alignment rules
        float _pad0, _pad1;
    };

    class ParticleServer;

    class ParticleEmitterComponent : public Component {
    public:
        ParticleEmitterComponent(const ComponentDef& def, uint32_t maxParticles = 10000);
        ~ParticleEmitterComponent() override;

        // --- Component Interface ---
        void OnAttach() override;
        void OnDetach() override;

        uint32_t GetMaxParticles() const { return max_particles; }
        
        GLuint GetCurrentSourceVBO() const;
        GLuint GetCurrentDestinationVBO() const;
        GLuint GetVAO() const { return vao; }
        void SwapBuffers();

        glm::vec3 attractor = glm::vec3(0.0f);

    private:
        friend class ParticleServer;

        ParticleServer& server;
        uint32_t max_particles;
        
        // We manage raw GLuints here as they are specific to this system
        GLuint particle_vbos[2] = { 0, 0 };
        GLuint vao = 0; // VAO to define particle layout for simulation pass
        uint32_t current_buffer_index = 0;
    };
}