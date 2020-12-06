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
//r_sprite.c -- sprite model rendering

#include "quakedef.h"

/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame (entity_t *currentent)
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int				i, numframes, frame;
	float			*pintervals, fullinterval, targettime, time;

	psprite = (msprite_t *) currentent->model->cache.data;
	frame = currentent->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_DrawSprite: no such frame %d for '%s'\n", frame, currentent->model->name);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		time = cl.time + currentent->syncbase;

	// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
	// are positive, so we don't have to worry about division by 0
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		for (i=0 ; i<(numframes-1) ; i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}

/*
=================
R_DrawSpriteModel -- johnfitz -- rewritten: now supports all orientations
=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	vec3_t			point, v_forward, v_right, v_up;
	msprite_t		*psprite;
	mspriteframe_t	*frame;
	float			*s_up, *s_right;
	float			angle, sr, cr;

	//TODO: frustum cull it?

	frame = R_GetSpriteFrame (e);
	psprite = (msprite_t *) currententity->model->cache.data;

	switch(psprite->type)
	{
	case SPR_VP_PARALLEL_UPRIGHT: //faces view plane, up is towards the heavens
		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;
		s_up = v_up;
		s_right = vright;
		break;
	case SPR_FACING_UPRIGHT: //faces camera origin, up is towards the heavens
		VectorSubtract(currententity->origin, r_origin, v_forward);
		v_forward[2] = 0;
		VectorNormalizeFast(v_forward);
		v_right[0] = v_forward[1];
		v_right[1] = -v_forward[0];
		v_right[2] = 0;
		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;
		s_up = v_up;
		s_right = v_right;
		break;
	case SPR_VP_PARALLEL: //faces view plane, up is towards the top of the screen
		s_up = vup;
		s_right = vright;
		break;
	case SPR_ORIENTED: //pitch yaw roll are independent of camera
		AngleVectors (currententity->angles, v_forward, v_right, v_up);
		s_up = v_up;
		s_right = v_right;
		break;
	case SPR_VP_PARALLEL_ORIENTED: //faces view plane, but obeys roll value
		angle = currententity->angles[ROLL] * M_PI_DIV_180;
		sr = sin(angle);
		cr = cos(angle);
		v_right[0] = vright[0] * cr + vup[0] * sr;
		v_right[1] = vright[1] * cr + vup[1] * sr;
		v_right[2] = vright[2] * cr + vup[2] * sr;
		v_up[0] = vright[0] * -sr + vup[0] * cr;
		v_up[1] = vright[1] * -sr + vup[1] * cr;
		v_up[2] = vright[2] * -sr + vup[2] * cr;
		s_up = v_up;
		s_right = v_right;
		break;
	default:
		return;
	}

	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(4 * sizeof(basicvertex_t), &buffer, &buffer_offset);

	memset(vertices, 255, 4 * sizeof(basicvertex_t));

	VectorMA (e->origin, frame->down, s_up, point);
	VectorMA (point, frame->left, s_right, point);
	vertices[0].position[0] = point[0];
	vertices[0].position[1] = point[1];
	vertices[0].position[2] = point[2];
	vertices[0].texcoord[0] = 0.0f;
	vertices[0].texcoord[1] = frame->tmax;

	VectorMA (e->origin, frame->up, s_up, point);
	VectorMA (point, frame->left, s_right, point);
	vertices[1].position[0] = point[0];
	vertices[1].position[1] = point[1];
	vertices[1].position[2] = point[2];
	vertices[1].texcoord[0] = 0.0f;
	vertices[1].texcoord[1] = 0.0f;

	VectorMA (e->origin, frame->up, s_up, point);
	VectorMA (point, frame->right, s_right, point);
	vertices[2].position[0] = point[0];
	vertices[2].position[1] = point[1];
	vertices[2].position[2] = point[2];
	vertices[2].texcoord[0] = frame->smax;
	vertices[2].texcoord[1] = 0.0f;

	VectorMA (e->origin, frame->down, s_up, point);
	VectorMA (point, frame->right, s_right, point);
	vertices[3].position[0] = point[0];
	vertices[3].position[1] = point[1];
	vertices[3].position[2] = point[2];
	vertices[3].texcoord[0] = frame->smax;
	vertices[3].texcoord[1] = frame->tmax;

	vkCmdBindVertexBuffers(vulkan_globals.command_buffer, 0, 1, &buffer, &buffer_offset);
	vkCmdBindIndexBuffer(vulkan_globals.command_buffer, vulkan_globals.fan_index_buffer, 0, VK_INDEX_TYPE_UINT16);

	R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.sprite_pipeline);

	if (psprite->type == SPR_ORIENTED)
		vkCmdSetDepthBias(vulkan_globals.command_buffer, OFFSET_DECAL, 0.0f, 0.0f);
	else
		vkCmdSetDepthBias(vulkan_globals.command_buffer, OFFSET_NONE, 0.0f, 0.0f);

	vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &frame->gltexture->descriptor_set, 0, NULL);
	vkCmdDrawIndexed(vulkan_globals.command_buffer, 6, 1, 0, 0, 0);
}
