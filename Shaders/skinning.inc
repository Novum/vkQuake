// Default MD5 path uses md5vert_t with four influence slots.
// The md5_8.vert and skinning_8.comp permutations enable EIGHT_WEIGHT_SKINNING
// and use md5vert8_t.
#if defined(EIGHT_WEIGHT_SKINNING)
#define MD5_INFLUENCE_COUNT		   8
#define MD5_INFLUENCE_GROUP_COUNT  2
#define MD5_VERTEX_STRIDE_FLOATS   36
#define MD5_JOINT_WEIGHTS0_FLOAT   8
#define MD5_JOINT_WEIGHTS1_FLOAT   9
#define MD5_JOINT_INDICES0_FLOAT   10
#define MD5_JOINT_INDICES1_FLOAT   11
#define MD5_JOINT_POSITION_X_FLOAT 12
#define MD5_JOINT_POSITION_Y_FLOAT 20
#define MD5_JOINT_POSITION_Z_FLOAT 28
#else
#define MD5_INFLUENCE_COUNT		   4
#define MD5_INFLUENCE_GROUP_COUNT  1
#define MD5_VERTEX_STRIDE_FLOATS   22
#define MD5_JOINT_WEIGHTS0_FLOAT   8
#define MD5_JOINT_INDICES0_FLOAT   9
#define MD5_JOINT_POSITION_X_FLOAT 10
#define MD5_JOINT_POSITION_Y_FLOAT 14
#define MD5_JOINT_POSITION_Z_FLOAT 18
#endif

void MD5_NormalizeJointWeights (inout vec4 joint_weights[MD5_INFLUENCE_GROUP_COUNT])
{
	float weight_sum = 0.0;
	for (int g = 0; g < MD5_INFLUENCE_GROUP_COUNT; ++g)
		weight_sum += dot (joint_weights[g], vec4 (1.0));

	if (weight_sum > 0.0)
	{
		const float weight_scale = 1.0 / weight_sum;
		for (int g = 0; g < MD5_INFLUENCE_GROUP_COUNT; ++g)
			joint_weights[g] *= weight_scale;
	}
}

void MD5_LoadJointPositions (
	vec4 joint_position_x[MD5_INFLUENCE_GROUP_COUNT], vec4 joint_position_y[MD5_INFLUENCE_GROUP_COUNT], vec4 joint_position_z[MD5_INFLUENCE_GROUP_COUNT],
	vec4 joint_weights[MD5_INFLUENCE_GROUP_COUNT], out vec4 joint_positions[MD5_INFLUENCE_COUNT])
{
	for (int g = 0; g < MD5_INFLUENCE_GROUP_COUNT; ++g)
	{
		for (int i = 0; i < 4; ++i)
		{
			const int j = g * 4 + i;
			joint_positions[j] = vec4 (joint_position_x[g][i], joint_position_y[g][i], joint_position_z[g][i], joint_weights[g][i]);
		}
	}
}

vec3 MD5_TransformPosition (mat4x3 joint_mat, vec4 joint_position)
{
	return (joint_mat * joint_position).xyz;
}

vec3 MD5_TransformNormal (mat4x3 joint_mat, vec3 normal, float weight)
{
	return weight * (mat3x3 (joint_mat) * normal).xyz;
}

void MD5_SkinFrames (
	uint joint_offsets[2], uvec4 joint_indices[MD5_INFLUENCE_GROUP_COUNT], vec4 joint_weights[MD5_INFLUENCE_GROUP_COUNT],
	vec4 joint_positions[MD5_INFLUENCE_COUNT], vec3 normal, bool skin_normals, out vec3 skinned_positions[2], out vec3 skinned_normals[2])
{
	skinned_positions[0] = vec3 (0.0);
	skinned_positions[1] = vec3 (0.0);
	skinned_normals[0] = vec3 (0.0);
	skinned_normals[1] = vec3 (0.0);

	for (int j = 0; j < 2; ++j)
	{
		const uint joints_offset = joint_offsets[j];
		for (int g = 0; g < MD5_INFLUENCE_GROUP_COUNT; ++g)
		{
			for (int i = 0; i < 4; ++i)
			{
				const uint	 joint_index = joint_indices[g][i];
				const mat4x3 joint_mat = MD5_LoadJointMatrix (joints_offset + joint_index);
				skinned_positions[j] += MD5_TransformPosition (joint_mat, joint_positions[g * 4 + i]);
				if (skin_normals)
				{
					const float joint_weight = joint_weights[g][i];
					skinned_normals[j] += MD5_TransformNormal (joint_mat, normal, joint_weight);
				}
			}
		}
	}
}
