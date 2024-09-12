#include "framebuffer_desc.hpp"

#include "log/log.hpp"


GLuint glxMakeFramebuffer(FramebufferDesc* set, GLuint depth_component, const std::initializer_list<GLuint>& color_components) {
	if (color_components.size() != set->texture_index_map.size()) {
		LOG_ERR("gl/fbo", "makeFbo(): incorrect amount of color components supplied");
		return 0;
	}

	std::vector<GLuint> color_comps = color_components;
	
	GLint max_draw_buffers = 0;
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);

	if (set->texture_index_map.size() > max_draw_buffers) {
		LOG_ERR("gl/fbo", "FramebufferDesc output texture count exceeds limit of " << max_draw_buffers);
		return 0;
	}

    std::vector<GLenum> draw_buffers(max_draw_buffers);
    std::fill(draw_buffers.begin(), draw_buffers.end(), GL_NONE);

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    for (const auto& kv : set->texture_index_map) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + kv.second, GL_TEXTURE_2D, color_comps[kv.second], 0);
        draw_buffers[kv.second] = GL_COLOR_ATTACHMENT0 + kv.second;
    }
    if (depth_component) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_component, 0);
    }
    glDrawBuffers(max_draw_buffers, draw_buffers.data());
    if (!glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERR("gl/framebuffer", "Framebuffer is incomplete");
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return fbo;
}
