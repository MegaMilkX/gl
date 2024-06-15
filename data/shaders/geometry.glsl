#vertex
#version 460
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inUV;
layout(location = 5) in vec4 inColor;
out vec3 fragNormal;
out vec2 fragUV;
out vec4 fragColor;
out vec3 fragWorldPos;
out mat3 fragTBN;

layout(std140) uniform ubCommon {
	mat4 matProjection;
	mat4 matView;
	vec3 cameraPosition;
	float reserved;
	vec2 viewportSize;
	float zNear;
	float zFar;
};
layout(std140) uniform ubModel {
	mat4 matModel;
};

void main() {
	vec3 T = normalize(vec3(matModel * vec4(inTangent, 0.0)));
	vec3 B = normalize(vec3(matModel * vec4(inBitangent, 0.0)));
	vec3 N = normalize(vec3(matModel * vec4(inNormal, 0.0)));
	fragTBN = mat3(T, B, N);

	fragNormal = (matModel * vec4(inNormal, 0.0)).xyz;
	fragUV = inUV;
	fragColor = inColor;
	vec4 WP = matModel * vec4(inPosition, 1.0);
	fragWorldPos = WP.xyz;
	gl_Position = matProjection * matView * WP;
}

#fragment
#version 460
in vec3 fragNormal;
in vec2 fragUV;
in vec4 fragColor;
in vec3 fragWorldPos;
in mat3 fragTBN;

uniform sampler2D texDiffuse;
uniform sampler2D texNormal;
uniform sampler2D texRoughness;
uniform sampler2D texMetallic;
uniform sampler2D texEmission;

out vec4 outAlbedo;
out vec4 outNormal;
out vec4 outWorldPos;
out vec4 outRoughness;
out vec4 outMetallic;
out vec4 outEmission;

void main() {
	vec3 N = normalize(fragNormal);
	vec4 diffuse = texture(texDiffuse, fragUV);
	vec3 normal = texture(texNormal, fragUV).xyz;
	normal = normal * 2.0 - 1.0;
	normal = normalize(fragTBN * normal);
	float roughness = texture(texRoughness, fragUV).x;
	float metallic = texture(texMetallic, fragUV).x;            
	float emission = texture(texEmission, fragUV).x;

	vec3 color = diffuse.xyz;// * fragColor.xyz;
	float alpha = diffuse.a * fragColor.a;

	outAlbedo = vec4(color, alpha);
	outNormal = vec4((normal + 1.0) / 2.0, 1.0);//vec4((N + 1.0) / 2.0, 1.0);
	outWorldPos = vec4(fragWorldPos, 1.0);
	outRoughness = vec4(roughness, 0.0, 0.0, 1.0);
	outMetallic = vec4(metallic, 0.0, 0.0, 1.0);
	outEmission = vec4(emission, 0.0, 0.0, 1.0);
}
