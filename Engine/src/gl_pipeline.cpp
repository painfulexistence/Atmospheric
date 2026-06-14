#include "gl_pipeline.hpp"
#include "shader.hpp"

GLPipeline::GLPipeline(ShaderProgram* shader) : _shader(shader) {}

void GLPipeline::Bind(CommandEncoder* /*enc*/) {
    if (_shader) _shader->Activate();
}

void GLPipeline::SetUniform(const std::string& name, const glm::mat4& val) {
    if (_shader) _shader->SetUniform(name, val);
}

void GLPipeline::SetUniform(const std::string& name, const glm::vec3& val) {
    if (_shader) _shader->SetUniform(name, val);
}

void GLPipeline::SetUniform(const std::string& name, int val) {
    if (_shader) _shader->SetUniform(name, val);
}

void GLPipeline::SetUniform(const std::string& name, float val) {
    if (_shader) _shader->SetUniform(name, val);
}
