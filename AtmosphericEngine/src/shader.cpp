#include "shader.hpp"
#include "file.hpp"

Shader::Shader(const std::string& path, ShaderType type)
{
    const auto& shaderSrc = File(path).GetContent();
    const char* src = shaderSrc.c_str();
    const int len = shaderSrc.size();

    shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, &len);

    glCompileShader(shader);
    GLint isCompiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

        GLchar* log = new GLchar[maxLength];
        glGetShaderInfoLog(shader, maxLength, &maxLength, log);

        throw std::runtime_error(fmt::format("Shader error: {}\n", (char*)log));
    }
}

ShaderProgram::ShaderProgram(const ShaderProgramProps& props) : _program(glCreateProgram())
{
    glAttachShader(_program, Shader(props.vert, ShaderType::VERTEX).shader);
    glAttachShader(_program, Shader(props.frag, ShaderType::FRAGMENT).shader);
    if (props.tese.has_value()) {
        glAttachShader(_program, Shader(props.tese.value(), ShaderType::TESS_EVALUATION).shader);
    }
#endif

    if (props.feedbackVaryings.has_value()) {
        std::vector<const char*> varyings_c_str;
        varyings_c_str.reserve(props.feedbackVaryings->size());
        for (const auto& varying : props.feedbackVaryings.value()) {
            varyings_c_str.push_back(varying.c_str());
        }
        glTransformFeedbackVaryings(_program, varyings_c_str.size(), varyings_c_str.data(), GL_INTERLEAVED_ATTRIBS);
    }

    glLinkProgram(_program);
}

ShaderProgram::ShaderProgram(std::string vert, std::string frag, std::optional<std::string> tesc, std::optional<std::string> tese) : _program(glCreateProgram())
{
    glAttachShader(_program, Shader(vert, ShaderType::VERTEX).shader);
    glAttachShader(_program, Shader(frag, ShaderType::FRAGMENT).shader);
#ifndef __EMSCRIPTEN__
    if (tesc.has_value() && tese.has_value()) {
        glAttachShader(_program, Shader(tesc.value(), ShaderType::TESS_CONTROL).shader);
        glAttachShader(_program, Shader(tese.value(), ShaderType::TESS_EVALUATION).shader);
    }
#endif
    glLinkProgram(_program);
}

ShaderProgram::ShaderProgram(std::array<Shader, 2>& shaders) : _program(glCreateProgram())
{
    for (int i = shaders.size() - 1; i >= 0; i--)
    {
        glAttachShader(_program, shaders[i].shader);
    }
    glLinkProgram(_program);
}

ShaderProgram::ShaderProgram(std::array<Shader, 4>& shaders) : _program(glCreateProgram())
{
    for (int i = shaders.size() - 1; i >= 0; i--)
    {
        glAttachShader(_program, shaders[i].shader);
    }
    glLinkProgram(_program);
}

void ShaderProgram::Activate() {
    glUseProgram(_program);
}

void ShaderProgram::Deactivate() {
    glUseProgram(0);
}
