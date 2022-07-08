#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	float gamma;
	float contrast;
}
push_constants;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput color_input;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec3 frag = subpassLoad (color_input).rgb;
	frag.rgb = frag.rgb * push_constants.contrast;
	out_frag_color = vec4 (pow (frag, vec3 (push_constants.gamma)), 1.0);
}
