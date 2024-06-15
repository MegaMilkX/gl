#pragma once

#include <string>
#include <map>
#include <vector>
#include "platform/win32/gl/glextutil.h"
#include "log/log.hpp"


struct ColorOutputSet {
	struct OUTPUT {
		GLuint texture;
		int index;
	};
	std::map<std::string, OUTPUT> texture_map;
	GLuint depth_component = 0;

	ColorOutputSet& color(const char* name, GLuint texture) {
		texture_map[name] = OUTPUT{ .texture = texture, .index = (int)texture_map.size() };
		return *this;
	}
	ColorOutputSet& depth(GLuint texture) {
		depth_component = texture;
		return *this;
	}	
};

inline GLuint makeFbo(ColorOutputSet* set) {
	GLint max_draw_buffers = 0;
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);

	if (set->texture_map.size() > max_draw_buffers) {
		LOG_ERR("gl/fbo", "ColorOutputSet output texture count exceeds limit of " << max_draw_buffers);
		return 0;
	}

    std::vector<GLenum> draw_buffers(max_draw_buffers);
    std::fill(draw_buffers.begin(), draw_buffers.end(), GL_NONE);

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    for (const auto& kv : set->texture_map) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + kv.second.index, GL_TEXTURE_2D, kv.second.texture, 0);
        draw_buffers[kv.second.index] = GL_COLOR_ATTACHMENT0 + kv.second.index;
    }
    if (set->depth_component) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, set->depth_component, 0);
    }
    glDrawBuffers(max_draw_buffers, draw_buffers.data());
    if (!glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERR("gl/framebuffer", "Framebuffer is incomplete");
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return fbo;
}


class ShaderProgram;
ShaderProgram* loadShaderProgram(const char* filename, ColorOutputSet* output_textures);


class ShaderProgram {
	GLuint progid;
	std::vector<std::string> sampler_names;
public:
	ShaderProgram();
	~ShaderProgram();

	bool _load(const char* filename, ColorOutputSet* output_textures);

	int samplerCount() const;
	const char* getSamplerName(int i) const;
	int getSamplerIndex(const char* name) const;

	GLuint id() const;
};
