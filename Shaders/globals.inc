struct world_vert_t
{
	float x;
	float y;
	float z;
	float u;
	float v;
	float s;
	float t;
};

struct surface_t
{
	uint  packed_lightstyles;
	float normal_x;
	float normal_y;
	float normal_z;
	float dist;
	uint  packed_light_st;
	uint  packed_tex_edgecount;
	uint  vbo_offset;
	vec4  vecs[2];
};

struct draw_indirect_command_t
{
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int	 vertexOffset;
	uint firstInstance;
};
