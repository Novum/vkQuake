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

static glheap_t *mesh_buffer_heap;

typedef struct
{
	VkBuffer				  buffer;
	VkDescriptorSet			  descriptor_set;
	glheapallocation_t		 *allocation;
	VkDescriptorSet			  desc_set;
	vulkan_desc_set_layout_t *desc_set_layout;
} buffer_garbage_t;

static int				current_garbage_index;
static int				num_garbage_buffers[2];
static buffer_garbage_t buffer_garbage[MAX_MODELS * 2][2];

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
void GLMesh_DeleteMeshBuffers (aliashdr_t *hdr)
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

/*
================
GLMesh_UploadBuffers
================
*/
void GLMesh_UploadBuffers (qmodel_t *m, aliashdr_t *mainhdr, unsigned short *indexes, byte *vertexes, aliasmesh_t *desc, jointpose_t *joints)
{
	size_t	 numindexes = 0;
	size_t	 numverts = 0;
	VkResult err;
	if (!mainhdr)
		return;

	// count how much space we're going to need.
	size_t totalvbosize = 0;
	for (const aliashdr_t *hdr = mainhdr; hdr != NULL; hdr = hdr->nextsurface)
	{
		switch (hdr->poseverttype)
		{
		case PV_QUAKE1:
			totalvbosize +=
				(hdr->numposes * hdr->numverts_vbo * sizeof (meshxyz_t)); // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
			break;
		case PV_MD5:
			totalvbosize += (hdr->numposes * hdr->numverts_vbo * sizeof (md5vert_t));
			break;
		}

		numverts += hdr->numverts_vbo;
		numindexes += hdr->numindexes;
	}

	const size_t totaljointssize = mainhdr->numframes * mainhdr->numjoints * sizeof (jointpose_t);

	assert ((mainhdr->nextsurface == NULL) || (mainhdr->poseverttype != PV_QUAKE1));
	if (mainhdr->poseverttype == PV_QUAKE1)
	{
		mainhdr->vbostofs = totalvbosize;
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
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &mainhdr->index_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)mainhdr->index_buffer, VK_OBJECT_TYPE_BUFFER, m->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, mainhdr->index_buffer, &memory_requirements);

		mainhdr->index_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		err = vkBindBufferMemory (
			vulkan_globals.device, mainhdr->index_buffer, GL_HeapGetAllocationMemory (mainhdr->index_allocation),
			GL_HeapGetAllocationOffset (mainhdr->index_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		R_StagingUploadBuffer (mainhdr->index_buffer, totalindexsize, (byte *)indexes);
	}

	// create the vertex buffer (empty)

	TEMP_ALLOC (byte, vbodata, totalvbosize);

	// fill in the vertices of the buffer
	size_t vertofs = 0;
	for (const aliashdr_t *hdr = mainhdr; hdr != NULL; hdr = hdr->nextsurface)
	{
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

					xyz[v].xyz[0] = trivert.v[0];
					xyz[v].xyz[1] = trivert.v[1];
					xyz[v].xyz[2] = trivert.v[2];
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
		case PV_MD5:
			assert (hdr->numposes == 1);
			memcpy (vbodata, vertexes, totalvbosize);
		}
	}

	// fill in the ST coords at the end of the buffer
	if (mainhdr->poseverttype == PV_QUAKE1)
	{
		meshst_t *st = (meshst_t *)(vbodata + mainhdr->vbostofs);
		for (int f = 0; f < mainhdr->numverts_vbo; f++)
		{
			st[f].st[0] = ((float)desc[f].st[0] + 0.5f) / (float)mainhdr->skinwidth;
			st[f].st[1] = ((float)desc[f].st[1] + 0.5f) / (float)mainhdr->skinheight;
		}
	}

	// Allocate vertex buffer & upload to GPU
	{
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = totalvbosize;
		buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &mainhdr->vertex_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)mainhdr->vertex_buffer, VK_OBJECT_TYPE_BUFFER, m->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, mainhdr->vertex_buffer, &memory_requirements);

		mainhdr->vertex_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		err = vkBindBufferMemory (
			vulkan_globals.device, mainhdr->vertex_buffer, GL_HeapGetAllocationMemory (mainhdr->vertex_allocation),
			GL_HeapGetAllocationOffset (mainhdr->vertex_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		R_StagingUploadBuffer (mainhdr->vertex_buffer, totalvbosize, vbodata);
	}

	// Allocate joints buffer & upload to GPU
	if (joints)
	{
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = totaljointssize;
		buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &mainhdr->joints_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)mainhdr->joints_buffer, VK_OBJECT_TYPE_BUFFER, m->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, mainhdr->joints_buffer, &memory_requirements);

		mainhdr->joints_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		err = vkBindBufferMemory (
			vulkan_globals.device, mainhdr->joints_buffer, GL_HeapGetAllocationMemory (mainhdr->joints_allocation),
			GL_HeapGetAllocationOffset (mainhdr->joints_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		R_StagingUploadBuffer (mainhdr->joints_buffer, totaljointssize, (byte *)joints);

		mainhdr->joints_set = R_AllocateDescriptorSet (&vulkan_globals.joints_buffer_set_layout);

		ZEROED_STRUCT (VkDescriptorBufferInfo, buffer_info);
		buffer_info.buffer = mainhdr->joints_buffer;
		buffer_info.offset = 0;
		buffer_info.range = VK_WHOLE_SIZE;

		ZEROED_STRUCT (VkWriteDescriptorSet, joints_set_write);
		joints_set_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		joints_set_write.dstSet = mainhdr->joints_set;
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

		for (int i = 0; i < 2; ++i)
			GLMesh_DeleteMeshBuffers ((aliashdr_t *)m->extradata[i]);
	}
}
