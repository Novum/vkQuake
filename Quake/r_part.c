/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"

#define MAX_PARTICLES \
	16384 // default max # of particles at one
		  //  time
#define ABSOLUTE_MIN_PARTICLES \
	512 // no fewer than this no matter what's
		//  on the command line

int ramp1[8] = {0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61};
int ramp2[8] = {0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66};
int ramp3[8] = {0x6d, 0x6b, 6, 5, 4, 3};

particle_t *active_particles, *free_particles, *particles;

vec3_t r_pright, r_pup, r_ppn;

int r_numparticles;

gltexture_t *particletexture, *particletexture1, *particletexture2, *particletexture3, *particletexture4; // johnfitz
float		 texturescalefactor; // johnfitz -- compensate for apparent size of different particle textures

cvar_t r_particles = {"r_particles", "1", CVAR_ARCHIVE};		 // johnfitz
cvar_t r_quadparticles = {"r_quadparticles", "1", CVAR_ARCHIVE}; // johnfitz

extern cvar_t r_showtris;

static VkBuffer particle_index_buffer;

/*
===============
R_ParticleTextureLookup -- johnfitz -- generate nice antialiased 32x32 circle for particles
===============
*/
int R_ParticleTextureLookup (int x, int y, int sharpness)
{
	int r; // distance from point x,y to circle origin, squared
	int a; // alpha value to return

	x -= 16;
	y -= 16;
	r = x * x + y * y;
	r = r > 255 ? 255 : r;
	a = sharpness * (255 - r);
	a = q_min (a, 255);
	return a;
}

/*
===============
R_InitParticleTextures -- johnfitz -- rewritten
===============
*/
void R_InitParticleTextures (void)
{
	int			x, y;
	static byte particle1_data[64 * 64 * 4];
	static byte particle2_data[2 * 2 * 4];
	static byte particle3_data[64 * 64 * 4];
	byte	   *dst;

	// particle texture 1 -- circle
	dst = particle1_data;
	for (x = 0; x < 64; x++)
		for (y = 0; y < 64; y++)
		{
			*dst++ = 255;
			*dst++ = 255;
			*dst++ = 255;
			*dst++ = R_ParticleTextureLookup (x, y, 8);
		}
	particletexture1 = TexMgr_LoadImage (
		NULL, "particle1", 64, 64, SRC_RGBA, particle1_data, "", (src_offset_t)particle1_data, TEXPREF_PERSIST | TEXPREF_ALPHA | TEXPREF_LINEAR);

	// particle texture 2 -- square
	dst = particle2_data;
	for (x = 0; x < 2; x++)
		for (y = 0; y < 2; y++)
		{
			*dst++ = 255;
			*dst++ = 255;
			*dst++ = 255;
			*dst++ = x || y ? 0 : 255;
		}
	particletexture2 = TexMgr_LoadImage (
		NULL, "particle2", 2, 2, SRC_RGBA, particle2_data, "", (src_offset_t)particle2_data, TEXPREF_PERSIST | TEXPREF_ALPHA | TEXPREF_NEAREST);

	// particle texture 3 -- blob
	dst = particle3_data;
	for (x = 0; x < 64; x++)
		for (y = 0; y < 64; y++)
		{
			*dst++ = 255;
			*dst++ = 255;
			*dst++ = 255;
			*dst++ = R_ParticleTextureLookup (x, y, 2);
		}
	particletexture3 = TexMgr_LoadImage (
		NULL, "particle3", 64, 64, SRC_RGBA, particle3_data, "", (src_offset_t)particle3_data, TEXPREF_PERSIST | TEXPREF_ALPHA | TEXPREF_LINEAR);

	// set default
	particletexture = particletexture1;
	texturescalefactor = 1.27;
}

/*
===============
R_SetParticleTexture_f -- johnfitz
===============
*/
static void R_SetParticleTexture_f (cvar_t *var)
{
	switch ((int)(r_particles.value))
	{
	case 1:
		particletexture = particletexture1;
		texturescalefactor = 1.27;
		break;
	case 2:
		particletexture = particletexture2;
		texturescalefactor = 1.0;
		break;
		//	case 3:
		//		particletexture = particletexture3;
		//		texturescalefactor = 1.5;
		//		break;
	}
}

/*
===============
R_InitParticleIndexBuffer
===============
*/
void R_InitParticleIndexBuffer (void)
{
	uint32_t particle_index_buffer_size = r_numparticles * sizeof (uint16_t) * 6; // 6 indices per particle quad

	VkResult err;

	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = particle_index_buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &particle_index_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");

	GL_SetObjectName ((uint64_t)particle_index_buffer, VK_OBJECT_TYPE_BUFFER, "Particle index buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, particle_index_buffer, &memory_requirements);

	const int aligned_size = q_align (memory_requirements.size, memory_requirements.alignment);

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = aligned_size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	Atomic_IncrementUInt32 (&num_vulkan_dynbuf_allocations);
	VkDeviceMemory particle_index_buffer_memory;
	Atomic_AddUInt64 (&total_device_vulkan_allocation_size, memory_requirements.size);
	err = vkAllocateMemory (vulkan_globals.device, &memory_allocate_info, NULL, &particle_index_buffer_memory);
	if (err != VK_SUCCESS)
		Sys_Error ("vkAllocateMemory failed");

	GL_SetObjectName ((uint64_t)particle_index_buffer_memory, VK_OBJECT_TYPE_DEVICE_MEMORY, "Particle index buffer");

	err = vkBindBufferMemory (vulkan_globals.device, particle_index_buffer, particle_index_buffer_memory, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");

	VkBuffer		staging_buffer;
	VkCommandBuffer cb_context;
	int				staging_offset;
	uint16_t	   *staging_indices = (uint16_t *)R_StagingAllocate (particle_index_buffer_size, 1, &cb_context, &staging_buffer, &staging_offset);

	VkBufferCopy region;
	region.srcOffset = staging_offset;
	region.dstOffset = 0;
	region.size = particle_index_buffer_size;
	vkCmdCopyBuffer (cb_context, staging_buffer, particle_index_buffer, 1, &region);

	R_StagingBeginCopy ();
	for (int i = 0; i < r_numparticles; ++i)
	{
		staging_indices[i * 6 + 0] = i * 4 + 0;
		staging_indices[i * 6 + 1] = i * 4 + 1;
		staging_indices[i * 6 + 2] = i * 4 + 2;
		staging_indices[i * 6 + 3] = i * 4 + 0;
		staging_indices[i * 6 + 4] = i * 4 + 2;
		staging_indices[i * 6 + 5] = i * 4 + 3;
	}
	R_StagingEndCopy ();
}

/*
===============
R_InitParticles
===============
*/
void R_InitParticles (void)
{
	int i;

	i = COM_CheckParm ("-particles");

	if (i)
	{
		r_numparticles = (int)(atoi (com_argv[i + 1]));
		if (r_numparticles < ABSOLUTE_MIN_PARTICLES)
			r_numparticles = ABSOLUTE_MIN_PARTICLES;
	}
	else
	{
		r_numparticles = MAX_PARTICLES;
	}

	particles = (particle_t *)Mem_Alloc (r_numparticles * sizeof (particle_t));

	Cvar_RegisterVariable (&r_particles); // johnfitz
	Cvar_SetCallback (&r_particles, R_SetParticleTexture_f);
	Cvar_RegisterVariable (&r_quadparticles); // johnfitz

	R_InitParticleTextures (); // johnfitz
	R_InitParticleIndexBuffer ();
}

/*
===============
R_EntityParticles
===============
*/
#define NUMVERTEXNORMALS 162
extern float r_avertexnormals[NUMVERTEXNORMALS][3];
vec3_t		 avelocities[NUMVERTEXNORMALS];
float		 beamlength = 16;
vec3_t		 avelocity = {23, 7, 3};
float		 partstep = 0.01;
float		 timescale = 0.01;

void R_EntityParticles (entity_t *ent)
{
	int			i;
	particle_t *p;
	float		angle;
	float		sp, sy, cp, cy;
	//	float		sr, cr;
	//	int		count;
	vec3_t		forward;
	float		dist;

	dist = 64;
	//	count = 50;

	if (!avelocities[0][0])
	{
		for (i = 0; i < NUMVERTEXNORMALS; i++)
		{
			avelocities[i][0] = (rand () & 255) * 0.01;
			avelocities[i][1] = (rand () & 255) * 0.01;
			avelocities[i][2] = (rand () & 255) * 0.01;
		}
	}

	for (i = 0; i < NUMVERTEXNORMALS; i++)
	{
		angle = cl.time * avelocities[i][0];
		sy = sin (angle);
		cy = cos (angle);
		angle = cl.time * avelocities[i][1];
		sp = sin (angle);
		cp = cos (angle);
		angle = cl.time * avelocities[i][2];
		//	sr = sin(angle);
		//	cr = cos(angle);

		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;

		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->die = cl.time + 0.01;
		p->color = 0x6f;
		p->type = pt_explode;

		p->org[0] = ent->origin[0] + r_avertexnormals[i][0] * dist + forward[0] * beamlength;
		p->org[1] = ent->origin[1] + r_avertexnormals[i][1] * dist + forward[1] * beamlength;
		p->org[2] = ent->origin[2] + r_avertexnormals[i][2] * dist + forward[2] * beamlength;
	}
}

/*
===============
R_ClearParticles
===============
*/
void R_ClearParticles (void)
{
	int i;

	free_particles = &particles[0];
	active_particles = NULL;

	for (i = 0; i < r_numparticles; i++)
		particles[i].next = &particles[i + 1];
	particles[r_numparticles - 1].next = NULL;
}

/*
===============
R_ReadPointFile_f
===============
*/
void R_ReadPointFile_f (void)
{
	FILE	   *f;
	vec3_t		org;
	int			r;
	int			c;
	particle_t *p;
	char		name[MAX_QPATH];

	if (cls.state != ca_connected)
		return; // need an active map.

	q_snprintf (name, sizeof (name), "maps/%s.pts", cl.mapname);

	COM_FOpenFile (name, &f, NULL);
	if (!f)
	{
		Con_Printf ("couldn't open %s\n", name);
		return;
	}

	Con_Printf ("Reading %s...\n", name);
	c = 0;
	org[0] = org[1] = org[2] = 0; // silence pesky compiler warnings
	for (;;)
	{
		r = fscanf (f, "%f %f %f\n", &org[0], &org[1], &org[2]);
		if (r != 3)
			break;
		c++;

		if (!free_particles)
		{
			Con_Printf ("Not enough free particles\n");
			break;
		}
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->die = 99999;
		p->color = (-c) & 15;
		p->type = pt_static;
		VectorCopy (vec3_origin, p->vel);
		VectorCopy (org, p->org);
	}

	fclose (f);
	Con_Printf ("%i points read\n", c);
}

/*
===============
R_ParseParticleEffect

Parse an effect out of the server message
===============
*/
void R_ParseParticleEffect (void)
{
	vec3_t org, dir;
	int	   i, count, msgcount, color;

	for (i = 0; i < 3; i++)
		org[i] = MSG_ReadCoord (cl.protocolflags);
	for (i = 0; i < 3; i++)
		dir[i] = MSG_ReadChar () * (1.0 / 16);
	msgcount = MSG_ReadByte ();
	color = MSG_ReadByte ();

	if (msgcount == 255)
		count = 1024;
	else
		count = msgcount;

	R_RunParticleEffect (org, dir, color, count);
}

/*
===============
R_ParticleExplosion
===============
*/
void R_ParticleExplosion (vec3_t org)
{
	int			i, j;
	particle_t *p;

	for (i = 0; i < 1024; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->die = cl.time + 5;
		p->color = ramp1[0];
		p->ramp = rand () & 3;
		if (i & 1)
		{
			p->type = pt_explode;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () % 32) - 16);
				p->vel[j] = (rand () % 512) - 256;
			}
		}
		else
		{
			p->type = pt_explode2;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () % 32) - 16);
				p->vel[j] = (rand () % 512) - 256;
			}
		}
	}
}

/*
===============
R_ParticleExplosion2
===============
*/
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength)
{
	int			i, j;
	particle_t *p;
	int			colorMod = 0;

	for (i = 0; i < 512; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->die = cl.time + 0.3;
		p->color = colorStart + (colorMod % colorLength);
		colorMod++;

		p->type = pt_blob;
		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () % 32) - 16);
			p->vel[j] = (rand () % 512) - 256;
		}
	}
}

/*
===============
R_BlobExplosion
===============
*/
void R_BlobExplosion (vec3_t org)
{
	int			i, j;
	particle_t *p;

	for (i = 0; i < 1024; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->die = cl.time + 1 + (rand () & 8) * 0.05;

		if (i & 1)
		{
			p->type = pt_blob;
			p->color = 66 + rand () % 6;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () % 32) - 16);
				p->vel[j] = (rand () % 512) - 256;
			}
		}
		else
		{
			p->type = pt_blob2;
			p->color = 150 + rand () % 6;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () % 32) - 16);
				p->vel[j] = (rand () % 512) - 256;
			}
		}
	}
}

/*
===============
R_RunParticleEffect
===============
*/
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count)
{
	int			i, j;
	particle_t *p;

	for (i = 0; i < count; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		if (count == 1024)
		{ // rocket explosion
			p->die = cl.time + 5;
			p->color = ramp1[0];
			p->ramp = rand () & 3;
			if (i & 1)
			{
				p->type = pt_explode;
				for (j = 0; j < 3; j++)
				{
					p->org[j] = org[j] + ((rand () % 32) - 16);
					p->vel[j] = (rand () % 512) - 256;
				}
			}
			else
			{
				p->type = pt_explode2;
				for (j = 0; j < 3; j++)
				{
					p->org[j] = org[j] + ((rand () % 32) - 16);
					p->vel[j] = (rand () % 512) - 256;
				}
			}
		}
		else
		{
			p->die = cl.time + 0.1 * (rand () % 5);
			p->color = (color & ~7) + (rand () & 7);
			p->type = pt_slowgrav;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () & 15) - 8);
				p->vel[j] = dir[j] * 15; // + (rand()%300)-150;
			}
		}
	}
}

/*
===============
R_LavaSplash
===============
*/
void R_LavaSplash (vec3_t org)
{
	int			i, j, k;
	particle_t *p;
	float		vel;
	vec3_t		dir;

	for (i = -16; i < 16; i++)
		for (j = -16; j < 16; j++)
			for (k = 0; k < 1; k++)
			{
				if (!free_particles)
					return;
				p = free_particles;
				free_particles = p->next;
				p->next = active_particles;
				active_particles = p;

				p->die = cl.time + 2 + (rand () & 31) * 0.02;
				p->color = 224 + (rand () & 7);
				p->type = pt_slowgrav;

				dir[0] = j * 8 + (rand () & 7);
				dir[1] = i * 8 + (rand () & 7);
				dir[2] = 256;

				p->org[0] = org[0] + dir[0];
				p->org[1] = org[1] + dir[1];
				p->org[2] = org[2] + (rand () & 63);

				VectorNormalize (dir);
				vel = 50 + (rand () & 63);
				VectorScale (dir, vel, p->vel);
			}
}

/*
===============
R_TeleportSplash
===============
*/
void R_TeleportSplash (vec3_t org)
{
	int			i, j, k;
	particle_t *p;
	float		vel;
	vec3_t		dir;

	for (i = -16; i < 16; i += 4)
		for (j = -16; j < 16; j += 4)
			for (k = -24; k < 32; k += 4)
			{
				if (!free_particles)
					return;
				p = free_particles;
				free_particles = p->next;
				p->next = active_particles;
				active_particles = p;

				p->die = cl.time + 0.2 + (rand () & 7) * 0.02;
				p->color = 7 + (rand () & 7);
				p->type = pt_slowgrav;

				dir[0] = j * 8;
				dir[1] = i * 8;
				dir[2] = k * 8;

				p->org[0] = org[0] + i + (rand () & 3);
				p->org[1] = org[1] + j + (rand () & 3);
				p->org[2] = org[2] + k + (rand () & 3);

				VectorNormalize (dir);
				vel = 50 + (rand () & 63);
				VectorScale (dir, vel, p->vel);
			}
}

/*
===============
R_RocketTrail

FIXME -- rename function and use #defined types instead of numbers
===============
*/
void R_RocketTrail (vec3_t start, vec3_t end, int type)
{
	vec3_t		vec;
	float		len;
	int			j;
	particle_t *p;
	int			dec;
	static int	tracercount;

	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);
	if (type < 128)
		dec = 3;
	else
	{
		dec = 1;
		type -= 128;
	}

	while (len > 0)
	{
		len -= dec;

		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		VectorCopy (vec3_origin, p->vel);
		p->die = cl.time + 2;

		switch (type)
		{
		case 0: // rocket trail
			p->ramp = (rand () & 3);
			p->color = ramp3[(int)p->ramp];
			p->type = pt_fire;
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);
			break;

		case 1: // smoke smoke
			p->ramp = (rand () & 3) + 2;
			p->color = ramp3[(int)p->ramp];
			p->type = pt_fire;
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);
			break;

		case 2: // blood
			p->type = pt_grav;
			p->color = 67 + (rand () & 3);
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);
			break;

		case 3:
		case 5: // tracer
			p->die = cl.time + 0.5;
			p->type = pt_static;
			if (type == 3)
				p->color = 52 + ((tracercount & 4) << 1);
			else
				p->color = 230 + ((tracercount & 4) << 1);

			tracercount++;

			VectorCopy (start, p->org);
			if (tracercount & 1)
			{
				p->vel[0] = 30 * vec[1];
				p->vel[1] = 30 * -vec[0];
			}
			else
			{
				p->vel[0] = 30 * -vec[1];
				p->vel[1] = 30 * vec[0];
			}
			break;

		case 4: // slight blood
			p->type = pt_grav;
			p->color = 67 + (rand () & 3);
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);
			len -= 3;
			break;

		case 6: // voor trail
			p->color = 9 * 16 + 8 + (rand () & 3);
			p->type = pt_static;
			p->die = cl.time + 0.3;
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () & 15) - 8);
			break;
		}

		VectorAdd (start, vec, start);
	}
}

/*
===============
CL_RunParticles -- johnfitz -- all the particle behavior, separated from R_DrawParticles
===============
*/
void CL_RunParticles (void)
{
	particle_t	 *p, *kill;
	int			  i;
	float		  time1, time2, time3, dvel, frametime, grav;
	extern cvar_t sv_gravity;

	frametime = q_max (0.0, cl.time - cl.oldtime);
	time3 = frametime * 15;
	time2 = frametime * 10;
	time1 = frametime * 5;
	grav = frametime * sv_gravity.value * 0.05;
	dvel = 4 * frametime;

	for (;;)
	{
		kill = active_particles;
		if (kill && kill->die < cl.time)
		{
			active_particles = kill->next;
			kill->next = free_particles;
			free_particles = kill;
			continue;
		}
		break;
	}

	for (p = active_particles; p; p = p->next)
	{
		for (;;)
		{
			kill = p->next;
			if (kill && kill->die < cl.time)
			{
				p->next = kill->next;
				kill->next = free_particles;
				free_particles = kill;
				continue;
			}
			break;
		}

		p->org[0] += p->vel[0] * frametime;
		p->org[1] += p->vel[1] * frametime;
		p->org[2] += p->vel[2] * frametime;

		switch (p->type)
		{
		case pt_static:
			break;
		case pt_fire:
			p->ramp += time1;
			if (p->ramp >= 6)
				p->die = -1;
			else
				p->color = ramp3[(int)p->ramp];
			p->vel[2] += grav;
			break;

		case pt_explode:
			p->ramp += time2;
			if (p->ramp >= 8)
				p->die = -1;
			else
				p->color = ramp1[(int)p->ramp];
			for (i = 0; i < 3; i++)
				p->vel[i] += p->vel[i] * dvel;
			p->vel[2] -= grav;
			break;

		case pt_explode2:
			p->ramp += time3;
			if (p->ramp >= 8)
				p->die = -1;
			else
				p->color = ramp2[(int)p->ramp];
			for (i = 0; i < 3; i++)
				p->vel[i] -= p->vel[i] * frametime;
			p->vel[2] -= grav;
			break;

		case pt_blob:
			for (i = 0; i < 3; i++)
				p->vel[i] += p->vel[i] * dvel;
			p->vel[2] -= grav;
			break;

		case pt_blob2:
			for (i = 0; i < 2; i++)
				p->vel[i] -= p->vel[i] * dvel;
			p->vel[2] -= grav;
			break;

		case pt_grav:
		case pt_slowgrav:
			p->vel[2] -= grav;
			break;
		}
	}
}

/*
===============
R_DrawParticlesFaces
===============
*/
static void R_DrawParticlesFaces (cb_context_t *cbx)
{
	particle_t	 *p;
	float		  scale, texcoord_scale;
	vec3_t		  up, right, up_right, p_up, p_right, p_up_right;
	extern cvar_t r_particles; // johnfitz

	if (!r_particles.value)
		return;

	if (!active_particles)
		return;

	if (r_quadparticles.value)
	{
		VectorScale (vup, 0.75, up);
		VectorScale (vright, 0.75, right);
		texcoord_scale = 0.5f;
	}
	else
	{
		VectorScale (vup, 1.5, up);
		VectorScale (vright, 1.5, right);
		texcoord_scale = 1.0f;
	}

	for (int i = 0; i < 3; ++i)
		up_right[i] = up[i] + right[i];

	int num_particles = 0;
	for (p = active_particles; p; p = p->next)
		num_particles += 1;
	Atomic_AddUInt32 (&rs_particles, num_particles);

	VkBuffer	   vertex_buffer;
	VkDeviceSize   vertex_buffer_offset;
	basicvertex_t *vertices;
	if (r_quadparticles.value)
		vertices = (basicvertex_t *)R_VertexAllocate (num_particles * 4 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);
	else
		vertices = (basicvertex_t *)R_VertexAllocate (num_particles * 3 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);

	int current_vertex = 0;
	for (p = active_particles; p; p = p->next)
	{
		// hack a scale up to keep particles from disapearing
		scale = (p->org[0] - r_origin[0]) * vpn[0] + (p->org[1] - r_origin[1]) * vpn[1] + (p->org[2] - r_origin[2]) * vpn[2];
		if (scale < 20)
			scale = 1 + 0.08; // johnfitz -- added .08 to be consistent
		else
			scale = 1 + scale * 0.004;

		scale *= texturescalefactor; // johnfitz -- compensate for apparent size of different particle textures

		byte *c = (byte *)&d_8to24table[(int)p->color];

		vertices[current_vertex].position[0] = p->org[0];
		vertices[current_vertex].position[1] = p->org[1];
		vertices[current_vertex].position[2] = p->org[2];
		vertices[current_vertex].texcoord[0] = 0.0f;
		vertices[current_vertex].texcoord[1] = 0.0f;
		vertices[current_vertex].color[0] = c[0];
		vertices[current_vertex].color[1] = c[1];
		vertices[current_vertex].color[2] = c[2];
		vertices[current_vertex].color[3] = 255;
		current_vertex++;

		VectorMA (p->org, scale, up, p_up);
		vertices[current_vertex].position[0] = p_up[0];
		vertices[current_vertex].position[1] = p_up[1];
		vertices[current_vertex].position[2] = p_up[2];
		vertices[current_vertex].texcoord[0] = texcoord_scale;
		vertices[current_vertex].texcoord[1] = 0.0f;
		vertices[current_vertex].color[0] = c[0];
		vertices[current_vertex].color[1] = c[1];
		vertices[current_vertex].color[2] = c[2];
		vertices[current_vertex].color[3] = 255;
		current_vertex++;

		if (r_quadparticles.value)
		{
			VectorMA (p->org, scale, up_right, p_up_right);
			vertices[current_vertex].position[0] = p_up_right[0];
			vertices[current_vertex].position[1] = p_up_right[1];
			vertices[current_vertex].position[2] = p_up_right[2];
			vertices[current_vertex].texcoord[0] = texcoord_scale;
			vertices[current_vertex].texcoord[1] = texcoord_scale;
			vertices[current_vertex].color[0] = c[0];
			vertices[current_vertex].color[1] = c[1];
			vertices[current_vertex].color[2] = c[2];
			vertices[current_vertex].color[3] = 255;
			current_vertex++;
		}

		VectorMA (p->org, scale, right, p_right);
		vertices[current_vertex].position[0] = p_right[0];
		vertices[current_vertex].position[1] = p_right[1];
		vertices[current_vertex].position[2] = p_right[2];
		vertices[current_vertex].texcoord[0] = 0.0f;
		vertices[current_vertex].texcoord[1] = texcoord_scale;
		vertices[current_vertex].color[0] = c[0];
		vertices[current_vertex].color[1] = c[1];
		vertices[current_vertex].color[2] = c[2];
		vertices[current_vertex].color[3] = 255;
		current_vertex++;
	}

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	if (r_quadparticles.value)
	{
		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, particle_index_buffer, 0, VK_INDEX_TYPE_UINT16);
		vulkan_globals.vk_cmd_draw_indexed (cbx->cb, num_particles * 6, 1, 0, 0, 0);
	}
	else
		vulkan_globals.vk_cmd_draw (cbx->cb, num_particles * 3, 1, 0, 0);
}

/*
===============
R_DrawParticles -- johnfitz -- moved all non-drawing code to CL_RunParticles
===============
*/
void R_DrawParticles (cb_context_t *cbx)
{
	R_BeginDebugUtilsLabel (cbx, "Particles");
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.particle_pipeline);
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &particletexture->descriptor_set, 0, NULL);

	R_DrawParticlesFaces (cbx);
	R_EndDebugUtilsLabel (cbx);
}

/*
===============
R_DrawParticles_ShowTris -- johnfitz
===============
*/
void R_DrawParticles_ShowTris (cb_context_t *cbx)
{
	if (r_showtris.value == 1)
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_pipeline);
	else
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_depth_test_pipeline);

	R_DrawParticlesFaces (cbx);
}
