#include "particle_subsystem.hpp"

#include "asset_manager.hpp"
#include "console_subsystem.hpp"
#include "gfx_factory.hpp"
#include "graphics_subsystem.hpp"
#include "particle_emitter.hpp"
#include "renderer.hpp"
#include "rng.hpp"
#include "vertex.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Atmospheric {

    void ParticleSubsystem::Init(GraphicsSubsystem* in_graphics_server) {
        this->graphics_server = in_graphics_server;
        if (this->graphics_server == nullptr) {
            throw std::runtime_error("Invalid graphics server provided to ParticleSubsystem.");
        }
        this->renderer = this->graphics_server->renderer.get();
        if (this->renderer == nullptr) {
            throw std::runtime_error("Renderer is not initialized in GraphicsSubsystem.");
        }

        CreateSharedResources();
        CreatePipelines();
        ConsoleSubsystem::Get()->Info("Particle Subsystem Initialized");
    }

    void ParticleSubsystem::Shutdown() {
        // Shaders and meshes are managed by AssetManager, no need to delete here.
        // Emitter components are responsible for releasing their own resources via OnDetach.
        graphics_server = nullptr;
        renderer = nullptr;
    }

    void ParticleSubsystem::Register(ParticleEmitterComponent* emitter) {
        emitters.push_back(emitter);
    }

    void ParticleSubsystem::Unregister(ParticleEmitterComponent* emitter) {
        emitters.erase(std::remove(emitters.begin(), emitters.end(), emitter), emitters.end());
    }

    void ParticleSubsystem::CreatePipelines() {
        AssetManager& assets = AssetManager::Get();
        try {
            // Simulation Shader
            ShaderProgramProps sim_props;
            sim_props.vert = "shaders/ParticleSim.vert";
            sim_props.frag = "shaders/noop.frag";// A dummy fragment shader
            sim_props.feedbackVaryings = { "out_position", "out_velocity", "out_color", "out_life",
                                           "out_size",     "out_pad0",     "out_pad1" };
            assets.CreateShader("particle_sim", sim_props);
            simulation_shader = assets.GetShaderHandle("particle_sim");

            // Drawing Shader
            // Assuming Particle.vert and Particle.frag are already loaded or can be loaded.
            ShaderProgramProps draw_props;
            draw_props.vert = "shaders/Particle.vert";
            draw_props.frag = "shaders/Particle.frag";
            assets.CreateShader("particle_draw", draw_props);
            drawing_shader = assets.GetShaderHandle("particle_draw");

        } catch (const std::exception& e) {
            ConsoleSubsystem::Get()->Error(fmt::format("Failed to create particle shaders: {}", e.what()));
            throw;
        }
    }

    Mesh* CreateQuadMesh() {
        std::vector<Vertex> vertices;
        std::vector<uint16_t> indices;

        glm::vec3 position(0.0f);
        glm::vec2 size(1.0f);
        glm::vec3 normal(0, 0, 1);
        glm::vec3 tangent(1, 0, 0);
        glm::vec3 bitangent(0, 1, 0);

        vertices.push_back({ { -0.5f, -0.5f, 0.0f }, { 0, 0 }, normal, tangent, bitangent });
        vertices.push_back({ { -0.5f, 0.5f, 0.0f }, { 0, 1 }, normal, tangent, bitangent });
        vertices.push_back({ { 0.5f, 0.5f, 0.0f }, { 1, 1 }, normal, tangent, bitangent });
        vertices.push_back({ { 0.5f, -0.5f, 0.0f }, { 1, 0 }, normal, tangent, bitangent });

        indices = { 0, 1, 2, 0, 2, 3 };

        auto mesh = new Mesh(MeshType::PRIM);
        mesh->Initialize(vertices, indices);
        // Calculate bounds
        mesh->SetBoundingBox({ { glm::vec3(0.5f, 0.5f, 0.0f),
                                 glm::vec3(-0.5f, 0.5f, 0.0f),
                                 glm::vec3(-0.5f, -0.5f, 0.0f),
                                 glm::vec3(0.5f, -0.5f, 0.0f),
                                 glm::vec3(0.5f, 0.5f, 0.0f),
                                 glm::vec3(-0.5f, 0.5f, 0.0f),
                                 glm::vec3(-0.5f, -0.5f, 0.0f),
                                 glm::vec3(0.5f, -0.5f, 0.0f) } });
        return mesh;
    }

    void ParticleSubsystem::CreateSharedResources() {
        // The quad mesh for drawing particles
        auto& am = AssetManager::Get();
        quad_mesh = am.GetMesh("quad");
        if (!quad_mesh.IsValid()) {
            auto quad = CreateQuadMesh();
            quad_mesh = am.CreateMesh("quad", quad);
        }
    }

    void ParticleSubsystem::CreateEmitterResources(ParticleEmitterComponent* emitter) {
        // Particles are GL-only for now (transform-feedback simulation has no
        // WebGPU equivalent until a compute pipeline abstraction exists).
        if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
        // 1. Create VAO for simulation pass
        glGenVertexArrays(1, &emitter->vao);

        // 2. Create two VBOs for ping-ponging
        glGenBuffers(2, emitter->particle_vbos);

        // 3. Populate with initial data
        RNG rng;
        std::vector<Particle> initial_particles(emitter->GetMaxParticles());
        for (auto& p : initial_particles) {
            float r = sqrt(rng.RandomFloat()) * 0.1f;
            float theta = rng.RandomFloat() * 2.0f * 3.14159f;
            p.position = glm::vec3(r * cos(theta), r * sin(theta), 0.0f);
            p.velocity = glm::normalize(glm::vec3(-p.position.y, p.position.x, 0)) * 0.5f;
            p.color = glm::vec4(rng.RandomFloat(), rng.RandomFloat(), rng.RandomFloat(), 1.0f);
            p.life = rng.RandomFloatInRange(0.0f, 5.0f);
            p.size = rng.RandomFloatInRange(0.05f, 0.1f);
        }

        // 4. Upload data to both buffers
        for (int i = 0; i < 2; ++i) {
            glBindBuffer(GL_ARRAY_BUFFER, emitter->particle_vbos[i]);
            glBufferData(
              GL_ARRAY_BUFFER, sizeof(Particle) * emitter->GetMaxParticles(), initial_particles.data(), GL_DYNAMIC_COPY
            );
        }

        // 5. Configure VAO layout for simulation shader
        glBindVertexArray(emitter->vao);
        glBindBuffer(GL_ARRAY_BUFFER, emitter->GetCurrentSourceVBO());// Bind one of the VBOs to set layout

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), reinterpret_cast<void*>(offsetof(Particle, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), reinterpret_cast<void*>(offsetof(Particle, velocity)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), reinterpret_cast<void*>(offsetof(Particle, color)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), reinterpret_cast<void*>(offsetof(Particle, life)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), reinterpret_cast<void*>(offsetof(Particle, size)));

        glBindVertexArray(0);
    }

    void ParticleSubsystem::ReleaseEmitterResources(ParticleEmitterComponent* emitter) {
        if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
        if (emitter->vao != 0) {
            glDeleteVertexArrays(1, &emitter->vao);
        }
        if (emitter->particle_vbos[0] != 0) {
            glDeleteBuffers(2, emitter->particle_vbos);
        }
    }

    struct SimUniforms {
        glm::vec3 attractorPos;
        float deltaTime;
    };

    void ParticleSubsystem::Simulate(float deltaTime) {
        if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
        ShaderProgram* simShader = AssetManager::Get().ResolveShader(simulation_shader);
        if (emitters.empty() || !simShader) return;

        simShader->Activate();
        renderer->BeginTransformFeedbackPass();

        for (auto* emitter : emitters) {
            SimUniforms uniforms = { .attractorPos = emitter->attractor, .deltaTime = deltaTime };
            // A bit of a hack to set uniforms without a proper UBO system
            simShader->SetUniform("attractorPos", uniforms.attractorPos);
            simShader->SetUniform("deltaTime", uniforms.deltaTime);

            glBindVertexArray(emitter->GetVAO());
            glBindBuffer(GL_ARRAY_BUFFER, emitter->GetCurrentSourceVBO());
            renderer->BindTransformFeedbackBuffer(emitter->GetCurrentDestinationVBO());

            glDrawArrays(GL_POINTS, 0, emitter->GetMaxParticles());
        }

        renderer->EndTransformFeedbackPass();
        glBindVertexArray(0);
        simShader->Deactivate();
    }

    void ParticleSubsystem::Draw(const CameraInfo& camInfo) {
        if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
        ShaderProgram* drawShader = AssetManager::Get().ResolveShader(drawing_shader);
        if (emitters.empty() || !drawShader) return;

        drawShader->Activate();
        drawShader->SetUniform("cam_pos", camInfo.position);
        drawShader->SetUniform("ProjectionView", camInfo.projection * camInfo.view);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);// Additive blending
        glDepthMask(GL_FALSE);// Don't write to depth buffer

        // Bind the quad mesh once for all emitters
        Mesh* quadMeshPtr = AssetManager::Get().GetMeshPtr(quad_mesh);
        if (!quadMeshPtr) return;
        glBindVertexArray(quadMeshPtr->vao);

        for (auto* emitter : emitters) {
            // Bind the buffer with the latest particle data as a storage buffer
            // This requires GL 4.3+, let's use vertex attributes for broader compatibility.
            // We'll bind the particle VBO and use instanced rendering.

            // This is a simplified drawing path. A real engine would batch this.
            glBindBuffer(GL_ARRAY_BUFFER, emitter->GetCurrentDestinationVBO());

            // Re-set the vertex attrib pointers for the particle data, this time with divisors for instancing.
            glEnableVertexAttribArray(2);// instance position
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), reinterpret_cast<void*>(offsetof(Particle, position)));
            glVertexAttribDivisor(2, 1);

            glEnableVertexAttribArray(3);// instance color
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), reinterpret_cast<void*>(offsetof(Particle, color)));
            glVertexAttribDivisor(3, 1);

            glEnableVertexAttribArray(4);// instance size
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), reinterpret_cast<void*>(offsetof(Particle, size)));
            glVertexAttribDivisor(4, 1);

            glDrawElementsInstanced(
              GL_TRIANGLES, quadMeshPtr->triCount * 3, GL_UNSIGNED_SHORT, 0, emitter->GetMaxParticles()
            );

            // Reset divisors
            glVertexAttribDivisor(2, 0);
            glVertexAttribDivisor(3, 0);
            glVertexAttribDivisor(4, 0);

            emitter->SwapBuffers();
        }

        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        drawShader->Deactivate();
    }
}// namespace Atmospheric