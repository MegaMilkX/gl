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
uniform sampler2D texWorldPos;
uniform sampler2D texRoughness;
uniform sampler2D texMetallic;
uniform samplerCube texCubemapIrradiance;
uniform samplerCube texCubemapSpecular;
uniform sampler2D texBrdfLut;

in vec2 fragUV;
out vec4 outLightness;

#include "uniform_blocks/common.glsl"

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    float gamma = 2.2;

    vec3 albedo = pow(texture(texDiffuse, fragUV).xyz, vec3(gamma));
    vec3 N = (texture(texNormal, fragUV).xyz - .5) * 2.0;
    vec3 worldPos = texture(texWorldPos, fragUV).xyz;
    float roughness = texture(texRoughness, fragUV).x;
    float metallic = texture(texMetallic, fragUV).x;

    vec3 V = normalize(cameraPosition - worldPos);
    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(texCubemapSpecular, R * vec3(1, 1, -1), roughness * MAX_REFLECTION_LOD).xyz;
    vec2 envBRDF = texture(texBrdfLut, vec2(max(dot(N, V), 0.0), roughness)).xy;
    vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    vec3 irradiance = texture(texCubemapIrradiance, N * vec3(1, 1, -1)).xyz;
    vec3 diffuse = irradiance * albedo;
    outLightness = vec4(kD * diffuse + specular, 1.0);
}