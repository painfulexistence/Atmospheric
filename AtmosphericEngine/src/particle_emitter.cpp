#include "particle_emitter.hpp"
#include "particle_server.hpp"

namespace Atmospheric {
    ParticleEmitterComponent::ParticleEmitterComponent(const ComponentDef& def, uint32_t maxParticles)
        : Component(def),
          server(ParticleServer::GetInstance()),
          max_particles(maxParticles)
    {
    }

    ParticleEmitterComponent::~ParticleEmitterComponent() {
        // OnDetach will be called by the ECS before destruction,
        // which handles unregistering and resource cleanup.
    }

    void ParticleEmitterComponent::OnAttach() {
        server.CreateEmitterResources(this);
        server.Register(this);
    }

    void ParticleEmitterComponent::OnDetach() {
        server.Unregister(this);
        server.ReleaseEmitterResources(this);
    }

    GLuint ParticleEmitterComponent::GetCurrentSourceVBO() const {
        return particle_vbos[current_buffer_index];
    }

    GLuint ParticleEmitterComponent::GetCurrentDestinationVBO() const {
        return particle_vbos[1 - current_buffer_index];
    }

    void ParticleEmitterComponent::SwapBuffers() {
        current_buffer_index = 1 - current_buffer_index;
    }
}