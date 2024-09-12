#pragma once

#include <string>
#include <map>
#include "platform/win32/gl/glextutil.h"


struct FramebufferDesc {
	std::map<std::string, int> texture_index_map;

	FramebufferDesc() {}
	FramebufferDesc(std::initializer_list<std::string> color_targets) {
		int idx = 0;
		for (const auto& tgt : color_targets) {
			texture_index_map[tgt] = idx;
			++idx;
		}
	}
};

GLuint glxMakeFramebuffer(FramebufferDesc* set, GLuint depth_component, const std::initializer_list<GLuint>& color_components);
