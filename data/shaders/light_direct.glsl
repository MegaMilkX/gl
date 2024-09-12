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
uniform sampler2D texWorldPos;
uniform sampler2D texNormal;
uniform sampler2D texRoughness;
uniform sampler2D texMetallic;
in vec2 fragUV;
out vec4 outLightness;

#include "uniform_blocks/common.glsl"

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
	float a      = roughness*roughness;
	float a2     = a*a;
	float NdotH  = max(dot(N, H), 0.0);
	float NdotH2 = NdotH*NdotH;

	float num   = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return num / denom;
}
float GeometrySchlickGGX(float NdotV, float roughness) {
	float r = (roughness + 1.0);
	float k = (r*r) / 8.0;

	float num   = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2  = GeometrySchlickGGX(NdotV, roughness);
	float ggx1  = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 addDirectLight(
	vec3 camPos,
	vec3 lightDir,
	vec3 lightColor,
	float lightIntensity,
	vec3 WorldPos,
	vec3 albedo,
	vec3 N,
	float metallic,
	float roughness
){
	vec3 V = normalize(camPos - WorldPos);

	vec3 F0 = vec3(.04);
	F0 = mix(F0, albedo, metallic);

	vec3 L = -lightDir;
	vec3 H = normalize(V + L);
	float attenuation = 1.0;
	vec3 radiance = lightColor * lightIntensity * attenuation;

	float NDF = DistributionGGX(N, H, roughness);
	float G = GeometrySmith(N, V, L, roughness);
	vec3 F = fresnelSchlick(max(dot(H, V), .0), F0);

	vec3 kS = F;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - metallic;

	vec3 numerator = NDF * G * F;
	float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
	vec3 specular = numerator / denominator;

	float NdotL = max(dot(N, L), 0.0);
	return (kD * albedo / PI + specular) * radiance * NdotL;
}

void main() {
	float gamma = 2.2;
	vec3 albedo = pow(texture(texDiffuse, fragUV).xyz, vec3(gamma));
	float metallic = texture(texMetallic, fragUV).x;
	float roughness = texture(texRoughness, fragUV).x;
	vec3 N = (texture(texNormal, fragUV).xyz - .5) * 2.0;
	vec3 worldPos = texture(texWorldPos, fragUV).xyz;

	vec3 Lo = addDirectLight(cameraPosition, vec3(-1, -1, -1), vec3(1, 1, 1), 2,
		worldPos, albedo, N, metallic, roughness
	);
	outLightness = vec4(Lo, 1.0);
}
		