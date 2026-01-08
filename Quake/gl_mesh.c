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

typedef struct
{
	VkAccelerationStructureKHR blas;
	VkBuffer				   buffer;
	glheapallocation_t		  *allocation;
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
static void AddBLASGarbage (VkAccelerationStructureKHR blas, VkBuffer buffer, glheapallocation_t *allocation)
{
	int				garbage_index;
	blas_garbage_t *garbage;

	garbage_index = num_garbage_blas[current_garbage_index]++;
	garbage = &blas_garbage[garbage_index][current_garbage_index];
	garbage->blas = blas;
	garbage->buffer = buffer;
	garbage->allocation = allocation;
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
		buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
	if (!vulkan_globals.ray_query)
		return;
	if (e->blas != VK_NULL_HANDLE)
		return;
	if (!e->model || e->model->type != mod_alias)
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

	Con_Printf ("Allocating BLAS for %p %d verts\n", e, hdr->numverts_vbo);

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
	buffer_create_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;

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

	Con_Printf("Freeing BLAS for %p\n", e);

	// Add to garbage collection - resources will be freed after GPU is done with them
	AddBLASGarbage (e->blas, e->blas_buffer, e->blas_allocation);

	e->blas = VK_NULL_HANDLE;
	e->blas_buffer = VK_NULL_HANDLE;
	e->blas_allocation = NULL;
	e->blas_address = 0;
}
