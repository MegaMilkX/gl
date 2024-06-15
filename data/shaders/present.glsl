#vertex
#version 460
layout(location = 0) in vec3 inPosition;
out vec2 fragUV;
void main() {
	fragUV = vec2((inPosition.x + 1.0) * .5, (inPosition.y + 1.0) * .5);
	gl_Position = vec4(inPosition, 1.0);
}

#fragment
#version 460
uniform sampler2D texDiffuse;
in vec2 fragUV;
out vec4 outAlbedo;
void main() {
	outAlbedo = vec4(texture(texDiffuse, fragUV).xyz, 1.0);
}
