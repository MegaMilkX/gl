#vertex
#version 460 
in vec3 inPosition;
out vec3 frag_cube_vec;

layout(std140) uniform ubCommon {
	mat4 matProjection;
	mat4 matView;
	vec3 cameraPosition;
	float time;
	vec2 viewportSize;
	float zNear;
	float zFar;
};

void main(){
	frag_cube_vec = vec3(inPosition.x, inPosition.y, -inPosition.z);
	mat4 view = matView;
	view[3] = vec4(0,0,0,1);
	vec4 pos = matProjection * view * vec4(inPosition, 1.0);
	gl_Position = pos.xyww;
}

#fragment
#version 460
in vec3 frag_cube_vec;
uniform samplerCube cubemapEnvironment;
out vec4 outFinal;

layout(std140) uniform ubCommon {
	mat4 matProjection;
	mat4 matView;
	vec3 cameraPosition;
	float time;
	vec2 viewportSize;
	float zNear;
	float zFar;
};

void main(){
	float gamma = 2.2;

	vec3 color = textureLod(cubemapEnvironment, (frag_cube_vec), 0/*(cos(time) + 1.0) * 2.0*/).xyz;
	
	// Gamma correction
	color = color / (color + vec3(1.0));
	color = pow(color, vec3(1.0/gamma));

	outFinal = vec4(color.xyz, 1.0);
}
