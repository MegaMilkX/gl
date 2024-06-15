#pragma once

#include <map>
#include <string>
#include "platform/win32/gl/glextutil.h"
#include "math/gfxm.hpp"
#include "log/log.hpp"
#include "shader_program.hpp"


constexpr int MATERIAL_MAX_TEXTURES = 16;

struct SamplerArray {
	struct BIND_DATA {
		GLenum target;
		GLuint texture;
	};

	BIND_DATA textures[MATERIAL_MAX_TEXTURES];
	int texture_count;
};


struct SamplerSet {
	struct TEXTURE_DATA {
		GLenum target;
		GLuint texture;
	};
	std::map<std::string, TEXTURE_DATA> texture_map;

	SamplerSet& setSampler(const char* name, GLenum target, GLuint texture) {
		texture_map[name].texture = texture;
		texture_map[name].target = target;
		return *this;
	}
};