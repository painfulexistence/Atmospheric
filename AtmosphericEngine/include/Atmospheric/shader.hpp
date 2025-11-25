#pragma once
#include "globals.hpp"
#include <optional>
#include <unordered_map>
#include <glm/mat4x4.hpp>

enum ShaderType
{
    VERTEX = GL_VERTEX_SHADER,
    FRAGMENT = GL_FRAGMENT_SHADER,
#ifndef __EMSCRIPTEN__
    TESS_CONTROL = GL_TESS_CONTROL_SHADER,
    TESS_EVALUATION = GL_TESS_EVALUATION_SHADER
#endif
};

struct Shader
{
    Shader(const std::string& path, ShaderType type);
    GLuint shader;
};

struct ShaderProgramProps
{
    std::string vert;
    std::string frag;
    std::optional<std::string> tesc = std::nullopt;
    std::optional<std::string> tese = std::nullopt;
    std::optional<std::vector<std::string>> feedbackVaryings = std::nullopt;
};

class ShaderProgram
{
public:
    ShaderProgram() = default;
    ShaderProgram(const ShaderProgramProps& props);
    ShaderProgram(std::string vert, std::string frag, std::optional<std::string> tesc = std::nullopt, std::optional<std::string> tese = std::nullopt);
    ShaderProgram(std::array<Shader, 2>&);
    ShaderProgram(std::array<Shader, 4>&);

    GLint GetAttrib(const std::string& attrib) {
        return glGetAttribLocation(_program, attrib.c_str());
    };

    GLint GetUniform(const std::string& uniform) {
        auto it = _uniformLocationCache.find(uniform);
        if (it != _uniformLocationCache.end()) {
            return it->second;
        }
        return CacheUniform(uniform.c_str());
    }
    void SetUniform(const std::string& uniform, const glm::mat4& val)
    {
        glUniformMatrix4fv(GetUniform(uniform), 1, GL_FALSE, &val[0][0]);
    };
    void SetUniform(const std::string& uniform, const glm::vec3& val)
    {
        glUniform3fv(GetUniform(uniform), 1, &val[0]);
    };
    void SetUniform(const std::string& uniform, int val)
    {
        glUniform1i(GetUniform(uniform), val);
    };
    void SetUniform(const std::string& uniform, float val)
    {
        glUniform1f(GetUniform(uniform), val);
    };

    void Activate();

    void Deactivate();

private:
    GLuint _program;
    std::unordered_map<std::string, GLint> _uniformLocationCache;

    GLint CacheUniform(const std::string& uniform) {
        auto it = _uniformLocationCache.find(uniform);
        if (it != _uniformLocationCache.end()) {
            return it->second;
        }
        GLint location = glGetUniformLocation(_program, uniform.c_str());
        _uniformLocationCache[uniform] = location;
        return location;
    }
};
