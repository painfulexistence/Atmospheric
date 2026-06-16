#pragma once
#include "command_encoder.hpp"
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>

// Abstract GPU pipeline — shader program + fixed-function state (depth, blend, cull).
//
// GL backend:     GLPipeline wraps ShaderProgram + glEnable/glDisable state.
// WebGPU backend: GPUPipeline wraps WGPURenderPipeline + WGPUBindGroup.
class RenderPipeline {
public:
    virtual ~RenderPipeline() = default;

    // Activate this pipeline for subsequent draw calls.
    // enc is unused by GL (pass nullptr); WebGPU sets the pipeline on the render pass.
    virtual void Bind(CommandEncoder* enc = nullptr) = 0;

    // Uniform / push-constant setters.
    // GL: delegates to glUniform*. WebGPU: writes into a mapped uniform buffer.
    virtual void SetUniform(const std::string& name, const glm::mat4& val) = 0;
    virtual void SetUniform(const std::string& name, const glm::vec3& val) = 0;
    virtual void SetUniform(const std::string& name, int val) = 0;
    virtual void SetUniform(const std::string& name, float val) = 0;
};
