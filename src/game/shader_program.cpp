#include "shader_program.hpp"

#include <assert.h>
#include <vector>
#include "log/log.hpp"
#include "profiler/profiler.hpp"
#include "filesystem/filesystem.hpp"


enum SHADER_TYPE { SHADER_UNKNOWN, SHADER_VERTEX, SHADER_FRAGMENT };
struct SHADER_PART {
    SHADER_TYPE type;
    const char* begin;
    const char* end;
};


ShaderProgram::ShaderProgram()
: progid(0) {}
ShaderProgram::~ShaderProgram() {
    glDeleteProgram(progid);
}

static bool glxLinkProgram(GLuint progid);
static GLuint glxLoadShaderProgram(const char* filename);

bool ShaderProgram::_load(const char* filename, ColorOutputSet* output_textures) {
    sampler_names.clear();

    if (progid) {
        glDeleteProgram(progid);
    }

    progid = glxLoadShaderProgram(filename);
    if (progid == 0) {
        return false;
    }

    // Prepare shader program
    
    // Set fragment output locations
    {
        if (output_textures) {
            GLint count = 0;
            int name_len = 0;
            const int NAME_MAX_LEN = 64;
            char name[NAME_MAX_LEN];
            glGetProgramInterfaceiv(progid, GL_PROGRAM_OUTPUT, GL_ACTIVE_RESOURCES, &count);
            for (int i = 0; i < count; ++i) {
                glGetProgramResourceName(progid, GL_PROGRAM_OUTPUT, i, NAME_MAX_LEN, &name_len, name);
                assert(name_len < NAME_MAX_LEN);
                std::string output_name(name, name + name_len);

                auto it = output_textures->texture_map.find(output_name);
                if (it == output_textures->texture_map.end()) {
                    LOG_WARN("gl/shader", "ColorOutputSet does not provide a color output " << output_name);
                    continue;
                }
                glBindFragDataLocation(progid, it->second.index, output_name.c_str());
            }
        } else {
            GLint count = 0;
            int name_len = 0;
            const int NAME_MAX_LEN = 64;
            char name[NAME_MAX_LEN];
            glGetProgramInterfaceiv(progid, GL_PROGRAM_OUTPUT, GL_ACTIVE_RESOURCES, &count);
            for (int i = 0; i < count; ++i) {
                glGetProgramResourceName(progid, GL_PROGRAM_OUTPUT, i, NAME_MAX_LEN, &name_len, name);
                assert(name_len < NAME_MAX_LEN);
                std::string output_name(name, name + name_len);

                glBindFragDataLocation(progid, i, output_name.c_str());
            }
        }
        
        // Need to link again for glBindFragDataLocation to take effect
        if (!glxLinkProgram(progid)) {
            return false;
        }
        /*
        glBindFragDataLocation(progid, 0, "outAlbedo");
        glBindFragDataLocation(progid, 1, "outNormal");
        glBindFragDataLocation(progid, 2, "outWorldPos");
        glBindFragDataLocation(progid, 3, "outRoughness");
        glBindFragDataLocation(progid, 4, "outMetallic");
        glBindFragDataLocation(progid, 5, "outEmission");
        glBindFragDataLocation(progid, 6, "outLightness");
        glBindFragDataLocation(progid, 7, "outFinal");*/
    }

    // Uniform buffers
    {
        GLuint block_index = glGetUniformBlockIndex(progid, "ubCommon");
        if (block_index != GL_INVALID_INDEX) {
            glUniformBlockBinding(progid, block_index, 0);
        }
        block_index = glGetUniformBlockIndex(progid, "ubModel");
        if (block_index != GL_INVALID_INDEX) {
            glUniformBlockBinding(progid, block_index, 1);
        }
    }

    // Uniforms/samplers
    {
        glUseProgram(progid);
        GLint count = 0;
        glGetProgramiv(progid, GL_ACTIVE_UNIFORMS, &count);
        int sampler_index = 0;
        for (int i = 0; i < count ; ++i) {
            const GLsizei bufSize = 64;
            GLchar name[bufSize] = {};
            GLsizei name_len;
            GLint size;
            GLenum type;
            glGetActiveUniform(progid, (GLuint)i, bufSize, &name_len, &size, &type, name);
            std::string uniform_name(name, name + name_len);

            switch (type) {
            case GL_SAMPLER_1D:
            //case GL_SAMPLER_1D_ARB:
            case GL_SAMPLER_2D:
            //case GL_SAMPLER_2D_ARB:
            case GL_SAMPLER_3D:
            //case GL_SAMPLER_3D_ARB:
            case GL_SAMPLER_CUBE:
            //case GL_SAMPLER_CUBE_ARB:
            case GL_SAMPLER_1D_SHADOW:
            //case GL_SAMPLER_1D_SHADOW_ARB:
            case GL_SAMPLER_2D_SHADOW:
            //case GL_SAMPLER_2D_SHADOW_ARB:
            case GL_SAMPLER_1D_ARRAY:
            case GL_SAMPLER_2D_ARRAY:
            case GL_SAMPLER_1D_ARRAY_SHADOW:
            case GL_SAMPLER_2D_ARRAY_SHADOW:
            case GL_SAMPLER_2D_MULTISAMPLE:
            case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
            case GL_SAMPLER_CUBE_SHADOW:
            case GL_SAMPLER_BUFFER:
            case GL_SAMPLER_2D_RECT:
            //case GL_SAMPLER_2D_RECT_ARB:
            case GL_SAMPLER_2D_RECT_SHADOW:
            //case GL_SAMPLER_2D_RECT_SHADOW_ARB:
            case GL_INT_SAMPLER_1D:
            case GL_INT_SAMPLER_2D:
            case GL_INT_SAMPLER_3D:
            case GL_INT_SAMPLER_CUBE:
            case GL_INT_SAMPLER_1D_ARRAY:
            case GL_INT_SAMPLER_2D_ARRAY:
            case GL_INT_SAMPLER_2D_MULTISAMPLE:
            case GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
            case GL_INT_SAMPLER_BUFFER:
            case GL_INT_SAMPLER_2D_RECT:
            case GL_UNSIGNED_INT_SAMPLER_1D:
            case GL_UNSIGNED_INT_SAMPLER_2D:
            case GL_UNSIGNED_INT_SAMPLER_3D:
            case GL_UNSIGNED_INT_SAMPLER_CUBE:
            case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY:
            case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
            case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE:
            case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
            case GL_UNSIGNED_INT_SAMPLER_BUFFER:
            case GL_UNSIGNED_INT_SAMPLER_2D_RECT:
                sampler_names.push_back(uniform_name);
                GLint loc = glGetUniformLocation(progid, uniform_name.c_str());
                glUniform1i(loc, sampler_index++);
                break;
            };
        }
        glUseProgram(0);
    }

    return true;
}

int ShaderProgram::samplerCount() const {
    return sampler_names.size();
}
const char* ShaderProgram::getSamplerName(int i) const {
    return sampler_names[i].c_str();
}
int ShaderProgram::getSamplerIndex(const char* name) const {
    for (int i = 0; i < sampler_names.size(); ++i) {
        if (sampler_names[i] == name) {
            return i;
        }
    }
    return -1;
}

GLuint ShaderProgram::id() const {
    return progid;
}

static GLenum glxShaderTypeToGlEnum(SHADER_TYPE type) {
    switch (type)
    {
    case SHADER_VERTEX:
        return GL_VERTEX_SHADER;
    case SHADER_FRAGMENT:
        return GL_FRAGMENT_SHADER;
    default:
        return 0;
    }
}

static void glxShaderSource(GLuint shader, const char* string, int len = 0) {
    glShaderSource(shader, 1, &string, len == 0 ? 0 : &len);
}

static bool glxCompileShader(GLuint shader) {
    PROF_SCOPE_FN();

    glCompileShader(shader);
    GLint res = GL_FALSE;
    int infoLogLen;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &res);
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLen);
    if(infoLogLen > 1)
    {
        std::vector<char> errMsg(infoLogLen + 1);
        glGetShaderInfoLog(shader, infoLogLen, NULL, &errMsg[0]);
        LOG_ERR("glsl", "GLSL compile: " << &errMsg[0]);
    }
    if(res == GL_FALSE)
        return false;
    return true;
}

static bool glxLinkProgram(GLuint progid) {
    PROF_SCOPE_FN();

    GL_CHECK(glLinkProgram(progid));
    GLint res = GL_FALSE;
    int infoLogLen;
    glGetProgramiv(progid, GL_LINK_STATUS, &res);
    glGetProgramiv(progid, GL_INFO_LOG_LENGTH, &infoLogLen);
    if (infoLogLen > 1)
    {
        std::vector<char> errMsg(infoLogLen + 1);
        glGetProgramInfoLog(progid, infoLogLen, NULL, &errMsg[0]);
        LOG_ERR("glsl", "GLSL link: " << &errMsg[0]);
    }
    if (res != GL_TRUE) {
        LOG_ERR("glsl", "Shader program failed to link");
        return false;
    }
    return true;
}

static GLuint glxCreateShaderProgram(SHADER_PART* parts, size_t count) {
    std::vector<GLuint> shaders;
    shaders.resize(count);
    for (int i = 0; i < count; ++i) {
        GLuint id = glCreateShader(glxShaderTypeToGlEnum(parts[i].type));
        shaders[i] = id;
        glxShaderSource(id, parts[i].begin, (size_t)(parts[i].end - parts[i].begin));
        if (!glxCompileShader(id)) {
            glDeleteShader(id);
            return 0;
        }
    }

    auto fnDeleteShaders = [&shaders, count]() {
        for (int i = 0; i < count; ++i) {
            glDeleteShader(shaders[i]);
        }
    };

    GLuint progid = glCreateProgram();
    for (int i = 0; i < count; ++i) {
        glAttachShader(progid, shaders[i]);
    }

    if (!glxLinkProgram(progid)) {
        glDeleteProgram(progid);
        fnDeleteShaders();
        return 0;
    }

    fnDeleteShaders();

    //prepareShaderProgram(progid);
    return progid;
}

static GLuint glxLoadShaderProgram(const char* filename) {
    std::string src = fsSlurpTextFile(filename);
    if (src.empty()) {
        LOG_ERR("gl/shader", "Failed to open shader source file " << filename);
        return 0;
    }

    std::vector<SHADER_PART> parts;
    {
        const char* str = src.data();
        size_t len = src.size();

        SHADER_PART part = { SHADER_UNKNOWN, 0, 0 };
        for (int i = 0; i < len; ++i) {
            char ch = str[i];
            if (isspace(ch)) {
                continue;
            }
            if (ch == '#') {
                const char* tok = str + i;
                int tok_len = 0;
                for (int j = i; j < len; ++j) {
                    ch = str[j];
                    if (isspace(ch)) {
                        break;
                    }
                    tok_len++;
                }
                if (strncmp("#vertex", tok, tok_len) == 0) {
                    if (part.type != SHADER_UNKNOWN) {
                        part.end = str + i;
                        parts.push_back(part);
                    }
                    part.type = SHADER_VERTEX;
                } else if(strncmp("#fragment", tok, tok_len) == 0) {
                    if (part.type != SHADER_UNKNOWN) {
                        part.end = str + i;
                        parts.push_back(part);
                    }
                    part.type = SHADER_FRAGMENT;
                } else {
                    continue;
                }
                i += tok_len;
                for (; i < len; ++i) {
                    ch = str[i];
                    if (ch == '\n') {
                        ++i;
                        break;
                    }
                }
                part.begin = str + i;
            }
        }
        if (part.type != SHADER_UNKNOWN) {
            part.end = str + len;
            parts.push_back(part);
        }
    }

    return glxCreateShaderProgram(parts.data(), parts.size());
}


ShaderProgram* loadShaderProgram(const char* filename, ColorOutputSet* output_textures) {
    auto psp = new ShaderProgram;
    if (!psp->_load(filename, output_textures)) {
        return 0;
    }
    return psp;
}