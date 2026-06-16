#include "shader.hpp"
#include "file.hpp"

#ifdef __EMSCRIPTEN__
#include <regex>

static std::string PreprocessShaderForWebGL(std::string src, ShaderType type) {
    // Replace '#version ...' with '#version 300 es'
    std::regex versionRegex(R"(#version\s+[0-9]+(?:\s+core)?)");
    src = std::regex_replace(src, versionRegex, "#version 300 es");

    // Insert precision qualifiers for fragment shaders if not present
    if (type == ShaderType::FRAGMENT) {
        // Strip layout(location = ...) from fragment shader inputs (in)
        std::regex fragInputLayoutRegex(R"(layout\s*\(\s*location\s*=\s*[0-9]+\s*\)\s*in\b)");
        src = std::regex_replace(src, fragInputLayoutRegex, "in");

        if (src.find("precision ") == std::string::npos) {
            size_t pos = src.find("#version 300 es");
            if (pos != std::string::npos) {
                size_t lineEnd = src.find('\n', pos);
                if (lineEnd != std::string::npos) {
                    src.insert(lineEnd + 1, "\nprecision highp float;\nprecision highp int;\n");
                } else {
                    src += "\nprecision highp float;\nprecision highp int;\n";
                }
            } else {
                src = "precision highp float;\nprecision highp int;\n" + src;
            }
        }
    } else if (type == ShaderType::VERTEX) {
        // Transform instanced matrix attribute to uniform for non-instanced WebGL 2.0 fallback
        std::regex worldAttrRegex(R"(layout\s*\(\s*location\s*=\s*5\s*\)\s*in\s+mat4\s+World\s*;)");
        src = std::regex_replace(src, worldAttrRegex, "uniform mat4 World;");
    }
    return src;
}
#endif

Shader::Shader(const std::string& path, ShaderType type) {
    std::string shaderSrc = File(path).GetContent();
#ifdef __EMSCRIPTEN__
    shaderSrc = PreprocessShaderForWebGL(shaderSrc, type);
#endif
    const char* src = shaderSrc.c_str();
    const int len = shaderSrc.size();

    shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, &len);

    glCompileShader(shader);
    GLint isCompiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE) {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

        GLchar* log = new GLchar[maxLength];
        glGetShaderInfoLog(shader, maxLength, &maxLength, log);

        throw std::runtime_error(fmt::format("Shader error: {}\n", (char*)log));
    }
}

ShaderProgram::ShaderProgram(const ShaderProgramProps& props) : _program(glCreateProgram()) {
    glAttachShader(_program, Shader(props.vert, ShaderType::VERTEX).shader);
    glAttachShader(_program, Shader(props.frag, ShaderType::FRAGMENT).shader);
#ifndef __EMSCRIPTEN__
    if (props.tesc.has_value()) {
        glAttachShader(_program, Shader(props.tesc.value(), ShaderType::TESS_CONTROL).shader);
    }
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

    GLint isLinked;
    glGetProgramiv(_program, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_FALSE) {
        GLint maxLength = 0;
        glGetProgramiv(_program, GL_INFO_LOG_LENGTH, &maxLength);

        std::vector<GLchar> infoLog(maxLength);
        glGetProgramInfoLog(_program, maxLength, &maxLength, &infoLog[0]);

        glDeleteProgram(_program);

        throw std::runtime_error(fmt::format("Shader link error: {}", infoLog.data()));
    }
}

ShaderProgram::ShaderProgram(
  std::string vert, std::string frag, std::optional<std::string> tesc, std::optional<std::string> tese
)
  : _program(glCreateProgram()) {
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

ShaderProgram::ShaderProgram(std::array<Shader, 2>& shaders) : _program(glCreateProgram()) {
    for (int i = shaders.size() - 1; i >= 0; i--) {
        glAttachShader(_program, shaders[i].shader);
    }
    glLinkProgram(_program);
}

ShaderProgram::ShaderProgram(std::array<Shader, 4>& shaders) : _program(glCreateProgram()) {
    for (int i = shaders.size() - 1; i >= 0; i--) {
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
