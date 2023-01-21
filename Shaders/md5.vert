#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout (push_constant) uniform PushConsts
{
	mat4  view_projection_matrix;
	vec3  fog_color;
	float fog_density;
}
push_constants;

layout (set = 2, binding = 0) uniform UBO
{
	mat4  model_matrix;
	vec3  shade_vector;
	float blend_factor;
	vec3  light_color;
	float entalpha;
	uint  flags;
	uint  joints_offset0;
	uint  joints_offset1;
}
ubo;

layout (std430, set = 3, binding = 0) restrict readonly buffer joints_buffer
{
	float joint_mats[];
};

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;
layout (location = 3) in vec4 in_joint_weights;
layout (location = 4) in uvec4 in_joint_indices;

layout (location = 0) out vec2 out_texcoord;
layout (location = 1) out vec4 out_color;
layout (location = 2) out float out_fog_frag_coord;

out gl_PerVertex
{
	vec4 gl_Position;
};

float r_avertexnormal_dot (vec3 vertexnormal) // from MH
{
	const float dot = dot (vertexnormal, ubo.shade_vector);
	// wtf - this reproduces anorm_dots within as reasonable a degree of tolerance as the >= 0 case
	if (dot < 0.0)
		return 1.0 + dot * (13.0 / 44.0);
	else
		return 1.0 + dot;
}

void main ()
{
	out_texcoord = in_texcoord;

	const vec3 xyz = in_position.xyz;

	vec3 skinned_positions[2] = {vec3 (0.0f), vec3 (0.0f)};
	uint joint_offsets[2] = {ubo.joints_offset0, ubo.joints_offset1};
	for (int j = 0; j < 2; ++j)
	{
		const uint joints_offset = joint_offsets[j];
		for (int i = 0; i < 4; ++i)
		{
			const uint	joint_index = in_joint_indices[i];
			const float joint_weight = in_joint_weights[i];
			float		joint_mat[12];
			for (int k = 0; k < 12; ++k)
				joint_mat[k] = joint_mats[((joints_offset + joint_index) * 12) + k];
			float	   y = (xyz[0] * joint_mat[4] + xyz[1] * joint_mat[5] + xyz[2] * joint_mat[6] + joint_mat[7]);
			float	   z = (xyz[0] * joint_mat[8] + xyz[1] * joint_mat[9] + xyz[2] * joint_mat[10] + joint_mat[11]);
			float	   x = (xyz[0] * joint_mat[0] + xyz[1] * joint_mat[1] + xyz[2] * joint_mat[2] + joint_mat[3]);
			const vec3 skinned_pos = vec3 (x, y, z);
			skinned_positions[j] += joint_weight * skinned_pos;
		}
	}

	const vec4 lerped_position = vec4 (mix (skinned_positions[0], skinned_positions[1], ubo.blend_factor), 1.0f);
	const vec4 model_space_position = ubo.model_matrix * lerped_position;
	gl_Position = push_constants.view_projection_matrix * model_space_position;

	if ((ubo.flags & 0x2) == 0)
	{
		const float dot = r_avertexnormal_dot (in_normal);
		out_color = vec4 (ubo.light_color * dot, 1.0);
	}
	else
		out_color = vec4 (ubo.light_color, 1.0f);

	out_fog_frag_coord = gl_Position.w;
}