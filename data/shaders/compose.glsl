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
uniform sampler2D texNormal;
uniform sampler2D texLightness;
uniform sampler2D texEmission;
uniform sampler2D texDepth;
in vec2 fragUV;
out vec4 outFinal;

#include "uniform_blocks/common.glsl"

void main() {
	float gamma = 2.2;
	vec3 albedo = pow(texture(texDiffuse, fragUV).xyz, vec3(gamma));
	vec3 N = (texture(texNormal, fragUV).xyz - .5) * 2.0;
	vec3 Lo = texture(texLightness, fragUV).xyz;
	vec3 emission = texture(texEmission, fragUV).xyz;

	vec3 ambient = vec3(0);//vec3(.03) * albedo;
	vec3 color = ambient + Lo;//mix(ambient + Lo, ambient + albedo * emission.x, emission);
	
	// Fog
	float depth = texture(texDepth, fragUV).x;
	vec4 screenPos = vec4(gl_FragCoord.x / viewportSize.x, gl_FragCoord.y / viewportSize.y, depth, 1.0) * 2.0 - 1.0;
	vec4 viewPosition = inverse(matProjection) * screenPos;
	float zDistance = (-viewPosition.z / viewPosition.w);
	float fogStrength = clamp(zDistance / 5.0, 0.0, 1.0);

	// Gamma correction
	color = color / (color + vec3(1.0));
	color = pow(color, vec3(1.0/gamma));

	//color = mix(color, vec3(0), fogStrength);

	outFinal = vec4(color, 1.0);
}