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
// r_misc.c

#include "quakedef.h"

//johnfitz -- new cvars
extern cvar_t r_clearcolor;
extern cvar_t r_drawflat;
extern cvar_t r_flatlightstyles;
extern cvar_t gl_fullbrights;
extern cvar_t gl_farclip;
extern cvar_t gl_overbright;
extern cvar_t gl_overbright_models;
extern cvar_t r_waterquality;
extern cvar_t r_oldwater;
extern cvar_t r_waterwarp;
extern cvar_t r_oldskyleaf;
extern cvar_t r_drawworld;
extern cvar_t r_showtris;
extern cvar_t r_showbboxes;
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_nolerp_list;
extern cvar_t r_noshadow_list;
//johnfitz
extern cvar_t gl_zfix; // QuakeSpasm z-fighting fix

extern gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz

vulkanglobals_t vulkan_globals;

#define STAGING_BUFFER_SIZE_KB	2048
#define NUM_STAGING_BUFFERS		2

typedef struct
{
	VkBuffer			buffer;
	VkCommandBuffer		command_buffer;
	VkFence				fence;
	int					current_offset;
	qboolean			submitted;
	unsigned char *		data;
} stagingbuffer_t;

static VkCommandPool	staging_command_pool;
static VkDeviceMemory	staging_memory;
static stagingbuffer_t	staging_buffers[NUM_STAGING_BUFFERS];
static int				current_staging_buffer = 0;

/*
================
GL_MemoryTypeFromProperties
================
*/
int GL_MemoryTypeFromProperties(uint32_t type_bits, VkFlags requirements_mask)
{
	for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
		if ((type_bits & 1) == 1)
		{
			if ((vulkan_globals.memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask)
				return i;
		}
		type_bits >>= 1;
	}

	Sys_Error("Could not find memory type");
	return 0;
}

/*
====================
GL_Overbright_f -- johnfitz
====================
*/
static void GL_Overbright_f (cvar_t *var)
{
	R_RebuildAllLightmaps ();
}

/*
====================
GL_Fullbrights_f -- johnfitz
====================
*/
static void GL_Fullbrights_f (cvar_t *var)
{
	TexMgr_ReloadNobrightImages ();
}

/*
====================
R_SetClearColor_f -- johnfitz
====================
*/
static void R_SetClearColor_f (cvar_t *var)
{
	byte	*rgb;
	int		s;

	s = (int)r_clearcolor.value & 0xFF;
	rgb = (byte*)(d_8to24table + s);
	vulkan_globals.clear_value.color.float32[0] = rgb[0]/255;
	vulkan_globals.clear_value.color.float32[1] = rgb[1]/255;
	vulkan_globals.clear_value.color.float32[2] = rgb[2]/255;
	vulkan_globals.clear_value.color.float32[3] = 0.0f;
}

/*
====================
R_Novis_f -- johnfitz
====================
*/
static void R_VisChanged (cvar_t *var)
{
	extern int vis_changed;
	vis_changed = 1;
}

/*
===============
R_Model_ExtraFlags_List_f -- johnfitz -- called when r_nolerp_list or r_noshadow_list cvar changes
===============
*/
static void R_Model_ExtraFlags_List_f (cvar_t *var)
{
	int i;
	for (i=0; i < MAX_MODELS; i++)
		Mod_SetExtraFlags (cl.model_precache[i]);
}

/*
====================
R_SetWateralpha_f -- ericw
====================
*/
static void R_SetWateralpha_f (cvar_t *var)
{
	map_wateralpha = var->value;
}

/*
====================
R_SetLavaalpha_f -- ericw
====================
*/
static void R_SetLavaalpha_f (cvar_t *var)
{
	map_lavaalpha = var->value;
}

/*
====================
R_SetTelealpha_f -- ericw
====================
*/
static void R_SetTelealpha_f (cvar_t *var)
{
	map_telealpha = var->value;
}

/*
====================
R_SetSlimealpha_f -- ericw
====================
*/
static void R_SetSlimealpha_f (cvar_t *var)
{
	map_slimealpha = var->value;
}

/*
====================
GL_WaterAlphaForSurfface -- ericw
====================
*/
float GL_WaterAlphaForSurface (msurface_t *fa)
{
	if (fa->flags & SURF_DRAWLAVA)
		return map_lavaalpha > 0 ? map_lavaalpha : map_wateralpha;
	else if (fa->flags & SURF_DRAWTELE)
		return map_telealpha > 0 ? map_telealpha : map_wateralpha;
	else if (fa->flags & SURF_DRAWSLIME)
		return map_slimealpha > 0 ? map_slimealpha : map_wateralpha;
	else
		return map_wateralpha;
}

/*
===============
R_InitStagingBuffers
===============
*/
void R_InitStagingBuffers()
{
	Con_Printf("Initializing staging\n");

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = STAGING_BUFFER_SIZE_KB * 1024;
	buffer_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	for(int i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		staging_buffers[i].current_offset = 0;
		staging_buffers[i].submitted = false;

		err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &staging_buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateBuffer failed");
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_globals.device, staging_buffers[0].buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ( ( memory_requirements.size % memory_requirements.alignment ) == 0 ) 
		? memory_requirements.size 
		: ( memory_requirements.size + memory_requirements.alignment - align_mod );

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_STAGING_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &staging_memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	for(int i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		err = vkBindBufferMemory(vulkan_globals.device, staging_buffers[i].buffer, staging_memory, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}

	unsigned char * data;
	err = vkMapMemory(vulkan_globals.device, staging_memory, 0, NUM_STAGING_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error("vkMapMemory failed");

	VkCommandPoolCreateInfo command_pool_create_info;
	memset(&command_pool_create_info, 0, sizeof(command_pool_create_info));
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

	err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &staging_command_pool);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateCommandPool failed");

	VkCommandBufferAllocateInfo command_buffer_allocate_info;
	memset(&command_buffer_allocate_info, 0, sizeof(command_buffer_allocate_info));
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = staging_command_pool;
	command_buffer_allocate_info.commandBufferCount = NUM_STAGING_BUFFERS;

	VkCommandBuffer command_buffers[NUM_STAGING_BUFFERS];
	err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateCommandBuffers failed");

	VkFenceCreateInfo fence_create_info;
	memset(&fence_create_info, 0, sizeof(fence_create_info));
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	VkCommandBufferBeginInfo command_buffer_begin_info;
	memset(&command_buffer_begin_info, 0, sizeof(command_buffer_begin_info));
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	for(int i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		err = vkCreateFence(vulkan_globals.device, &fence_create_info, NULL, &staging_buffers[i].fence);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateFence failed");

		staging_buffers[i].command_buffer = command_buffers[i];
		staging_buffers[i].data = data + (i * aligned_size);

		err = vkBeginCommandBuffer(staging_buffers[i].command_buffer, &command_buffer_begin_info);
		if (err != VK_SUCCESS)
			Sys_Error("vkBeginCommandBuffer failed");
	}
}

/*
===============
R_SubmitStagingBuffer
===============
*/
static void R_SubmitStagingBuffer(int index)
{
	vkEndCommandBuffer(staging_buffers[index].command_buffer);

	VkSubmitInfo submit_info;
	memset(&submit_info, 0, sizeof(submit_info));
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &staging_buffers[index].command_buffer;
	
	vkQueueSubmit(vulkan_globals.queue, 1, &submit_info, staging_buffers[index].fence);

	staging_buffers[index].submitted = true;
	current_staging_buffer = (current_staging_buffer + 1) % NUM_STAGING_BUFFERS;
}


/*
===============
R_SubmitStagingBuffers
===============
*/
void R_SubmitStagingBuffers()
{
	for (int i = 0; i<NUM_STAGING_BUFFERS; ++i)
	{
		if (!staging_buffers[i].submitted && staging_buffers[i].current_offset > 0)
			R_SubmitStagingBuffer(i);
	}
}

/*
===============
R_StagingAllocate
===============
*/
unsigned char * R_StagingAllocate(int size, VkCommandBuffer * command_buffer, VkBuffer * buffer, int * buffer_offset)
{
	if (size > (STAGING_BUFFER_SIZE_KB * 1024))
		Sys_Error("Cannot allocate staging buffer space");

	if ((staging_buffers[current_staging_buffer].current_offset + size) >= (STAGING_BUFFER_SIZE_KB * 1024) && !staging_buffers[current_staging_buffer].submitted)
		R_SubmitStagingBuffer(current_staging_buffer);

	stagingbuffer_t * staging_buffer = &staging_buffers[current_staging_buffer];
	if (staging_buffer->submitted)
	{
		VkResult err;

		vkWaitForFences(vulkan_globals.device, 1, &staging_buffer->fence, VK_TRUE, UINT64_MAX);

		err = vkResetFences(vulkan_globals.device, 1, &staging_buffer->fence);
		if (err != VK_SUCCESS)
			Sys_Error("vkResetFences failed");

		staging_buffer->current_offset = 0;
		staging_buffer->submitted = false;

		VkCommandBufferBeginInfo command_buffer_begin_info;
		memset(&command_buffer_begin_info, 0, sizeof(command_buffer_begin_info));
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		err = vkBeginCommandBuffer(staging_buffer->command_buffer, &command_buffer_begin_info);
		if (err != VK_SUCCESS)
			Sys_Error("vkBeginCommandBuffer failed");
	}

	*command_buffer = staging_buffer->command_buffer;
	*buffer = staging_buffer->buffer;
	*buffer_offset = staging_buffer->current_offset;

	unsigned char *data = staging_buffer->data + staging_buffer->current_offset;
	staging_buffer->current_offset += size;

	return data;
}

/*
===============
R_Init
===============
*/
void R_Init (void)
{
	extern cvar_t gl_finish;

	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);

	Cvar_RegisterVariable (&r_norefresh);
	Cvar_RegisterVariable (&r_lightmap);
	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawviewmodel);
	Cvar_RegisterVariable (&r_shadows);
	Cvar_RegisterVariable (&r_wateralpha);
	Cvar_SetCallback (&r_wateralpha, R_SetWateralpha_f);
	Cvar_RegisterVariable (&r_dynamic);
	Cvar_RegisterVariable (&r_novis);
	Cvar_SetCallback (&r_novis, R_VisChanged);
	Cvar_RegisterVariable (&r_speeds);
	Cvar_RegisterVariable (&r_pos);

	Cvar_RegisterVariable (&gl_finish);
	Cvar_RegisterVariable (&gl_clear);
	Cvar_RegisterVariable (&gl_cull);
	Cvar_RegisterVariable (&gl_smoothmodels);
	Cvar_RegisterVariable (&gl_affinemodels);
	Cvar_RegisterVariable (&gl_polyblend);
	Cvar_RegisterVariable (&gl_flashblend);
	Cvar_RegisterVariable (&gl_playermip);
	Cvar_RegisterVariable (&gl_nocolors);

	//johnfitz -- new cvars
	Cvar_RegisterVariable (&r_clearcolor);
	Cvar_SetCallback (&r_clearcolor, R_SetClearColor_f);
	Cvar_RegisterVariable (&r_waterquality);
	Cvar_RegisterVariable (&r_oldwater);
	Cvar_RegisterVariable (&r_waterwarp);
	Cvar_RegisterVariable (&r_drawflat);
	Cvar_RegisterVariable (&r_flatlightstyles);
	Cvar_RegisterVariable (&r_oldskyleaf);
	Cvar_SetCallback (&r_oldskyleaf, R_VisChanged);
	Cvar_RegisterVariable (&r_drawworld);
	Cvar_RegisterVariable (&r_showtris);
	Cvar_RegisterVariable (&r_showbboxes);
	Cvar_RegisterVariable (&gl_farclip);
	Cvar_RegisterVariable (&gl_fullbrights);
	Cvar_RegisterVariable (&gl_overbright);
	Cvar_SetCallback (&gl_fullbrights, GL_Fullbrights_f);
	Cvar_SetCallback (&gl_overbright, GL_Overbright_f);
	Cvar_RegisterVariable (&gl_overbright_models);
	Cvar_RegisterVariable (&r_lerpmodels);
	Cvar_RegisterVariable (&r_lerpmove);
	Cvar_RegisterVariable (&r_nolerp_list);
	Cvar_SetCallback (&r_nolerp_list, R_Model_ExtraFlags_List_f);
	Cvar_RegisterVariable (&r_noshadow_list);
	Cvar_SetCallback (&r_noshadow_list, R_Model_ExtraFlags_List_f);
	//johnfitz

	Cvar_RegisterVariable (&gl_zfix); // QuakeSpasm z-fighting fix
	Cvar_RegisterVariable (&r_lavaalpha);
	Cvar_RegisterVariable (&r_telealpha);
	Cvar_RegisterVariable (&r_slimealpha);
	Cvar_SetCallback (&r_lavaalpha, R_SetLavaalpha_f);
	Cvar_SetCallback (&r_telealpha, R_SetTelealpha_f);
	Cvar_SetCallback (&r_slimealpha, R_SetSlimealpha_f);

	R_InitParticles ();
	R_SetClearColor_f (&r_clearcolor); //johnfitz

	Sky_Init (); //johnfitz
	Fog_Init (); //johnfitz
}

/*
===============
R_TranslatePlayerSkin -- johnfitz -- rewritten.  also, only handles new colors, not new skins
===============
*/
void R_TranslatePlayerSkin (int playernum)
{
	int			top, bottom;

	top = (cl.scores[playernum].colors & 0xf0)>>4;
	bottom = cl.scores[playernum].colors &15;

	//FIXME: if gl_nocolors is on, then turned off, the textures may be out of sync with the scoreboard colors.
	if (!gl_nocolors.value)
		if (playertextures[playernum])
			TexMgr_ReloadImage (playertextures[playernum], top, bottom);
}

/*
===============
R_TranslateNewPlayerSkin -- johnfitz -- split off of TranslatePlayerSkin -- this is called when
the skin or model actually changes, instead of just new colors
added bug fix from bengt jardup
===============
*/
void R_TranslateNewPlayerSkin (int playernum)
{
	char		name[64];
	byte		*pixels;
	aliashdr_t	*paliashdr;
	int		skinnum;

//get correct texture pixels
	currententity = &cl_entities[1+playernum];

	if (!currententity->model || currententity->model->type != mod_alias)
		return;

	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);

	skinnum = currententity->skinnum;

	//TODO: move these tests to the place where skinnum gets received from the server
	if (skinnum < 0 || skinnum >= paliashdr->numskins)
	{
		Con_DPrintf("(%d): Invalid player skin #%d\n", playernum, skinnum);
		skinnum = 0;
	}

	pixels = (byte *)paliashdr + paliashdr->texels[skinnum]; // This is not a persistent place!

//upload new image
	q_snprintf(name, sizeof(name), "player_%i", playernum);
	playertextures[playernum] = TexMgr_LoadImage (currententity->model, name, paliashdr->skinwidth, paliashdr->skinheight,
		SRC_INDEXED, pixels, paliashdr->gltextures[skinnum][0]->source_file, paliashdr->gltextures[skinnum][0]->source_offset, TEXPREF_PAD | TEXPREF_OVERWRITE);

//now recolor it
	R_TranslatePlayerSkin (playernum);
}

/*
===============
R_NewGame -- johnfitz -- handle a game switch
===============
*/
void R_NewGame (void)
{
	int i;

	//clear playertexture pointers (the textures themselves were freed by texmgr_newgame)
	for (i=0; i<MAX_SCOREBOARD; i++)
		playertextures[i] = NULL;
}

/*
=============
R_ParseWorldspawn

called at map load
=============
*/
static void R_ParseWorldspawn (void)
{
	char key[128], value[4096];
	const char *data;

	map_wateralpha = r_wateralpha.value;
	map_lavaalpha = r_lavaalpha.value;
	map_telealpha = r_telealpha.value;
	map_slimealpha = r_slimealpha.value;

	data = COM_Parse(cl.worldmodel->entities);
	if (!data)
		return; // error
	if (com_token[0] != '{')
		return; // error
	while (1)
	{
		data = COM_Parse(data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			strcpy(key, com_token + 1);
		else
			strcpy(key, com_token);
		while (key[strlen(key)-1] == ' ') // remove trailing spaces
			key[strlen(key)-1] = 0;
		data = COM_Parse(data);
		if (!data)
			return; // error
		strcpy(value, com_token);

		if (!strcmp("wateralpha", key))
			map_wateralpha = atof(value);

		if (!strcmp("lavaalpha", key))
			map_lavaalpha = atof(value);

		if (!strcmp("telealpha", key))
			map_telealpha = atof(value);

		if (!strcmp("slimealpha", key))
			map_slimealpha = atof(value);
	}
}


/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int		i;

	for (i=0 ; i<256 ; i++)
		d_lightstylevalue[i] = 264;		// normal light value

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		cl.worldmodel->leafs[i].efrags = NULL;

	r_viewleaf = NULL;
	R_ClearParticles ();

	GL_BuildLightmaps ();
	GL_BuildBModelVertexBuffer ();
	//ericw -- no longer load alias models into a VBO here, it's done in Mod_LoadAliasModel

	r_framecount = 0; //johnfitz -- paranoid?
	r_visframecount = 0; //johnfitz -- paranoid?

	Sky_NewMap (); //johnfitz -- skybox in worldspawn
	Fog_NewMap (); //johnfitz -- global fog in worldspawn
	R_ParseWorldspawn (); //ericw -- wateralpha, lavaalpha, telealpha, slimealpha in worldspawn

	load_subdivide_size = gl_subdivide_size.value; //johnfitz -- is this the right place to set this?
}

/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int		i;
	float		start, stop, time;

	if (cls.state != ca_connected)
	{
		Con_Printf("Not connected to a server\n");
		return;
	}

	start = Sys_DoubleTime ();
	for (i = 0; i < 128; i++)
	{
		GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
		r_refdef.viewangles[1] = i/128.0*360.0;
		R_RenderView ();
		GL_EndRendering ();
	}

	//glFinish ();
	stop = Sys_DoubleTime ();
	time = stop-start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128/time);
}

void D_FlushCaches (void)
{
}

static int gl_num_programs;

/*
=============
GL_GetUniformLocation
=============
*/
/*GLint GL_GetUniformLocation (GLuint *programPtr, const char *name)
{
	GLint location;

	if (!programPtr)
		return -1;

	location = GL_GetUniformLocationFunc(*programPtr, name);
	if (location == -1)
	{
		Con_Warning("GL_GetUniformLocationFunc %s failed\n", name);
		*programPtr = 0;
	}
	return location;
}*/

/*
====================
GL_CreateProgram

Compiles and returns GLSL program.
====================
*/
/*GLuint GL_CreateProgram (const GLchar *vertSource, const GLchar *fragSource, int numbindings, const glsl_attrib_binding_t *bindings)
{
	int i;
	GLuint program, vertShader, fragShader;

	if (!gl_glsl_able)
		return 0;

	vertShader = GL_CreateShaderFunc (GL_VERTEX_SHADER);
	GL_ShaderSourceFunc (vertShader, 1, &vertSource, NULL);
	GL_CompileShaderFunc (vertShader);
	if (!GL_CheckShader (vertShader))
	{
		GL_DeleteShaderFunc (vertShader);
		return 0;
	}

	fragShader = GL_CreateShaderFunc (GL_FRAGMENT_SHADER);
	GL_ShaderSourceFunc (fragShader, 1, &fragSource, NULL);
	GL_CompileShaderFunc (fragShader);
	if (!GL_CheckShader (fragShader))
	{
		GL_DeleteShaderFunc (vertShader);
		GL_DeleteShaderFunc (fragShader);
		return 0;
	}

	program = GL_CreateProgramFunc ();
	GL_AttachShaderFunc (program, vertShader);
	GL_DeleteShaderFunc (vertShader);
	GL_AttachShaderFunc (program, fragShader);
	GL_DeleteShaderFunc (fragShader);
	
	for (i = 0; i < numbindings; i++)
	{
		GL_BindAttribLocationFunc (program, bindings[i].attrib, bindings[i].name);
	}
	
	GL_LinkProgramFunc (program);

	if (!GL_CheckProgram (program))
	{
		GL_DeleteProgramFunc (program);
		return 0;
	}
	else
	{
		if (gl_num_programs == (sizeof(gl_programs)/sizeof(GLuint)))
			Host_Error ("gl_programs overflow");

		gl_programs[gl_num_programs] = program;
		gl_num_programs++;

		return program;
	}
}*/

/*
====================
R_DeleteShaders

Deletes any GLSL programs that have been created.
====================
*/
void R_DeleteShaders (void)
{
	/*int i;

	for (i = 0; i < gl_num_programs; i++)
	{
		GL_DeleteProgramFunc (gl_programs[i]);
		gl_programs[i] = 0;
	}*/
	gl_num_programs = 0;
}

/*
====================
GL_BindBuffer

glBindBuffer wrapper
====================
*/
/*void GL_BindBuffer (GLenum target, GLuint buffer)
{
	GLuint *cache;

	if (!gl_vbo_able)
		return;
	
	switch (target)
	{
		case GL_ARRAY_BUFFER:
			cache = &current_array_buffer;
			break;
		case GL_ELEMENT_ARRAY_BUFFER:
			cache = &current_element_array_buffer;
			break;
		default:
			Host_Error("GL_BindBuffer: unsupported target %d", (int)target);
			return;
	}
	
	if (*cache != buffer)
	{
		*cache = buffer;
		GL_BindBufferFunc (target, *cache);
	}
}*/

/*
====================
GL_ClearBufferBindings

This must be called if you do anything that could make the cached bindings
invalid (e.g. manually binding, destroying the context).
====================
*/
void GL_ClearBufferBindings ()
{
}
