#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform PushConsts {
	float gamma;
} push_constants;

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput color_input;

layout (location = 0) out vec4 out_frag_color;

void main() 
{
	out_frag_color = vec4(pow(subpassLoad(color_input).rgb, vec3(push_constants.gamma)), 1.0);
}
