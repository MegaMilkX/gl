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

layout(std140) uniform ubCommon {
	mat4 matProjection;
	mat4 matView;
	vec3 cameraPosition;
	float time; 
	vec2 viewportSize;
	float zNear;
	float zFar;
};

void main() {
	float depth = texture(texDiffuse, fragUV).x;
	vec4 screenPos = vec4(gl_FragCoord.x / viewportSize.x, gl_FragCoord.y / viewportSize.y, depth, 1.0) * 2.0 - 1.0;
	vec4 viewPosition = inverse(matProjection) * screenPos;
	float zDistance = (-viewPosition.z / viewPosition.w);
	float value = zDistance / (zFar - zNear);

	outAlbedo = vec4(vec3(value), 1.0);
}
