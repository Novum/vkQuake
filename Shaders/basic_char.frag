#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform PushConsts {
	mat4 mvp;
	vec3 fog_color;
	float fog_density;
} push_constants;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout (location = 0) in centroid vec4 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

void main() 
{
	out_frag_color = in_color * texture(tex, in_texcoord.xy);
	if(out_frag_color.a < 0.666f)
		discard;

	float fog = exp(-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
	fog = clamp(fog, 0.0, 1.0);
	out_frag_color = mix(vec4(push_constants.fog_color, 1.0f), out_frag_color, fog);
}
