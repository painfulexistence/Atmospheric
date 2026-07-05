#pragma once
#include "globals.hpp"
#include <array>
#include <glm/mat4x4.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class ShaderType { Vertex, Fragment, TessControl, TessEvaluation };

struct Shader {
    Shader(const std::string& path, ShaderType type);
    uint32_t shader;// Store as raw uint32_t instead of GLuint
};

struct ShaderProgramProps {
    std::string vert;
    std::string frag;
    std::optional<std::string> tesc = std::nullopt;
    std::optional<std::string> tese = std::nullopt;
    std::optional<std::vector<std::string>> feedbackVaryings = std::nullopt;
};

// Abstract base class representing a GPU shader program.
class ShaderProgram {
public:
    virtual ~ShaderProgram() = default;

    virtual void Activate() = 0;
    virtual void Deactivate() = 0;

    virtual void SetUniform(const std::string& uniform, const glm::mat4& val) = 0;
    virtual void SetUniform(const std::string& uniform, const glm::vec2& val) = 0;
    virtual void SetUniform(const std::string& uniform, const glm::vec3& val) = 0;
    virtual void SetUniform(const std::string& uniform, int val) = 0;
    virtual void SetUniform(const std::string& uniform, float val) = 0;
};

// OpenGL implementation of ShaderProgram.
class GLShaderProgram : public ShaderProgram {
public:
    GLShaderProgram() = default;
    GLShaderProgram(const ShaderProgramProps& props);
    GLShaderProgram(
        std::string vert,
        std::string frag,
        std::optional<std::string> tesc = std::nullopt,
        std::optional<std::string> tese = std::nullopt
    );
    ~GLShaderProgram() override;

    void Activate() override;
    void Deactivate() override;

    void SetUniform(const std::string& uniform, const glm::mat4& val) override;
    void SetUniform(const std::string& uniform, const glm::vec2& val) override;
    void SetUniform(const std::string& uniform, const glm::vec3& val) override;
    void SetUniform(const std::string& uniform, int val) override;
    void SetUniform(const std::string& uniform, float val) override;

    int GetAttrib(const std::string& attrib);
    int GetUniform(const std::string& uniform);

    uint32_t GetProgramID() const {
        return _program;
    }

private:
    uint32_t _program = 0;
    std::unordered_map<std::string, int> _uniformLocationCache;

    int CacheUniform(const std::string& uniform);
};
