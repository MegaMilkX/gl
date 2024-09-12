#pragma once

#include <string>
#include <map>
#include <vector>
#include "platform/win32/gl/glextutil.h"
#include "log/log.hpp"
#include "framebuffer_desc.hpp"


class ShaderProgram;
ShaderProgram* loadShaderProgram(const char* filename, FramebufferDesc* output_textures);


class ShaderProgram {
	GLuint progid;
	std::vector<std::string> sampler_names;
public:
	ShaderProgram();
	~ShaderProgram();

	bool _load(const char* filename, FramebufferDesc* output_textures);

	int samplerCount() const;
	const char* getSamplerName(int i) const;
	int getSamplerIndex(const char* name) const;

	GLuint id() const;
};
