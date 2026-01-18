/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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
// gl_mesh.c: triangle model functions

#include "quakedef.h"
#include "gl_heap.h"

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

// Heap
#define MESH_HEAP_SIZE_MB	16
#define MESH_HEAP_PAGE_SIZE 4096
#define MESH_HEAP_NAME		"Mesh heap"

extern cvar_t r_lerpmodels;
extern cvar_t r_rtshadows;

static glheap_t *mesh_buffer_heap;

typedef struct
{
	VkBuffer				  buffer;
	VkDescriptorSet			  descriptor_set;
	glheapallocation_t		 *allocation;
	VkDescriptorSet			  desc_set;
	vulkan_desc_set_layout_t *desc_set_layout;
} buffer_garbage_t;

typedef struct
{
	VkAccelerationStructureKHR blas;
	VkBuffer				   buffer;
	glheapallocation_t		  *allocation;
	VkDescriptorSet			   compute_set;
	vulkan_desc_set_layout_t  *compute_set_layout;
} blas_garbage_t;

static int				current_garbage_index;
static int				num_garbage_buffers[2];
static buffer_garbage_t buffer_garbage[MAX_MODELS * 2][2];
static int				num_garbage_blas[2];
static blas_garbage_t	blas_garbage[MAX_EDICTS][2];

/*
================
AddBufferGarbage
================
*/
static void AddBufferGarbage (
	VkBuffer buffer, VkDescriptorSet descriptor_set, glheapallocation_t *allocation, const VkDescriptorSet desc_set, vulkan_desc_set_layout_t *desc_set_layout)
{
	int				  garbage_index;
	buffer_garbage_t *garbage;

	garbage_index = num_garbage_buffers[current_garbage_index]++;
	garbage = &buffer_garbage[garbage_index][current_garbage_index];
	garbage->buffer = buffer;
	garbage->descriptor_set = descriptor_set;
	garbage->allocation = allocation;
	garbage->desc_set = desc_set;
	garbage->desc_set_layout = desc_set_layout;
}

/*
================
AddBLASGarbage
================
*/
static void AddBLASGarbage (
	VkAccelerationStructureKHR blas, VkBuffer buffer, glheapallocation_t *allocation, VkDescriptorSet compute_set, vulkan_desc_set_layout_t *compute_set_layout)
{
	int				garbage_index;
	blas_garbage_t *garbage;

	garbage_index = num_garbage_blas[current_garbage_index]++;
	garbage = &blas_garbage[garbage_index][current_garbage_index];
	garbage->blas = blas;
	garbage->buffer = buffer;
	garbage->allocation = allocation;
	garbage->compute_set = compute_set;
	garbage->compute_set_layout = compute_set_layout;
}

/*
================
R_InitMeshHeap
================
*/
void R_InitMeshHeap (void)
{
	// Allocate index buffer & upload to GPU
	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = 16;
	buffer_create_info.usage =
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if (vulkan_globals.ray_query)
		buffer_create_info.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
	VkBuffer dummy_buffer;
	VkResult err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &dummy_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, dummy_buffer, &memory_requirements);

	const uint32_t memory_type_index = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	VkDeviceSize   heap_size = MESH_HEAP_SIZE_MB * (VkDeviceSize)1024 * (VkDeviceSize)1024;
	mesh_buffer_heap = GL_HeapCreate (heap_size, MESH_HEAP_PAGE_SIZE, memory_type_index, VULKAN_MEMORY_TYPE_DEVICE, MESH_HEAP_NAME);

	vkDestroyBuffer (vulkan_globals.device, dummy_buffer, NULL);
}

/*
================
R_GetMeshHeapStats
================
*/
glheapstats_t *R_GetMeshHeapStats (void)
{
	return GL_HeapGetStats (mesh_buffer_heap);
}

/*
================
R_CollectMeshBufferGarbage
================
*/
void R_CollectMeshBufferGarbage (void)
{
	int				  num;
	int				  i;
	buffer_garbage_t *garbage;

	current_garbage_index = (current_garbage_index + 1) % 2;
	num = num_garbage_buffers[current_garbage_index];
	for (i = 0; i < num; ++i)
	{
		garbage = &buffer_garbage[i][current_garbage_index];
		vkDestroyBuffer (vulkan_globals.device, garbage->buffer, NULL);
		GL_HeapFree (mesh_buffer_heap, garbage->allocation, &num_vulkan_mesh_allocations);
		if (garbage->desc_set != VK_NULL_HANDLE)
			R_FreeDescriptorSet (garbage->desc_set, garbage->desc_set_layout);
	}
	num_garbage_buffers[current_garbage_index] = 0;

	// Process BLAS garbage
	num = num_garbage_blas[current_garbage_index];
	for (i = 0; i < num; ++i)
	{
		blas_garbage_t *blas_g = &blas_garbage[i][current_garbage_index];
		vulkan_globals.vk_destroy_acceleration_structure (vulkan_globals.device, blas_g->blas, NULL);
		vkDestroyBuffer (vulkan_globals.device, blas_g->buffer, NULL);
		GL_HeapFree (mesh_buffer_heap, blas_g->allocation, &num_vulkan_mesh_allocations);
		if (blas_g->compute_set != VK_NULL_HANDLE)
			R_FreeDescriptorSet (blas_g->compute_set, blas_g->compute_set_layout);
	}
	num_garbage_blas[current_garbage_index] = 0;
}

/*
================
GL_MakeAliasModelDisplayLists
Original code by MH from RMQEngine
================
*/
static uint32_t AliasMeshHash (const void *const p)
{
	aliasmesh_t *mesh = (aliasmesh_t *)p;
	uint32_t	 vertindex = mesh->vertindex;
	return HashCombine (HashInt32 (&vertindex), HashCombine (HashFloat (&mesh->st[0]), HashFloat (&mesh->st[1])));
}

void GL_MakeAliasModelDisplayLists (qmodel_t *m, aliashdr_t *paliashdr)
{
	assert (paliashdr->poseverttype == PV_QUAKE1);

	Con_DPrintf2 ("meshing %s...\n", m->name);

	// first, copy the verts onto the hunk
	TEMP_ALLOC_ZEROED (trivertx_t, verts, paliashdr->numposes * paliashdr->numverts);

	for (int i = 0; i < paliashdr->numposes; i++)
		for (int j = 0; j < paliashdr->numverts; j++)
			verts[i * paliashdr->numverts + j] = poseverts[i][j];

	// there can never be more than this number of verts
	const int maxverts_vbo = paliashdr->numtris * 3;
	TEMP_ALLOC_ZEROED (aliasmesh_t, desc, maxverts_vbo);
	// there will always be this number of indexes
	TEMP_ALLOC_ZEROED (unsigned short, indexes, maxverts_vbo);

	hash_map_t *vertex_to_index_map = HashMap_Create (aliasmesh_t, unsigned short, &AliasMeshHash, NULL);
	HashMap_Reserve (vertex_to_index_map, maxverts_vbo);

	ZEROED_STRUCT (aliasmesh_t, mesh);
	for (int i = 0; i < paliashdr->numtris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			// index into hdr->vertexes
			unsigned short vertindex = triangles[i].vertindex[j];

			// basic s/t coords
			int s = stverts[vertindex].s;
			int t = stverts[vertindex].t;

			// check for back side and adjust texcoord s
			if (!triangles[i].facesfront && stverts[vertindex].onseam)
				s += paliashdr->skinwidth / 2;

			mesh.st[0] = s;
			mesh.st[1] = t;
			mesh.vertindex = vertindex;

			// Check if this vert already exists
			unsigned short	index;
			unsigned short *found_index;
			if ((found_index = HashMap_Lookup (unsigned short, vertex_to_index_map, &mesh)))
				index = *found_index;
			else
			{
				// doesn't exist; emit a new vert and index
				index = paliashdr->numverts_vbo;
				HashMap_Insert (vertex_to_index_map, &mesh, &index);
				desc[paliashdr->numverts_vbo].vertindex = vertindex;
				desc[paliashdr->numverts_vbo].st[0] = s;
				desc[paliashdr->numverts_vbo++].st[1] = t;
			}

			indexes[paliashdr->numindexes++] = index;
		}
	}

	HashMap_Destroy (vertex_to_index_map);

	// upload immediately
	paliashdr->poseverttype = PV_QUAKE1;
	GLMesh_UploadBuffers (m, paliashdr, indexes, (byte *)verts, desc, NULL);

	TEMP_FREE (indexes);
	TEMP_FREE (desc);
	TEMP_FREE (verts);
}

#define NUMVERTEXNORMALS 162
extern float r_avertexnormals[NUMVERTEXNORMALS][3];

/*
================
GLMesh_DeleteMeshBuffers
================
*/
void GLMesh_DeleteMeshBuffers (aliashdr_t *mainhdr)
{
	// Delete all surfaces:
	for (aliashdr_t *hdr = mainhdr; hdr != NULL; hdr = hdr->nextsurface)
	{
		if (!hdr || (hdr->vertex_buffer == VK_NULL_HANDLE))
			return;

		if (in_update_screen)
		{
			AddBufferGarbage (hdr->vertex_buffer, VK_NULL_HANDLE, hdr->vertex_allocation, VK_NULL_HANDLE, NULL);
			AddBufferGarbage (hdr->index_buffer, VK_NULL_HANDLE, hdr->index_allocation, VK_NULL_HANDLE, NULL);
			if (hdr->joints_buffer != VK_NULL_HANDLE)
				AddBufferGarbage (hdr->joints_buffer, VK_NULL_HANDLE, hdr->joints_allocation, hdr->joints_set, &vulkan_globals.joints_buffer_set_layout);
		}
		else
		{
			GL_WaitForDeviceIdle ();

			vkDestroyBuffer (vulkan_globals.device, hdr->vertex_buffer, NULL);
			GL_HeapFree (mesh_buffer_heap, hdr->vertex_allocation, &num_vulkan_mesh_allocations);

			vkDestroyBuffer (vulkan_globals.device, hdr->index_buffer, NULL);
			GL_HeapFree (mesh_buffer_heap, hdr->index_allocation, &num_vulkan_mesh_allocations);

			if (hdr->joints_buffer != VK_NULL_HANDLE)
			{
				vkDestroyBuffer (vulkan_globals.device, hdr->joints_buffer, NULL);
				GL_HeapFree (mesh_buffer_heap, hdr->joints_allocation, &num_vulkan_mesh_allocations);
				R_FreeDescriptorSet (hdr->joints_set, &vulkan_globals.joints_buffer_set_layout);
			}
		}

		hdr->vertex_buffer = VK_NULL_HANDLE;
		hdr->vertex_allocation = NULL;
		hdr->index_buffer = VK_NULL_HANDLE;
		hdr->index_allocation = NULL;
		hdr->joints_buffer = VK_NULL_HANDLE;
		hdr->joints_allocation = NULL;
		hdr->joints_set = VK_NULL_HANDLE;
		for (int i = 0; i < MAX_SKINS; ++i)
			SAFE_FREE (hdr->texels[i]);
	}
}

/*
================
GLMesh_UploadBuffers : Upload data for a single aliashdr_t *hdr (not it's nextsurfaces)
================
*/
void GLMesh_UploadBuffers (qmodel_t *mod, aliashdr_t *hdr, unsigned short *indexes, byte *vertexes, aliasmesh_t *desc, jointpose_t *joints)
{
	int		 numindexes = 0;
	int		 numverts = 0;
	VkResult err;
	if (!hdr)
		return;

	// count how much space we're going to need.
	int totalvbosize = 0;

	switch (hdr->poseverttype)
	{
	case PV_QUAKE1:
	{
		numverts = hdr->numverts_vbo;
		totalvbosize += (numverts * hdr->numposes * (int)sizeof (meshxyz_t)); // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
		numindexes = hdr->numindexes;
	}
	break;
	case PV_QUAKE3:
	{
		numverts = hdr->numverts_vbo;
		totalvbosize += (numverts * hdr->numframes * (int)sizeof (meshxyz_t)); // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
		numindexes = hdr->numindexes;
	}
	break;
	case PV_MD5:
	{
		assert (hdr->numposes == 1);
		totalvbosize += hdr->numverts_vbo * (int)sizeof (md5vert_t);
		numverts = hdr->numverts_vbo;
		numindexes = hdr->numindexes;
	}
	break;
	default:
		assert (false);
	}

	const size_t totaljointssize = hdr->numframes * hdr->numjoints * sizeof (jointpose_t);

	if (hdr->poseverttype == PV_QUAKE1 || hdr->poseverttype == PV_QUAKE3)
	{
		// reserve room from ST data starting at vbostofs.
		hdr->vbostofs = totalvbosize;
		totalvbosize += (numverts * sizeof (meshst_t));
	}

	if (isDedicated)
		return;
	if (!numindexes)
		return;
	if (!totalvbosize)
		return;

	{
		const size_t totalindexsize = numindexes * sizeof (unsigned short);

		// Allocate index buffer & upload to GPU
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = totalindexsize;
		buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (vulkan_globals.ray_query)
			buffer_create_info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &hdr->index_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)hdr->index_buffer, VK_OBJECT_TYPE_BUFFER, mod->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, hdr->index_buffer, &memory_requirements);

		hdr->index_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		err = vkBindBufferMemory (
			vulkan_globals.device, hdr->index_buffer, GL_HeapGetAllocationMemory (hdr->index_allocation), GL_HeapGetAllocationOffset (hdr->index_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		R_StagingUploadBuffer (hdr->index_buffer, totalindexsize, (byte *)indexes);

		// Get device address for ray tracing
		if (vulkan_globals.ray_query)
		{
			VkBufferDeviceAddressInfoKHR address_info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR};
			address_info.buffer = hdr->index_buffer;
			hdr->index_buffer_address = vulkan_globals.vk_get_buffer_device_address (vulkan_globals.device, &address_info);
		}
	}

	// create the vertex buffer (empty)
	TEMP_ALLOC (byte, vbodata, totalvbosize);

	// fill in the vertices of the buffer
	size_t vertofs = 0;

	switch (hdr->poseverttype)
	{
	case PV_QUAKE1:
		for (int f = 0; f < hdr->numposes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
		{
			meshxyz_t		 *xyz = (meshxyz_t *)vbodata + vertofs;
			const trivertx_t *tv = (trivertx_t *)vertexes + (hdr->numverts * f);
			vertofs += hdr->numverts_vbo;

			for (int v = 0; v < hdr->numverts_vbo; v++)
			{
				trivertx_t trivert = tv[desc[v].vertindex];
				// MDL is [0-255] => remapped on unsigned 16bit [0; 65535] seen as [0,1] coords in the vertex shader
				// to be compatible with the MD3 range
				xyz[v].xyz[0] = (int)trivert.v[0] * 257;
				xyz[v].xyz[1] = (int)trivert.v[1] * 257;
				xyz[v].xyz[2] = (int)trivert.v[2] * 257;
				xyz[v].xyz[3] = 1; // need w 1 for 4 byte vertex compression

				// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
				// this introduces some error (less than 0.004), but the normals were very coarse
				// to begin with
				xyz[v].normal[0] = 127 * r_avertexnormals[trivert.lightnormalindex][0];
				xyz[v].normal[1] = 127 * r_avertexnormals[trivert.lightnormalindex][1];
				xyz[v].normal[2] = 127 * r_avertexnormals[trivert.lightnormalindex][2];
				xyz[v].normal[3] = 0; // unused; for 4-byte alignment
			}
		}
		break;
	case PV_QUAKE3:
		for (int f = 0; f < hdr->numframes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
		{
			meshxyz_t			 *xyz = (meshxyz_t *)vbodata + vertofs;
			const md3XyzNormal_t *tv = (md3XyzNormal_t *)vertexes + (hdr->numverts * f);
			vertofs += hdr->numverts_vbo;

			float lat, lng;

			for (int v = 0; v < hdr->numverts_vbo; v++, tv++)
			{
				// MD3 is SIGNED 16bit => remapped on unsigned 16bit seen as [0,1] coords in the vertex shader
				xyz[v].xyz[0] = (int)tv->xyz[0] + 32768;
				xyz[v].xyz[1] = (int)tv->xyz[1] + 32768;
				xyz[v].xyz[2] = (int)tv->xyz[2] + 32768;
				xyz[v].xyz[3] = 1; // need w 1 for 4 byte vertex compression

				// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
				// this introduces some error (less than 0.004), but the normals were very coarse
				// to begin with
				lat = (float)tv->latlong[0] * (2 * M_PI) * (1.0 / 255.0);
				lng = (float)tv->latlong[1] * (2 * M_PI) * (1.0 / 255.0);
				xyz[v].normal[0] = 127 * cos (lng) * sin (lat);
				xyz[v].normal[1] = 127 * sin (lng) * sin (lat);
				xyz[v].normal[2] = 127 * cos (lat);
				xyz[v].normal[3] = 0; // unused; for 4-byte alignment
			}
		}
		break;
	case PV_MD5:
		memcpy (vbodata, vertexes, totalvbosize);
		// vertexes is already the concat of the hdr surface vertices, triangles, ST, and normals
		// already baked in.
		break;
	default:
		assert (false);
	}

	// fill in the ST coords at the end of the buffer for MDL and MD3:
	if (hdr->poseverttype == PV_QUAKE1)
	{
		assert (hdr->nextsurface == NULL);

		meshst_t *st = (meshst_t *)(vbodata + hdr->vbostofs);
		for (int f = 0; f < hdr->numverts_vbo; f++)
		{
			st[f].st[0] = ((float)desc[f].st[0] + 0.5f) / (float)hdr->skinwidth;
			st[f].st[1] = ((float)desc[f].st[1] + 0.5f) / (float)hdr->skinheight;
		}
	}
	else if (hdr->poseverttype == PV_QUAKE3)
	{
		meshst_t *st = (meshst_t *)(vbodata + hdr->vbostofs);
		for (int f = 0; f < hdr->numverts_vbo; f++)
		{
			// md3 has floating-point skin coords. use the values directly.
			st[f].st[0] = desc[f].st[0];
			st[f].st[1] = desc[f].st[1];
		}
	}

	// Allocate vertex buffer & upload to GPU
	{
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = totalvbosize;
		buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (vulkan_globals.ray_query)
			buffer_create_info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &hdr->vertex_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)hdr->vertex_buffer, VK_OBJECT_TYPE_BUFFER, mod->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, hdr->vertex_buffer, &memory_requirements);

		hdr->vertex_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		err = vkBindBufferMemory (
			vulkan_globals.device, hdr->vertex_buffer, GL_HeapGetAllocationMemory (hdr->vertex_allocation),
			GL_HeapGetAllocationOffset (hdr->vertex_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		R_StagingUploadBuffer (hdr->vertex_buffer, totalvbosize, vbodata);

		// Get device address for ray tracing
		if (vulkan_globals.ray_query)
		{
			VkBufferDeviceAddressInfoKHR address_info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR};
			address_info.buffer = hdr->vertex_buffer;
			hdr->vertex_buffer_address = vulkan_globals.vk_get_buffer_device_address (vulkan_globals.device, &address_info);
		}
	}

	// Allocate joints buffer & upload to GPU
	if (joints)
	{
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = totaljointssize;
		buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &hdr->joints_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)hdr->joints_buffer, VK_OBJECT_TYPE_BUFFER, mod->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, hdr->joints_buffer, &memory_requirements);

		hdr->joints_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		err = vkBindBufferMemory (
			vulkan_globals.device, hdr->joints_buffer, GL_HeapGetAllocationMemory (hdr->joints_allocation),
			GL_HeapGetAllocationOffset (hdr->joints_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		R_StagingUploadBuffer (hdr->joints_buffer, totaljointssize, (byte *)joints);

		hdr->joints_set = R_AllocateDescriptorSet (&vulkan_globals.joints_buffer_set_layout);

		ZEROED_STRUCT (VkDescriptorBufferInfo, buffer_info);
		buffer_info.buffer = hdr->joints_buffer;
		buffer_info.offset = 0;
		buffer_info.range = VK_WHOLE_SIZE;

		ZEROED_STRUCT (VkWriteDescriptorSet, joints_set_write);
		joints_set_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		joints_set_write.dstSet = hdr->joints_set;
		joints_set_write.dstBinding = 0;
		joints_set_write.dstArrayElement = 0;
		joints_set_write.descriptorCount = 1;
		joints_set_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		joints_set_write.pBufferInfo = &buffer_info;

		vkUpdateDescriptorSets (vulkan_globals.device, 1, &joints_set_write, 0, NULL);
	}

	TEMP_FREE (vbodata);
}

/*
================
GLMesh_DeleteAllMeshBuffers

Delete VBOs for all loaded alias models
================
*/
void GLMesh_DeleteAllMeshBuffers (void)
{
	qmodel_t *m;

	for (int j = 1; j < MAX_MODELS; j++)
	{
		if (!(m = cl.model_precache[j]))
			break;
		if (m->type != mod_alias)
			continue;

		for (int i = 0; i < PV_SIZE; ++i)
		{
			GLMesh_DeleteMeshBuffers ((aliashdr_t *)m->extradata[i]);
		}
	}
}

/*
================
R_AllocateEntityBLAS

Allocate acceleration structure for an entity with an alias model.
Handles MDL (PV_QUAKE1), MD3 (PV_QUAKE3), and MD5 (PV_MD5) models.
================
*/
void R_AllocateEntityBLAS (entity_t *e)
{
	if (!vulkan_globals.ray_query || r_rtshadows.value <= 0)
		return;
	if (!e->model || e->model->type != mod_alias)
		return;

	// Check if model changed - need to reallocate BLAS
	if (e->blas != VK_NULL_HANDLE && e->blas_model != e->model)
	{
		R_FreeEntityBLAS (e);
	}

	if (e->blas != VK_NULL_HANDLE)
		return;

	aliashdr_t *hdr = (aliashdr_t *)Mod_Extradata (e->model);
	if (!hdr)
		return;

	// TODO: handle multi-surface models (nextsurface chain)
	const uint32_t num_triangles = hdr->numtris;
	if (num_triangles == 0)
		return;

	// Set up geometry info for size query
	// Vertex positions will be computed into scratch memory as vec3 floats
	ZEROED_STRUCT (VkAccelerationStructureGeometryKHR, blas_geometry);
	blas_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	blas_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	blas_geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	blas_geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	blas_geometry.geometry.triangles.vertexStride = sizeof (float) * 3;
	blas_geometry.geometry.triangles.maxVertex = hdr->numverts_vbo;
	blas_geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT16;

	ZEROED_STRUCT (VkAccelerationStructureBuildGeometryInfoKHR, blas_geometry_info);
	blas_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	blas_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	blas_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	blas_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	blas_geometry_info.geometryCount = 1;
	blas_geometry_info.pGeometries = &blas_geometry;

	// Query acceleration structure size
	ZEROED_STRUCT (VkAccelerationStructureBuildSizesInfoKHR, blas_sizes_info);
	blas_sizes_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vulkan_globals.vk_get_acceleration_structure_build_sizes (
		vulkan_globals.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_geometry_info, &num_triangles, &blas_sizes_info);

	// Create buffer for BLAS
	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = blas_sizes_info.accelerationStructureSize;
	buffer_create_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	VkResult err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &e->blas_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed for entity BLAS");

	// Allocate from mesh heap
	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, e->blas_buffer, &memory_requirements);

	e->blas_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
	err = vkBindBufferMemory (
		vulkan_globals.device, e->blas_buffer, GL_HeapGetAllocationMemory (e->blas_allocation), GL_HeapGetAllocationOffset (e->blas_allocation));
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed for entity BLAS");

	// Create acceleration structure
	ZEROED_STRUCT (VkAccelerationStructureCreateInfoKHR, acceleration_structure_create_info);
	acceleration_structure_create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	acceleration_structure_create_info.buffer = e->blas_buffer;
	acceleration_structure_create_info.size = blas_sizes_info.accelerationStructureSize;
	acceleration_structure_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	err = vulkan_globals.vk_create_acceleration_structure (vulkan_globals.device, &acceleration_structure_create_info, NULL, &e->blas);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateAccelerationStructure failed for entity BLAS");

	// Get device address
	ZEROED_STRUCT (VkAccelerationStructureDeviceAddressInfoKHR, address_info);
	address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	address_info.accelerationStructure = e->blas;
	e->blas_address = vulkan_globals.vk_get_acceleration_structure_device_address (vulkan_globals.device, &address_info);

	// Store scratch size for per-frame rebuilds
	e->blas_build_scratch_size = blas_sizes_info.buildScratchSize;

	// Track which model this BLAS was allocated for
	e->blas_model = e->model;

	// Allocate and set up descriptor set for compute shader
	e->blas_compute_set = R_AllocateDescriptorSet (&vulkan_globals.anim_compute_set_layout);

	// Set up descriptor set bindings (these don't change between frames)
	if (vulkan_globals.scratch_buffer != VK_NULL_HANDLE)
	{
		ZEROED_STRUCT_ARRAY (VkDescriptorBufferInfo, buffer_infos, 3);
		buffer_infos[0].buffer = hdr->vertex_buffer;
		buffer_infos[0].offset = 0;
		buffer_infos[0].range = VK_WHOLE_SIZE;

		// Binding 1: joints buffer (for MD5) or scratch buffer as dummy
		if (hdr->poseverttype == PV_MD5 && hdr->joints_buffer != VK_NULL_HANDLE)
		{
			buffer_infos[1].buffer = hdr->joints_buffer;
			buffer_infos[1].offset = 0;
			buffer_infos[1].range = VK_WHOLE_SIZE;
		}
		else
		{
			buffer_infos[1].buffer = vulkan_globals.scratch_buffer;
			buffer_infos[1].offset = 0;
			buffer_infos[1].range = VK_WHOLE_SIZE;
		}

		buffer_infos[2].buffer = vulkan_globals.scratch_buffer;
		buffer_infos[2].offset = 0;
		buffer_infos[2].range = VK_WHOLE_SIZE;

		ZEROED_STRUCT_ARRAY (VkWriteDescriptorSet, writes, 3);
		for (int j = 0; j < 3; ++j)
		{
			writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[j].dstSet = e->blas_compute_set;
			writes[j].dstBinding = j;
			writes[j].dstArrayElement = 0;
			writes[j].descriptorCount = 1;
			writes[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[j].pBufferInfo = &buffer_infos[j];
		}
		vkUpdateDescriptorSets (vulkan_globals.device, 3, writes, 0, NULL);
	}
}

/*
================
R_FreeEntityBLAS

Free acceleration structure for an entity
================
*/
void R_FreeEntityBLAS (entity_t *e)
{
	if (!e)
		return;
	if (e->blas == VK_NULL_HANDLE)
		return;

	// Add to garbage collection - resources will be freed after GPU is done with them
	AddBLASGarbage (e->blas, e->blas_buffer, e->blas_allocation, e->blas_compute_set, &vulkan_globals.anim_compute_set_layout);

	e->blas_compute_set = VK_NULL_HANDLE;
	e->blas = VK_NULL_HANDLE;
	e->blas_buffer = VK_NULL_HANDLE;
	e->blas_allocation = NULL;
	e->blas_address = 0;
	e->blas_build_scratch_size = 0;
	e->blas_model = NULL;
}

/*
================
R_FreeAllEntityBLAS

Free all entity BLASes. Called when RT shadows are disabled.
================
*/
void R_FreeAllEntityBLAS (void)
{
	if (!cl.entities)
		return;

	for (int i = 0; i < cl.num_entities; i++)
		R_FreeEntityBLAS (&cl.entities[i]);

	for (int i = 0; i < cl.num_statics; i++)
		R_FreeEntityBLAS (cl.static_entities[i]);
}

/*
================
R_SetupAliasFrameForBLAS

Sets up animation frame data for BLAS building.
This updates entity state (previouspose, currentpose, lerpstart) just like
R_SetupAliasFrame in r_alias.c, ensuring correct animation for ray tracing.
================
*/
static void R_SetupAliasFrameForBLAS (entity_t *e, aliashdr_t *paliashdr, int *pose1, int *pose2, float *blend)
{
	int frame = e->frame;
	if ((frame >= paliashdr->numframes) || (frame < 0))
		frame = 0;

	int posenum = paliashdr->frames[frame].firstpose;
	int numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		e->lerptime = paliashdr->frames[frame].interval;
		posenum += (int)(cl.time / e->lerptime) % numposes;
	}
	else
		e->lerptime = 0.1f;

	// Update entity animation state (mirroring R_SetupAliasFrame logic)
	if (e->lerpflags & LERP_RESETANIM)
	{
		e->lerpstart = 0;
		e->previouspose = posenum;
		e->currentpose = posenum;
		e->lerpflags -= LERP_RESETANIM;
	}
	else if (e->currentpose != posenum)
	{
		if (e->lerpflags & LERP_RESETANIM2)
		{
			e->lerpstart = 0;
			e->previouspose = posenum;
			e->currentpose = posenum;
			e->lerpflags -= LERP_RESETANIM2;
		}
		else
		{
			e->lerpstart = cl.time;
			e->previouspose = e->currentpose;
			e->currentpose = posenum;
		}
	}

	// Compute lerp values
	if (r_lerpmodels.value && !(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2))
	{
		if (e->lerpflags & LERP_FINISH && numposes == 1)
			*blend = CLAMP (0, (cl.time - e->lerpstart) / (e->lerpfinish - e->lerpstart), 1);
		else
			*blend = CLAMP (0, (cl.time - e->lerpstart) / e->lerptime, 1);

		if (*blend == 1.0f)
			e->previouspose = e->currentpose;

		*pose1 = e->previouspose;
		*pose2 = e->currentpose;

		// Clamp poses to valid range
		// For MD5, numposes is 1 (bind pose only), but we have numframes joint matrices
		int max_poses = (paliashdr->poseverttype == PV_MD5) ? paliashdr->numframes : paliashdr->numposes;
		if (*pose1 >= max_poses || *pose1 < 0)
			*pose1 = 0;
		if (*pose2 >= max_poses || *pose2 < 0)
			*pose2 = 0;
	}
	else
	{
		*blend = 1.0f;
		*pose1 = posenum;
		*pose2 = posenum;
	}
}

/*
================
R_UpdateAnimatedBLAS

Update all entity BLASes with animated vertex data.
This dispatches compute shaders to interpolate/skin vertices into the
scratch buffer, then builds/updates the BLASes.
================
*/
#define MAX_PENDING_BLAS_BUILDS 128

static VkAccelerationStructureGeometryKHR		   pending_geometries[MAX_PENDING_BLAS_BUILDS];
static VkAccelerationStructureBuildGeometryInfoKHR pending_build_infos[MAX_PENDING_BLAS_BUILDS];
static VkAccelerationStructureBuildRangeInfoKHR	   pending_range_infos[MAX_PENDING_BLAS_BUILDS];

/*
================
R_FlushPendingBLASBuilds

Flushes pending BLAS builds: inserts compute->AS barrier, builds all pending AS, then inserts appropriate barrier for next phase.
================
*/
static void R_FlushPendingBLASBuilds (cb_context_t *cbx, int num_pending, qboolean more_entities)
{
	if (num_pending == 0)
		return;

	// Barrier: compute writes -> AS reads
	ZEROED_STRUCT (VkMemoryBarrier, compute_barrier);
	compute_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	compute_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	compute_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	vulkan_globals.vk_cmd_pipeline_barrier (
		cbx->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &compute_barrier, 0, NULL, 0, NULL);

	// Build range info pointer array
	const VkAccelerationStructureBuildRangeInfoKHR *range_info_ptrs[MAX_PENDING_BLAS_BUILDS];
	for (int i = 0; i < num_pending; ++i)
		range_info_ptrs[i] = &pending_range_infos[i];

	// Single batched AS build call
	vulkan_globals.vk_cmd_build_acceleration_structures (cbx->cb, num_pending, pending_build_infos, range_info_ptrs);

	// Barrier for next phase
	ZEROED_STRUCT (VkMemoryBarrier, as_barrier);
	as_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	as_barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

	if (more_entities)
	{
		// More batches coming: need AS_READ for TLAS + SHADER_WRITE for next compute batch
		as_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT;
		vulkan_globals.vk_cmd_pipeline_barrier (
			cbx->cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &as_barrier, 0, NULL, 0, NULL);
	}
	else
	{
		// Final batch: only need AS_READ for TLAS build
		as_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		vulkan_globals.vk_cmd_pipeline_barrier (
			cbx->cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &as_barrier, 0, NULL,
			0, NULL);
	}
}

void R_UpdateAnimatedBLAS (cb_context_t *cbx)
{
	if (!vulkan_globals.ray_query)
		return;
	if (vulkan_globals.scratch_buffer == VK_NULL_HANDLE)
		return;

	const VkDeviceSize scratch_buffer_size = SCRATCH_BUFFER_SIZE_MB * 1024 * 1024;
	const VkDeviceSize as_alignment = vulkan_globals.physical_device_acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment;

	VkDeviceSize scratch_offset = 0;
	int			 num_pending = 0;
	int			 entity_index = 0;
	const int	 total_entities = cl.num_entities + cl.num_statics;

	R_BeginDebugUtilsLabel (cbx, "Update Animated BLAS");

	while (entity_index < total_entities)
	{
		// Phase 1: Compute - dispatch shaders and collect build info
		while (entity_index < total_entities && num_pending < MAX_PENDING_BLAS_BUILDS)
		{
			entity_t *e = (entity_index < cl.num_entities) ? &cl.entities[entity_index] : cl.static_entities[entity_index - cl.num_entities];
			++entity_index;

			if (!e->model || e->model->needload || e->model->type != mod_alias || e->blas == VK_NULL_HANDLE)
				continue;

			// Skip transparent entities (same as TLAS)
			if ((e->alpha != ENTALPHA_DEFAULT) && (ENTALPHA_DECODE (e->alpha) < 1.0f))
				continue;

			aliashdr_t *hdr = (aliashdr_t *)Mod_Extradata (e->model);
			if (!hdr || hdr->numverts_vbo == 0)
				continue;

			// Align for AS build scratch
			VkDeviceSize aligned_scratch_offset = q_align (scratch_offset, as_alignment);

			// Calculate space needed
			const VkDeviceSize vertex_size = hdr->numverts_vbo * sizeof (float) * 3;
			const VkDeviceSize scratch_size = e->blas_build_scratch_size;
			VkDeviceSize	   aligned_vertex_size = q_align (vertex_size, as_alignment);
			VkDeviceSize	   total_needed = aligned_vertex_size + scratch_size;

			// Check if entity fits in scratch buffer at all
			if (total_needed > scratch_buffer_size)
			{
				Con_DPrintf ("Entity BLAS too large for scratch buffer\n");
				continue;
			}

			// Check if we have space; if not, flush current batch and reset
			if (aligned_scratch_offset + total_needed > scratch_buffer_size)
			{
				// Need to flush - back up entity_index to retry this entity after flush
				--entity_index;
				break;
			}

			scratch_offset = aligned_scratch_offset;

			VkDeviceAddress vertex_output_address = vulkan_globals.scratch_buffer_address + scratch_offset;
			VkDeviceAddress scratch_address = vulkan_globals.scratch_buffer_address + scratch_offset + aligned_vertex_size;

			// Get lerp data
			int	  pose1, pose2;
			float blend;
			R_SetupAliasFrameForBLAS (e, hdr, &pose1, &pose2, &blend);

			// Use the descriptor set allocated per-entity (bindings don't change between frames)
			VkDescriptorSet anim_compute_set = e->blas_compute_set;

			// Dispatch compute shader
			vulkan_pipeline_t pipeline;
			uint32_t		  push_constants[12]; // Max size needed

			if (hdr->poseverttype == PV_MD5)
			{
				// MD5 skinning
				pipeline = vulkan_globals.skinning_pipeline;
				push_constants[0] = pose1 * hdr->numjoints;						 // joints_offset0
				push_constants[1] = pose2 * hdr->numjoints;						 // joints_offset1
				push_constants[2] = (uint32_t)(scratch_offset / sizeof (float)); // output_offset (in floats)
				push_constants[3] = hdr->numverts_vbo;							 // num_verts
				memcpy (&push_constants[4], &blend, sizeof (float));			 // blend_factor
			}
			else
			{
				// MDL/MD3 interpolation
				pipeline = vulkan_globals.mesh_interpolate_pipeline;
				push_constants[0] = pose1 * hdr->numverts_vbo;					 // pose1_offset (in vertices)
				push_constants[1] = pose2 * hdr->numverts_vbo;					 // pose2_offset (in vertices)
				push_constants[2] = (uint32_t)(scratch_offset / sizeof (float)); // output_offset (in floats)
				push_constants[3] = hdr->numverts_vbo;							 // num_verts
				memcpy (&push_constants[4], &blend, sizeof (float));			 // blend_factor
				push_constants[5] = (hdr->poseverttype == PV_QUAKE3) ? 0x4 : 0;	 // flags (bit 2 = MD3)
			}

			R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout.handle, 0, 1, &anim_compute_set, 0, NULL);
			R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, pipeline.layout.push_constant_range.size, push_constants);

			uint32_t num_groups = (hdr->numverts_vbo + 63) / 64;
			vkCmdDispatch (cbx->cb, num_groups, 1, 1);

			// Store build info for later
			VkAccelerationStructureGeometryKHR *geom = &pending_geometries[num_pending];
			memset (geom, 0, sizeof (*geom));
			geom->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
			geom->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			geom->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			geom->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			geom->geometry.triangles.vertexData.deviceAddress = vertex_output_address;
			geom->geometry.triangles.vertexStride = sizeof (float) * 3;
			geom->geometry.triangles.maxVertex = hdr->numverts_vbo;
			geom->geometry.triangles.indexType = VK_INDEX_TYPE_UINT16;
			geom->geometry.triangles.indexData.deviceAddress = hdr->index_buffer_address;

			VkAccelerationStructureBuildGeometryInfoKHR *build = &pending_build_infos[num_pending];
			memset (build, 0, sizeof (*build));
			build->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
			build->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			build->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
			build->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			build->dstAccelerationStructure = e->blas;
			build->geometryCount = 1;
			build->pGeometries = geom;
			build->scratchData.deviceAddress = scratch_address;

			VkAccelerationStructureBuildRangeInfoKHR *range = &pending_range_infos[num_pending];
			memset (range, 0, sizeof (*range));
			range->primitiveCount = hdr->numtris;

			++num_pending;

			scratch_offset += total_needed;
		}

		// Phase 2: Build - flush pending builds
		if (num_pending > 0)
		{
			qboolean more_entities = (entity_index < total_entities);
			R_FlushPendingBLASBuilds (cbx, num_pending, more_entities);
			num_pending = 0;
			scratch_offset = 0;
		}
	}

	R_EndDebugUtilsLabel (cbx);
}
