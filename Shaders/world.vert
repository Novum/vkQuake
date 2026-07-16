#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

#include "globals.inc"

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	float alpha;
	uint  instance_base; // 0: identity transforms, else buffer base offset + 1
}
push_constants;

layout (std430, set = 4, binding = 0) restrict readonly buffer vertex_instances_buffer
{
	uint vertex_instances[];
};
layout (std430, set = 4, binding = 1) restrict readonly buffer instances_buffer
{
	bmodel_instance_t bmodel_instances[];
};

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec2 in_texcoord1;
layout (location = 2) in vec2 in_texcoord2;

layout (location = 0) out vec4 out_texcoords;
layout (location = 1) out float out_fog_frag_coord;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main ()
{
	out_texcoords.xy = in_texcoord1.xy;
	out_texcoords.zw = in_texcoord2.xy;

	vec3 position = in_position;
	if (push_constants.instance_base != 0)
	{
		const uint instance_index = vertex_instances[gl_VertexIndex];
		if (instance_index != 0)
		{
			const bmodel_instance_t instance = bmodel_instances[push_constants.instance_base - 1 + instance_index];
			const vec4 model_pos = vec4 (in_position, 1.0f);
			position = vec3 (dot (instance.transform[0], model_pos), dot (instance.transform[1], model_pos), dot (instance.transform[2], model_pos));
		}
	}
	gl_Position = push_constants.mvp * vec4 (position, 1.0f);

	out_fog_frag_coord = gl_Position.w;
}
