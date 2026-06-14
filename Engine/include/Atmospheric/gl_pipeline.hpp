#pragma once
#include "render_pipeline.hpp"

class ShaderProgram;

// OpenGL implementation of RenderPipeline.
// Thin wrapper around ShaderProgram that satisfies the abstract interface.
class GLPipeline : public RenderPipeline {
public:
    explicit GLPipeline(ShaderProgram* shader);

    // enc is unused for GL — state is implicit.
    void Bind(CommandEncoder* enc = nullptr) override;

    void SetUniform(const std::string& name, const glm::mat4& val) override;
    void SetUniform(const std::string& name, const glm::vec3& val) override;
    void SetUniform(const std::string& name, int val) override;
    void SetUniform(const std::string& name, float val) override;

private:
    ShaderProgram* _shader = nullptr;
};
