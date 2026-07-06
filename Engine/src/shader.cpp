#include "shader.hpp"
#include "file.hpp"

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
#include <regex>

static std::string PreprocessShaderForWebGL(std::string src, ShaderType type) {
    // Replace '#version ...' with '#version 300 es'
    std::regex versionRegex(R"(#version\s+[0-9]+(?:\s+core)?)");
    src = std::regex_replace(src, versionRegex, "#version 300 es");

    // GLSL requires #version to be the literal first line. Desktop compilers
    // tolerate leading comments/blank lines before it, but ANGLE (WebGL) rejects
    // it outright and silently falls back to GLSL ES 1.00 defaults -- which then
    // cascades into unrelated-looking errors like "'in' supported in GLSL ES 3.00
    // only". If a source has header comments above #version, hoist the directive
    // to line 1.
    size_t versionPos = src.find("#version 300 es");
    if (versionPos != std::string::npos && versionPos != 0) {
        size_t lineEnd = src.find('\n', versionPos);
        size_t lineLen = (lineEnd == std::string::npos ? src.size() : lineEnd + 1) - versionPos;
        std::string versionLine = src.substr(versionPos, lineLen);
        src.erase(versionPos, lineLen);
        src = versionLine + src;
    }

    // Insert precision qualifiers for fragment shaders if not present
    if (type == ShaderType::Fragment) {
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
    } else if (type == ShaderType::Vertex) {
        // Transform instanced matrix attribute to uniform for non-instanced WebGL 2.0 fallback
        std::regex worldAttrRegex(R"(layout\s*\(\s*location\s*=\s*5\s*\)\s*in\s+mat4\s+World\s*;)");
        src = std::regex_replace(src, worldAttrRegex, "uniform mat4 World;");
    }
    return src;
}
#endif

static GLenum GetGLShaderType(ShaderType type) {
    switch (type) {
    case ShaderType::Vertex:
        return GL_VERTEX_SHADER;
    case ShaderType::Fragment:
        return GL_FRAGMENT_SHADER;
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    case ShaderType::TessControl:
        return GL_TESS_CONTROL_SHADER;
    case ShaderType::TessEvaluation:
        return GL_TESS_EVALUATION_SHADER;
#endif
    default:
        return GL_VERTEX_SHADER;
    }
}

Shader::Shader(const std::string& path, ShaderType type) {
    std::string shaderSrc = File(path).GetContent();
#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    shaderSrc = PreprocessShaderForWebGL(shaderSrc, type);
#endif
    const char* src = shaderSrc.c_str();
    const int len = shaderSrc.size();

    shader = glCreateShader(GetGLShaderType(type));
    glShaderSource(shader, 1, &src, &len);

    glCompileShader(shader);
    GLint isCompiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE) {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

        std::string log(maxLength, '\0');
        glGetShaderInfoLog(shader, maxLength, &maxLength, log.data());
        log.resize(maxLength);// drop the trailing '\0' GL didn't write into

        throw std::runtime_error(fmt::format("Shader error: {}\n", log));
    }
}

GLShaderProgram::GLShaderProgram(const ShaderProgramProps& props) : _program(glCreateProgram()) {
    glAttachShader(_program, Shader(props.vert, ShaderType::Vertex).shader);
    glAttachShader(_program, Shader(props.frag, ShaderType::Fragment).shader);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    if (props.tesc.has_value()) {
        glAttachShader(_program, Shader(props.tesc.value(), ShaderType::TessControl).shader);
    }
    if (props.tese.has_value()) {
        glAttachShader(_program, Shader(props.tese.value(), ShaderType::TessEvaluation).shader);
    }
#endif

    if (props.feedbackVaryings.has_value()) {
        std::vector<const char*> varyingsCStr;
        varyingsCStr.reserve(props.feedbackVaryings->size());
        for (const auto& varying : props.feedbackVaryings.value()) {
            varyingsCStr.push_back(varying.c_str());
        }
        glTransformFeedbackVaryings(_program, varyingsCStr.size(), varyingsCStr.data(), GL_INTERLEAVED_ATTRIBS);
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

GLShaderProgram::GLShaderProgram(
    std::string vert, std::string frag, std::optional<std::string> tesc, std::optional<std::string> tese
)
  : _program(glCreateProgram()) {
    glAttachShader(_program, Shader(vert, ShaderType::Vertex).shader);
    glAttachShader(_program, Shader(frag, ShaderType::Fragment).shader);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    if (tesc.has_value() && tese.has_value()) {
        glAttachShader(_program, Shader(tesc.value(), ShaderType::TessControl).shader);
        glAttachShader(_program, Shader(tese.value(), ShaderType::TessEvaluation).shader);
    }
#endif
    glLinkProgram(_program);
}

GLShaderProgram::~GLShaderProgram() {
    if (_program != 0) {
        glDeleteProgram(_program);
    }
}

void GLShaderProgram::Activate() {
    glUseProgram(_program);
}

void GLShaderProgram::Deactivate() {
    glUseProgram(0);
}

int GLShaderProgram::GetAttrib(const std::string& attrib) {
    return glGetAttribLocation(_program, attrib.c_str());
}

int GLShaderProgram::GetUniform(const std::string& uniform) {
    auto it = _uniformLocationCache.find(uniform);
    if (it != _uniformLocationCache.end()) {
        return it->second;
    }
    return CacheUniform(uniform.c_str());
}

void GLShaderProgram::SetUniform(const std::string& uniform, const glm::mat4& val) {
    glUniformMatrix4fv(GetUniform(uniform), 1, GL_FALSE, &val[0][0]);
}

void GLShaderProgram::SetUniform(const std::string& uniform, const glm::vec2& val) {
    glUniform2fv(GetUniform(uniform), 1, &val[0]);
}

void GLShaderProgram::SetUniform(const std::string& uniform, const glm::vec3& val) {
    glUniform3fv(GetUniform(uniform), 1, &val[0]);
}

void GLShaderProgram::SetUniform(const std::string& uniform, const glm::vec4& val) {
    glUniform4fv(GetUniform(uniform), 1, &val[0]);
}

void GLShaderProgram::SetUniform(const std::string& uniform, int val) {
    glUniform1i(GetUniform(uniform), val);
}

void GLShaderProgram::SetUniform(const std::string& uniform, float val) {
    glUniform1f(GetUniform(uniform), val);
}

int GLShaderProgram::CacheUniform(const std::string& uniform) {
    auto it = _uniformLocationCache.find(uniform);
    if (it != _uniformLocationCache.end()) {
        return it->second;
    }
    GLint location = glGetUniformLocation(_program, uniform.c_str());
    _uniformLocationCache[uniform] = location;
    return location;
}
