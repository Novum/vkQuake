/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"
#include <sys/stat.h>

static void		 Mod_LoadSpriteModel (qmodel_t *mod, void *buffer);
static void		 Mod_LoadBrushModel (qmodel_t *mod, const char *loadname, void *buffer);
static void		 Mod_LoadAliasModel (qmodel_t *mod, void *buffer);
static void		 Mod_LoadMD5MeshModel (qmodel_t *mod, const void *buffer);
static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash);

cvar_t external_ents = {"external_ents", "1", CVAR_ARCHIVE};
cvar_t external_vis = {"external_vis", "1", CVAR_ARCHIVE};
cvar_t r_loadmd5models = {"r_loadmd5models", "1", CVAR_ARCHIVE};
cvar_t r_md5models = {"r_md5models", "1", CVAR_ARCHIVE};
cvar_t keepbmodelcache = {"keepbmodelcache", "1", CVAR_NONE};

static byte *mod_novis;
static int	 mod_novis_capacity;

static byte *mod_decompressed;
static int	 mod_decompressed_capacity;

#define MAX_MOD_KNOWN 2048 /*johnfitz -- was 512 */
qmodel_t mod_known[MAX_MOD_KNOWN];
int		 mod_numknown;

char   mod_loaded_map[MAX_QPATH];
off_t  mod_loaded_map_size;
time_t mod_loaded_map_time;
off_t  mod_loaded_map_lit_size;
time_t mod_loaded_map_lit_time;

texture_t *r_notexture_mip;	 // johnfitz -- moved here from r_main.c
texture_t *r_notexture_mip2; // johnfitz -- used for non-lightmapped surfs with a missing texture

/*
===============
ReadShortUnaligned
===============
*/
static short ReadShortUnaligned (byte *ptr)
{
	short temp;
	memcpy (&temp, ptr, sizeof (short));
	return LittleShort (temp);
}

/*
===============
ReadLongUnaligned
===============
*/
static int ReadLongUnaligned (byte *ptr)
{
	int temp;
	memcpy (&temp, ptr, sizeof (int));
	return LittleLong (temp);
}

/*
===============
ReadFloatUnaligned
===============
*/
static float ReadFloatUnaligned (byte *ptr)
{
	float temp;
	memcpy (&temp, ptr, sizeof (float));
	return LittleFloat (temp);
}

/*
===============
Mod_RefreshSkins_f
===============
*/
void Mod_RefreshSkins_f (cvar_t *var)
{
	for (int i = 0; i < cl.maxclients; ++i)
		R_TranslateNewPlayerSkin (i);
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	Cvar_RegisterVariable (&external_vis);
	Cvar_RegisterVariable (&external_ents);
	Cvar_RegisterVariable (&r_loadmd5models);
	Cvar_RegisterVariable (&r_md5models);
	Cvar_SetCallback (&r_md5models, Mod_RefreshSkins_f);
	Cvar_RegisterVariable (&keepbmodelcache);

	// johnfitz -- create notexture miptex
	r_notexture_mip = (texture_t *)Mem_Alloc (sizeof (texture_t));
	strcpy (r_notexture_mip->name, "notexture");
	r_notexture_mip->height = r_notexture_mip->width = 32;

	r_notexture_mip2 = (texture_t *)Mem_Alloc (sizeof (texture_t));
	strcpy (r_notexture_mip2->name, "notexture2");
	r_notexture_mip2->height = r_notexture_mip2->width = 32;
	// johnfitz
}

/*
===============
Mod_Extradata_CheckSkin

Caches the data if needed
===============
*/
void *Mod_Extradata_CheckSkin (qmodel_t *mod, int skinnum)
{
	Mod_LoadModel (mod, true);
	if (mod->type == mod_alias && mod->extradata[1])
	{
		if (r_md5models.value >= 3)
			return mod->extradata[1];
		if (r_md5models.value >= 2 && skinnum < ((aliashdr_t *)mod->extradata[1])->numskins)
			return mod->extradata[1];
		if (r_md5models.value && mod->md5_prio)
			return mod->extradata[1];
	}
	return mod->extradata[0];
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
void *Mod_Extradata (qmodel_t *mod)
{
	return Mod_Extradata_CheckSkin (mod, 0);
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (float *p, qmodel_t *model)
{
	mnode_t	 *node;
	float	  d;
	mplane_t *plane;

	if (!model || !model->nodes)
		Sys_Error ("Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p, plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	return NULL; // never reached
}

/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, qmodel_t *model)
{
	int	  c;
	byte *out;
	byte *outend;
	int	  row;

	row = (model->numleafs + 31) / 8;
	if (mod_decompressed == NULL || row > mod_decompressed_capacity)
	{
		mod_decompressed_capacity = row;
		mod_decompressed = (byte *)Mem_Realloc (mod_decompressed, mod_decompressed_capacity);
		if (!mod_decompressed)
			Sys_Error ("Mod_DecompressVis: realloc() failed on %d bytes", mod_decompressed_capacity);
	}
	out = mod_decompressed;
	outend = mod_decompressed + row;

	if (!in)
	{ // no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return mod_decompressed;
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		in += 2;
		if (c > row - (out - mod_decompressed))
			c = row -
				(out -
				 mod_decompressed); // now that we're dynamically allocating pvs buffers, we have to be more careful to avoid heap overflows with buggy maps.
		while (c)
		{
			if (out == outend)
			{
				if (!model->viswarn)
				{
					model->viswarn = true;
					Con_Warning ("Mod_DecompressVis: output overrun on model \"%s\"\n", model->name);
				}
				return mod_decompressed;
			}
			*out++ = 0;
			c--;
		}
	} while (out - mod_decompressed < row);

	return mod_decompressed;
}

/*
===================
Mod_LeafPVS
===================
*/
byte *Mod_LeafPVS (mleaf_t *leaf, qmodel_t *model)
{
	if (leaf == model->leafs)
		return Mod_NoVisPVS (model);
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

/*
===================
Mod_NoVisPVS
===================
*/
byte *Mod_NoVisPVS (qmodel_t *model)
{
	int pvsbytes;

	pvsbytes = (model->numleafs + 31) / 8;
	if (mod_novis == NULL || pvsbytes > mod_novis_capacity)
	{
		mod_novis_capacity = pvsbytes;
		mod_novis = (byte *)Mem_Realloc (mod_novis, mod_novis_capacity);
		if (!mod_novis)
			Sys_Error ("Mod_NoVisPVS: realloc() failed on %d bytes", mod_novis_capacity);
	}
	memset (mod_novis, 0xff, mod_novis_capacity);
	return mod_novis;
}

/*
===================
Mod_FreeSpriteMemory
===================
*/
static void Mod_FreeSpriteMemory (msprite_t *psprite)
{
	for (int i = 0; i < psprite->numframes; ++i)
	{
		if (psprite->frames[i].type == SPR_SINGLE)
		{
			SAFE_FREE (psprite->frames[i].frameptr);
		}
		else
		{
			mspritegroup_t *group = (mspritegroup_t *)psprite->frames[i].frameptr;
			for (int j = 0; j < group->numframes; ++j)
			{
				SAFE_FREE (group->frames[i]);
			}
			SAFE_FREE (psprite->frames[i].frameptr);
		}
	}
	psprite->numframes = 0;
}

/*
===================
Mod_FreeModelMemory
===================
*/
static void Mod_FreeModelMemory (qmodel_t *mod)
{
	if (mod->name[0] != '*')
	{
		if ((mod->type == mod_sprite) && (mod->extradata[0]))
			Mod_FreeSpriteMemory ((msprite_t *)mod->extradata[0]);
		// Last two ones are dummy textures
		for (int i = 0; i < mod->numtextures - 2; ++i)
			SAFE_FREE (mod->textures[i]);
		for (int i = 0; i < mod->numsurfaces; ++i)
			SAFE_FREE (mod->surfaces[i].polys);
		SAFE_FREE (mod->hulls[0].clipnodes);
		SAFE_FREE (mod->submodels);
		mod->numsubmodels = 0;
		SAFE_FREE (mod->planes);
		mod->numplanes = 0;
		SAFE_FREE (mod->leafs);
		mod->numleafs = 0;
		SAFE_FREE (mod->vertexes);
		mod->numvertexes = 0;
		SAFE_FREE (mod->edges);
		mod->numedges = 0;
		SAFE_FREE (mod->nodes);
		mod->numnodes = 0;
		SAFE_FREE (mod->texinfo);
		mod->numtexinfo = 0;
		SAFE_FREE (mod->surfaces);
		mod->numsurfaces = 0;
		SAFE_FREE (mod->surfedges);
		mod->numsurfedges = 0;
		SAFE_FREE (mod->clipnodes);
		mod->numclipnodes = 0;
		SAFE_FREE (mod->marksurfaces);
		mod->nummarksurfaces = 0;
		SAFE_FREE (mod->soa_leafbounds);
		SAFE_FREE (mod->surfvis);
		SAFE_FREE (mod->soa_surfplanes);
		SAFE_FREE (mod->textures);
		mod->numtextures = 0;
		SAFE_FREE (mod->visdata);
		SAFE_FREE (mod->lightdata);
		SAFE_FREE (mod->entities);
		for (int i = 0; i < 2; ++i)
			SAFE_FREE (mod->extradata[i]);
		SAFE_FREE (mod->water_surfs);
		mod->used_water_surfs = 0;
		mod->water_surfs_specials = 0;
	}
	else
		SAFE_FREE (mod->textures);

	if (!isDedicated)
		TexMgr_FreeTexturesForOwner (mod);
}

/*
===================
Mod_UnPrimeAll
===================
*/
void Mod_UnPrimeAll (void)
{
	int		  i;
	qmodel_t *mod;
	GL_DeleteBModelAccelerationStructures ();

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (mod->type == mod_brush && mod->primed)
		{
			mod->primed = false;
			SAFE_FREE (mod->surfvis);
			SAFE_FREE (mod->soa_leafbounds);
			SAFE_FREE (mod->soa_surfplanes);
			for (int j = 0; j < mod->numsurfaces; ++j)
				if (!(mod->surfaces[j].flags & SURF_DRAWTILED))
					SAFE_FREE (mod->surfaces[j].polys);
		}
	}

	InvalidateTraceLineCache ();
}

/*
==================
GetMapFileInfo
==================
*/
static void GetMapFileInfo (const char *path, off_t *size, time_t *time, off_t *lit_size, time_t *lit_time)
{
	// this does not account for external .vis changes for BSP29
	// (or for .ent files, but that's not important)
	struct stat file_info;
	FILE	   *f;

	COM_FOpenFile (path, &f, NULL);
	if (!f || fstat (fileno (f), &file_info))
	{
		if (f)
			fclose (f);
		*size = *time = *lit_size = *lit_time = 0;
		return;
	}
	*time = file_info.st_mtime;
	*size = file_info.st_size;
	fclose (f);

	char litfilename[MAX_OSPATH];
	q_strlcpy (litfilename, path, sizeof (litfilename));
	COM_StripExtension (litfilename, litfilename, sizeof (litfilename));
	q_strlcat (litfilename, ".lit", sizeof (litfilename));
	COM_FOpenFile (path, &f, NULL);
	if (!f || fstat (fileno (f), &file_info))
	{
		if (f)
			fclose (f);
		*lit_size = *lit_time = 0;
		return;
	}
	*lit_time = file_info.st_mtime;
	*lit_size = file_info.st_size;
	fclose (f);
}

/*
================
Mod_ClearBModelCaches

newmap contains the path of the map that is about to be loaded. If
it matches the last one, brush models are not freed.
================
*/
void Mod_ClearBModelCaches (const char *newmap)
{
	qboolean clear = !keepbmodelcache.value || strcmp (mod_loaded_map, newmap);

	off_t  size;
	time_t time;
	off_t  lit_size;
	time_t lit_time;
	GetMapFileInfo (newmap, &size, &time, &lit_size, &lit_time);
	if (size != mod_loaded_map_size || time != mod_loaded_map_time || lit_size != mod_loaded_map_lit_size || lit_time != mod_loaded_map_lit_time)
		clear = true;

	q_strlcpy (mod_loaded_map, newmap, sizeof (mod_loaded_map));
	mod_loaded_map_size = size;
	mod_loaded_map_time = time;
	mod_loaded_map_lit_size = lit_size;
	mod_loaded_map_lit_time = lit_time;

	Con_DPrintf ("%s bmodels\n", clear ? "Clearing" : "Keeping");

	if (clear)
	{
		Mod_ClearAll ();
		Sky_ClearAll ();
	}
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		  i;
	qmodel_t *mod;
	GL_DeleteBModelAccelerationStructures ();

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (mod->type != mod_alias)
		{
			mod->needload = true;
			mod->primed = false;
			Mod_FreeModelMemory (mod); // johnfitz
		}
	}

	InvalidateTraceLineCache ();
}

/*
===================
Mod_ResetAll
===================
*/
void Mod_ResetAll (void)
{
	int		  i;
	qmodel_t *mod;

	// ericw -- free alias model VBOs
	GLMesh_DeleteAllMeshBuffers ();
	GL_DeleteBModelAccelerationStructures ();

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->needload) // otherwise Mod_ClearAll() did it already
			Mod_FreeModelMemory (mod);

		memset (mod, 0, sizeof (qmodel_t));
	}
	mod_numknown = 0;

	memset (mod_loaded_map, 0, sizeof (mod_loaded_map));

	InvalidateTraceLineCache ();
}

/*
==================
Mod_FindName

==================
*/
qmodel_t *Mod_FindName (const char *name)
{
	int		  i;
	qmodel_t *mod;

	if (!name[0])
		Sys_Error ("Mod_FindName: NULL name"); // johnfitz -- was "Mod_ForName"

	//
	// search the currently loaded models
	//
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
		if (!strcmp (mod->name, name))
			break;

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
		q_strlcpy (mod->name, name, MAX_QPATH);
		mod->needload = true;
		mod_numknown++;
		InvalidateTraceLineCache ();
	}

	return mod;
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (const char *name)
{
	Mod_FindName (name);
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash)
{
	byte *buf = NULL;
	int	  mod_type;

	if (!mod->needload)
		return mod;

	InvalidateTraceLineCache ();

	if (mod->type == mod_alias)
	{
		for (int i = 0; i < 2; ++i)
			GLMesh_DeleteMeshBuffers ((aliashdr_t *)mod->extradata[i]);
	}

	//
	// load the file
	//
	if (r_loadmd5models.value)
	{
		char newname[MAX_QPATH];
		q_strlcpy (newname, mod->name, sizeof (newname));
		char *extension = (char *)COM_FileGetExtension (newname);
		if (strcmp (extension, "mdl") == 0)
		{
			q_strlcpy (extension, "md5mesh", sizeof (newname) - (extension - newname));
			buf = COM_LoadFile (newname, &mod->path_id);
			if (buf)
				Mod_LoadMD5MeshModel (mod, buf);
			Mem_Free (buf);
		}
	}

	unsigned int md5_path_id = mod->path_id;
	buf = COM_LoadFile (mod->name, &mod->path_id);
	if (!buf)
	{
		if (crash)
			Host_Error ("Mod_LoadModel: %s not found", mod->name); // johnfitz -- was "Mod_NumForName"
		return NULL;
	}
	mod->md5_prio = md5_path_id >= mod->path_id;

	//
	// allocate a new model
	//
	char loadname[256];
	COM_FileBase (mod->name, loadname, sizeof (loadname));

	//
	// fill it in
	//

	// call the apropriate loader
	mod->needload = false;

	mod_type = (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
	switch (mod_type)
	{
	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf);
		break;

	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;

	default:
		Mod_LoadBrushModel (mod, loadname, buf);
		break;
	}

	Mem_Free (buf);
	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
qmodel_t *Mod_ForName (const char *name, qboolean crash)
{
	qmodel_t *mod;

	mod = Mod_FindName (name);

	return Mod_LoadModel (mod, crash);
}

/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

/*
=================
Mod_CheckFullbrights -- johnfitz
=================
*/
qboolean Mod_CheckFullbrights (byte *pixels, int count)
{
	int i;
	for (i = 0; i < count; i++)
		if (*pixels++ > 223)
			return true;
	return false;
}

/*
=================
Mod_CheckAnimTextureArrayQ64

Quake64 bsp
Check if we have any missing textures in the array
=================
*/
qboolean Mod_CheckAnimTextureArrayQ64 (texture_t *anims[], int numTex)
{
	int i;

	for (i = 0; i < numTex; i++)
	{
		if (!anims[i])
			return false;
	}
	return true;
}

/*
=================
Mod_LoadTextureTask
=================
*/
static void Mod_LoadTextureTask (int i, qmodel_t **ppmod)
{
	qmodel_t  *mod = *ppmod;
	texture_t *tx = mod->textures[i];
	if (!tx)
		return;

	int	  pixels = tx->width * tx->height / 64 * 85;
	char  texturename[64];
	int	  fwidth, fheight;
	char  filename[MAX_OSPATH], mapname[MAX_OSPATH];
	byte *data = NULL;

	if (!q_strncasecmp (tx->name, "sky", 3)) // sky texture //also note -- was strncmp, changed to match qbsp
	{
		if (mod->bspversion == BSPVERSION_QUAKE64)
			Sky_LoadTextureQ64 (mod, tx, i);
		else
			Sky_LoadTexture (mod, tx, i);
	}
	else if (tx->name[0] == '*') // warping texture
	{
		// external textures -- first look in "textures/mapname/" then look in "textures/"
		COM_StripExtension (mod->name + 5, mapname, sizeof (mapname));
		q_snprintf (filename, sizeof (filename), "textures/%s/#%s", mapname, tx->name + 1); // this also replaces the '*' with a '#'
		enum srcformat fmt = SRC_RGBA;
		data = Image_LoadImage (filename, &fwidth, &fheight, &fmt);
		if (!data)
		{
			q_snprintf (filename, sizeof (filename), "textures/#%s", tx->name + 1);
			data = Image_LoadImage (filename, &fwidth, &fheight, &fmt);
		}

		// now load whatever we found
		if (data) // load external image
		{
			q_strlcpy (texturename, filename, sizeof (texturename));
			tx->gltexture = TexMgr_LoadImage (mod, texturename, fwidth, fheight, fmt, data, filename, 0, TEXPREF_NONE);
		}
		else // use the texture from the bsp file
		{
			q_snprintf (texturename, sizeof (texturename), "%s:%s", mod->name, tx->name);
			tx->gltexture =
				TexMgr_LoadImage (mod, texturename, tx->width, tx->height, SRC_INDEXED, (byte *)(tx + 1), mod->name, tx->source_offset, TEXPREF_NONE);
		}

		// now create the warpimage, using dummy data from the hunk to create the initial image
		q_snprintf (texturename, sizeof (texturename), "%s_warp", texturename);
		tx->warpimage = TexMgr_LoadImage (mod, texturename, WARPIMAGESIZE, WARPIMAGESIZE, SRC_RGBA, NULL, "", 0, TEXPREF_NOPICMIP | TEXPREF_WARPIMAGE);
		Atomic_StoreUInt32 (&tx->update_warp, true);
	}
	else // regular texture
	{
		// ericw -- fence textures
		int extraflags;

		extraflags = 0;
		if (tx->name[0] == '{')
			extraflags |= TEXPREF_ALPHA;
		// ericw

		// external textures -- first look in "textures/mapname/" then look in "textures/"
		COM_StripExtension (mod->name + 5, mapname, sizeof (mapname));
		q_snprintf (filename, sizeof (filename), "textures/%s/%s", mapname, tx->name);
		enum srcformat fmt = SRC_RGBA;
		data = Image_LoadImage (filename, &fwidth, &fheight, &fmt);
		if (!data)
		{
			q_snprintf (filename, sizeof (filename), "textures/%s", tx->name);
			data = Image_LoadImage (filename, &fwidth, &fheight, &fmt);
		}

		// now load whatever we found
		if (data) // load external image
		{
			char filename2[MAX_OSPATH];

			tx->gltexture = TexMgr_LoadImage (mod, filename, fwidth, fheight, fmt, data, filename, 0, TEXPREF_MIPMAP | extraflags);
			Mem_Free (data);

			// now try to load glow/luma image from the same place
			q_snprintf (filename2, sizeof (filename2), "%s_glow", filename);
			data = Image_LoadImage (filename2, &fwidth, &fheight, &fmt);
			if (!data)
			{
				q_snprintf (filename2, sizeof (filename2), "%s_luma", filename);
				data = Image_LoadImage (filename2, &fwidth, &fheight, &fmt);
			}

			if (data)
				tx->fullbright = TexMgr_LoadImage (mod, filename2, fwidth, fheight, fmt, data, filename2, 0, TEXPREF_MIPMAP | extraflags);
		}
		else // use the texture from the bsp file
		{
			q_snprintf (texturename, sizeof (texturename), "%s:%s", mod->name, tx->name);
			if (Mod_CheckFullbrights ((byte *)(tx + 1), pixels))
			{
				tx->gltexture = TexMgr_LoadImage (
					mod, texturename, tx->width, tx->height, SRC_INDEXED, (byte *)(tx + 1), mod->name, tx->source_offset,
					TEXPREF_MIPMAP | TEXPREF_NOBRIGHT | extraflags);
				q_snprintf (texturename, sizeof (texturename), "%s:%s_glow", mod->name, tx->name);
				tx->fullbright = TexMgr_LoadImage (
					mod, texturename, tx->width, tx->height, SRC_INDEXED, (byte *)(tx + 1), mod->name, tx->source_offset,
					TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT | extraflags);
			}
			else
			{
				tx->gltexture = TexMgr_LoadImage (
					mod, texturename, tx->width, tx->height, SRC_INDEXED, (byte *)(tx + 1), mod->name, tx->source_offset, TEXPREF_MIPMAP | extraflags);
			}
		}
	}
	Mem_Free (data);
}

/*
=================
Mod_LoadTextures
=================
*/
static void Mod_LoadTextures (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		   i, j, pixels, num, maxanim, altmax;
	miptex_t   mt;
	texture_t *tx, *tx2;
	texture_t *anims[10];
	texture_t *altanims[10];
	byte	  *m;
	byte	  *pixels_p;
	int		   nummiptex;
	int		   dataofs;

	// johnfitz -- don't return early if no textures; still need to create dummy texture
	if (!l->filelen)
	{
		Con_Printf ("Mod_LoadTextures: no textures in bsp file\n");
		nummiptex = 0;
		m = NULL; // avoid bogus compiler warning
	}
	else
	{
		m = mod_base + l->fileofs;
		nummiptex = ReadLongUnaligned (m + offsetof (dmiptexlump_t, nummiptex));
	}
	// johnfitz

	mod->numtextures = nummiptex + 2; // johnfitz -- need 2 dummy texture chains for missing textures
	mod->textures = (texture_t **)Mem_Alloc (mod->numtextures * sizeof (*mod->textures));

	for (i = 0; i < nummiptex; i++)
	{
		dataofs = ReadLongUnaligned (m + offsetof (dmiptexlump_t, dataofs[i]));
		if (dataofs == -1)
			continue;
		memcpy (&mt, m + dataofs, sizeof (miptex_t));
		mt.width = LittleLong (mt.width);
		mt.height = LittleLong (mt.height);
		for (j = 0; j < MIPLEVELS; j++)
			mt.offsets[j] = LittleLong (mt.offsets[j]);

		if (mt.width == 0 || mt.height == 0)
		{
			Con_Warning ("Zero sized texture %s in %s!\n", mt.name, mod->name);
			continue;
		}

		pixels = mt.width * mt.height / 64 * 85;
		tx = (texture_t *)Mem_Alloc (sizeof (texture_t) + pixels);
		mod->textures[i] = tx;

		memcpy (tx->name, mt.name, sizeof (tx->name));
		tx->width = mt.width;
		tx->height = mt.height;
		for (j = 0; j < MIPLEVELS; j++)
			tx->offsets[j] = mt.offsets[j] + sizeof (texture_t) - sizeof (miptex_t);
		// the pixels immediately follow the structures

		// ericw -- check for pixels extending past the end of the lump.
		// appears in the wild; e.g. jam2_tronyn.bsp (func_mapjam2),
		// kellbase1.bsp (quoth), and can lead to a segfault if we read past
		// the end of the .bsp file buffer
		pixels_p = m + dataofs + sizeof (miptex_t);
		if ((pixels_p + pixels) > (mod_base + l->fileofs + l->filelen))
		{
			Con_DPrintf ("Texture %s extends past end of lump\n", mt.name);
			pixels = q_max (0, (mod_base + l->fileofs + l->filelen) - pixels_p);
		}
		tx->source_offset = (src_offset_t)(pixels_p) - (src_offset_t)mod_base;

		Atomic_StoreUInt32 (&tx->update_warp, false); // johnfitz
		tx->warpimage = NULL;						  // johnfitz
		tx->fullbright = NULL;						  // johnfitz
		tx->shift = 0;								  // Q64 only

		if (mod->bspversion != BSPVERSION_QUAKE64)
		{
			memcpy (tx + 1, pixels_p, pixels);
		}
		else
		{ // Q64 bsp
			tx->shift = ReadLongUnaligned (m + dataofs + offsetof (miptex64_t, shift));
			memcpy (tx + 1, m + dataofs + sizeof (miptex64_t), pixels);
		}
	}

	if (!isDedicated)
	{
		if (!Tasks_IsWorker () && (nummiptex > 1))
		{
			task_handle_t task = Task_AllocateAssignIndexedFuncAndSubmit ((task_indexed_func_t)Mod_LoadTextureTask, nummiptex, &mod, sizeof (mod));
			Task_Join (task, SDL_MUTEX_MAXWAIT);
		}
		else
		{
			for (i = 0; i < nummiptex; i++)
				Mod_LoadTextureTask (i, &mod);
		}
	}

	// johnfitz -- last 2 slots in array should be filled with dummy textures
	mod->textures[mod->numtextures - 2] = r_notexture_mip;	// for lightmapped surfs
	mod->textures[mod->numtextures - 1] = r_notexture_mip2; // for SURF_DRAWTILED surfs

	//
	// sequence the animations
	//
	for (i = 0; i < nummiptex; i++)
	{
		tx = mod->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue; // allready sequenced

		// find the number of frames in the animation
		memset (anims, 0, sizeof (anims));
		memset (altanims, 0, sizeof (altanims));

		maxanim = tx->name[1];
		altmax = 0;
		if (maxanim >= 'a' && maxanim <= 'z')
			maxanim -= 'a' - 'A';
		if (maxanim >= '0' && maxanim <= '9')
		{
			maxanim -= '0';
			altmax = 0;
			anims[maxanim] = tx;
			maxanim++;
		}
		else if (maxanim >= 'A' && maxanim <= 'J')
		{
			altmax = maxanim - 'A';
			maxanim = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j = i + 1; j < nummiptex; j++)
		{
			tx2 = mod->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name + 2, tx->name + 2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num + 1 > maxanim)
					maxanim = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num + 1 > altmax)
					altmax = num + 1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}

		if (mod->bspversion == BSPVERSION_QUAKE64 && !Mod_CheckAnimTextureArrayQ64 (anims, maxanim))
			continue; // Just pretend this is a normal texture

#define ANIM_CYCLE 2
		// link them all together
		for (j = 0; j < maxanim; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s", j, tx->name);
			tx2->anim_total = maxanim * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j + 1) * ANIM_CYCLE;
			tx2->anim_next = anims[(j + 1) % maxanim];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j = 0; j < altmax; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s", j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j + 1) * ANIM_CYCLE;
			tx2->anim_next = altanims[(j + 1) % altmax];
			if (maxanim)
				tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadLighting -- johnfitz -- replaced with lit support code via lordhavoc
=================
*/
static void Mod_LoadLighting (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int			 i;
	byte		*in, *out, *data;
	byte		 d, q64_b0, q64_b1;
	char		 litfilename[MAX_OSPATH];
	unsigned int path_id;

	mod->lightdata = NULL;
	// LordHavoc: check for a .lit file
	q_strlcpy (litfilename, mod->name, sizeof (litfilename));
	COM_StripExtension (litfilename, litfilename, sizeof (litfilename));
	q_strlcat (litfilename, ".lit", sizeof (litfilename));
	data = (byte *)COM_LoadFile (litfilename, &path_id);
	if (data)
	{
		// use lit file only from the same gamedir as the map
		// itself or from a searchpath with higher priority.
		if (path_id < mod->path_id)
		{
			Con_DPrintf ("ignored %s from a gamedir with lower priority\n", litfilename);
		}
		else if (data[0] == 'Q' && data[1] == 'L' && data[2] == 'I' && data[3] == 'T')
		{
			i = ReadLongUnaligned (data + sizeof (int));
			if (i == 1)
			{
				if (8 + l->filelen * 3 == com_filesize)
				{
					Con_DPrintf2 ("%s loaded\n", litfilename);
					mod->lightdata = (byte *)Mem_AllocNonZero (l->filelen * 3);
					memcpy (mod->lightdata, data + 8, l->filelen * 3);
					Mem_Free (data);
					return;
				}
				Con_Printf ("Outdated .lit file (%s should be %u bytes, not %u)\n", litfilename, 8 + l->filelen * 3, com_filesize);
			}
			else
			{
				Con_Printf ("Unknown .lit file version (%d)\n", i);
			}
		}
		else
		{
			Con_Printf ("Corrupt .lit file (old version?), ignoring\n");
		}

		Mem_Free (data);
	}
	// LordHavoc: no .lit found, expand the white lighting data to color
	if (!l->filelen)
		return;

	// Quake64 bsp lighmap data
	if (mod->bspversion == BSPVERSION_QUAKE64)
	{
		// RGB lightmap samples are packed in 16bits.
		// RRRRR GGGGG BBBBBB

		mod->lightdata = (byte *)Mem_Alloc ((l->filelen / 2) * 3);
		in = mod_base + l->fileofs;
		out = mod->lightdata;

		for (i = 0; i < (l->filelen / 2); i++)
		{
			q64_b0 = *in++;
			q64_b1 = *in++;

			*out++ = q64_b0 & 0xf8;									  /* 0b11111000 */
			*out++ = ((q64_b0 & 0x07) << 5) + ((q64_b1 & 0xc0) >> 5); /* 0b00000111, 0b11000000 */
			*out++ = (q64_b1 & 0x3f) << 2;							  /* 0b00111111 */
		}
		return;
	}

	mod->lightdata = (byte *)Mem_Alloc (l->filelen * 3);
	in = mod->lightdata + l->filelen * 2; // place the file at the end, so it will not be overwritten until the very last write
	out = mod->lightdata;
	memcpy (in, mod_base + l->fileofs, l->filelen);
	for (i = 0; i < l->filelen; i++)
	{
		d = *in++;
		*out++ = d;
		*out++ = d;
		*out++ = d;
	}
}

/*
=================
Mod_LoadVisibility
=================
*/
static void Mod_LoadVisibility (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	mod->viswarn = false;
	if (!l->filelen)
	{
		mod->visdata = NULL;
		return;
	}
	mod->visdata = (byte *)Mem_Alloc (l->filelen);
	memcpy (mod->visdata, mod_base + l->fileofs, l->filelen);
}

/*
=================
Mod_LoadEntities
=================
*/
static void Mod_LoadEntities (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	char		 basemapname[MAX_QPATH];
	char		 entfilename[MAX_QPATH];
	char		*ents = NULL;
	unsigned int path_id;
	unsigned int crc = 0;
	qboolean	 versioned = true;

	if (!external_ents.value)
		goto _load_embedded;

	if (l->filelen > 0)
	{
		crc = CRC_Block (mod_base + l->fileofs, l->filelen - 1);
	}

	q_strlcpy (basemapname, mod->name, sizeof (basemapname));
	COM_StripExtension (basemapname, basemapname, sizeof (basemapname));

	q_snprintf (entfilename, sizeof (entfilename), "%s@%04x.ent", basemapname, crc);
	Con_DPrintf2 ("trying to load %s\n", entfilename);
	ents = (char *)COM_LoadFile (entfilename, &path_id);

	if (!ents)
	{
		q_snprintf (entfilename, sizeof (entfilename), "%s.ent", basemapname);
		Con_DPrintf2 ("trying to load %s\n", entfilename);
		ents = (char *)COM_LoadFile (entfilename, &path_id);
		versioned = false;
	}

	if (ents)
	{
		// use ent file only from the same gamedir as the map
		// itself or from a searchpath with higher priority
		// unless we got a CRC match
		if (versioned == false && path_id < mod->path_id)
		{
			Con_DPrintf ("ignored %s from a gamedir with lower priority\n", entfilename);
		}
		else
		{
			mod->entities = ents;
			Con_DPrintf ("Loaded external entity file %s\n", entfilename);
			return;
		}
	}

_load_embedded:
	if (!l->filelen)
	{
		Mem_Free (mod->entities);
		mod->entities = NULL;
		return;
	}
	mod->entities = (char *)Mem_Alloc (l->filelen);
	memcpy (mod->entities, mod_base + l->fileofs, l->filelen);
	Mem_Free (ents);
}

/*
=================
Mod_LoadVertexes
=================
*/
static void Mod_LoadVertexes (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	byte	  *in;
	mvertex_t *out;
	int		   i, count;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dvertex_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (dvertex_t);
	out = (mvertex_t *)Mem_Alloc (count * sizeof (*out));

	mod->vertexes = out;
	mod->numvertexes = count;

	for (i = 0; i < count; i++, in += sizeof (dvertex_t), out++)
	{
		out->position[0] = ReadFloatUnaligned (in + offsetof (dvertex_t, point[0]));
		out->position[1] = ReadFloatUnaligned (in + offsetof (dvertex_t, point[1]));
		out->position[2] = ReadFloatUnaligned (in + offsetof (dvertex_t, point[2]));
	}
}

/*
=================
Mod_LoadEdges
=================
*/
static void Mod_LoadEdges (qmodel_t *mod, byte *mod_base, lump_t *l, int bsp2)
{
	medge_t *out;
	int		 i, count;

	if (bsp2)
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof (dledge_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (dledge_t);
		out = (medge_t *)Mem_Alloc ((count + 1) * sizeof (*out));

		mod->edges = out;
		mod->numedges = count;

		for (i = 0; i < count; i++, in += sizeof (dledge_t), out++)
		{
			out->v[0] = ReadLongUnaligned (in + offsetof (dledge_t, v[0]));
			out->v[1] = ReadLongUnaligned (in + offsetof (dledge_t, v[1]));
		}
	}
	else
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof (dsedge_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (dsedge_t);
		out = (medge_t *)Mem_Alloc ((count + 1) * sizeof (*out));

		mod->edges = out;
		mod->numedges = count;

		for (i = 0; i < count; i++, in += sizeof (dsedge_t), out++)
		{
			out->v[0] = (unsigned short)ReadShortUnaligned (in + offsetof (dsedge_t, v[0]));
			out->v[1] = (unsigned short)ReadShortUnaligned (in + offsetof (dsedge_t, v[1]));
		}
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
static void Mod_LoadTexinfo (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	byte	   *in;
	mtexinfo_t *out;
	int			i, j, count, miptex;
	int			missing = 0; // johnfitz

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (texinfo_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (texinfo_t);
	out = (mtexinfo_t *)Mem_Alloc (count * sizeof (*out));

	mod->texinfo = out;
	mod->numtexinfo = count;

	for (i = 0; i < count; i++, in += sizeof (texinfo_t), out++)
	{
		for (j = 0; j < 4; j++)
		{
			out->vecs[0][j] = ReadFloatUnaligned (in + offsetof (texinfo_t, vecs[0][j]));
			out->vecs[1][j] = ReadFloatUnaligned (in + offsetof (texinfo_t, vecs[1][j]));
		}

		miptex = ReadLongUnaligned (in + offsetof (texinfo_t, miptex));
		out->flags = ReadLongUnaligned (in + offsetof (texinfo_t, flags));

		// johnfitz -- rewrote this section
		if (miptex >= mod->numtextures - 1 || !mod->textures[miptex])
		{
			if (out->flags & TEX_SPECIAL)
				out->texture = mod->textures[mod->numtextures - 1];
			else
				out->texture = mod->textures[mod->numtextures - 2];
			out->flags |= TEX_MISSING;
			missing++;
			out->tex_idx = -1;
		}
		else
		{
			out->texture = mod->textures[miptex];
			out->tex_idx = miptex;
		}
		// johnfitz
	}

	// johnfitz: report missing textures
	if (missing && mod->numtextures > 1)
		Con_Printf ("Mod_LoadTexinfo: %d texture(s) missing from BSP file\n", missing);
	// johnfitz
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
static void CalcSurfaceExtents (qmodel_t *mod, msurface_t *s)
{
	float		mins[2], maxs[2], val;
	int			i, j, e;
	mvertex_t  *v;
	mtexinfo_t *tex;
	int			bmins[2], bmaxs[2];

	mins[0] = mins[1] = FLT_MAX;
	maxs[0] = maxs[1] = -FLT_MAX;

	tex = s->texinfo;

	const double tex_vecs[2][4] = {
		{tex->vecs[0][0], tex->vecs[0][1], tex->vecs[0][2], tex->vecs[0][3]},
		{tex->vecs[1][0], tex->vecs[1][1], tex->vecs[1][2], tex->vecs[1][3]},
	};

	for (i = 0; i < s->numedges; i++)
	{
		e = mod->surfedges[s->firstedge + i];
		if (e >= 0)
			v = &mod->vertexes[mod->edges[e].v[0]];
		else
			v = &mod->vertexes[mod->edges[-e].v[1]];

		for (j = 0; j < 2; j++)
		{
			/* The following calculation is sensitive to floating-point
			 * precision.  It needs to produce the same result that the
			 * light compiler does, because R_BuildLightMap uses surf->
			 * extents to know the width/height of a surface's lightmap,
			 * and incorrect rounding here manifests itself as patches
			 * of "corrupted" looking lightmaps.
			 * Most light compilers are win32 executables, so they use
			 * x87 floating point.  This means the multiplies and adds
			 * are done at 80-bit precision, and the result is rounded
			 * down to 32-bits and stored in val.
			 * Adding the casts to double seems to be good enough to fix
			 * lighting glitches when Quakespasm is compiled as x86_64
			 * and using SSE2 floating-point.  A potential trouble spot
			 * is the hallway at the beginning of mfxsp17.  -- ericw
			 */
			val = ((double)v->position[0] * tex_vecs[j][0]) + ((double)v->position[1] * tex_vecs[j][1]) + ((double)v->position[2] * tex_vecs[j][2]) +
				  tex_vecs[j][3];

			mins[j] = q_min (mins[j], val);
			maxs[j] = q_max (maxs[j], val);
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = floor (mins[i] / 16);
		bmaxs[i] = ceil (maxs[i] / 16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

		if (!(tex->flags & TEX_SPECIAL) && s->extents[i] > 2000) // johnfitz -- was 512 in glquake, 256 in winquake
			Sys_Error ("Bad surface extents");
	}
}

/*
================
Mod_PolyForUnlitSurface -- johnfitz -- creates polys for unlightmapped surfaces (sky and water)

TODO: merge this into BuildSurfaceDisplayList?
================
*/
static void Mod_PolyForUnlitSurface (qmodel_t *mod, msurface_t *fa)
{
	vec3_t	  verts[64];
	int		  numverts, i, lindex;
	float	 *vec;
	glpoly_t *poly;
	float	 *poly_vert;
	float	  texscale;

	if (fa->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
		texscale = (1.0 / 128.0); // warp animation repeats every 128
	else
		texscale = (1.0 / 32.0); // to match r_notexture_mip

	// convert edges back to a normal polygon
	numverts = 0;
	for (i = 0; i < fa->numedges; i++)
	{
		lindex = mod->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = mod->vertexes[mod->edges[lindex].v[0]].position;
		else
			vec = mod->vertexes[mod->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	// create the poly
	poly = (glpoly_t *)Mem_Alloc (sizeof (glpoly_t) + (numverts - 4) * VERTEXSIZE * sizeof (float));
	poly->next = NULL;
	fa->polys = poly;
	poly->numverts = numverts;
	for (i = 0, vec = (float *)verts; i < numverts; i++, vec += 3)
	{
		poly_vert = &poly->verts[0][0] + (i * VERTEXSIZE);
		VectorCopy (vec, poly_vert);
		poly_vert[3] = DotProduct (vec, fa->texinfo->vecs[0]) * texscale;
		poly_vert[4] = DotProduct (vec, fa->texinfo->vecs[1]) * texscale;
	}
}

/*
================
Mod_CalcSurfaceExtents
================
*/
static void Mod_CalcSurfaceExtentsTask (int surfnum, qmodel_t **mod_ptr)
{
	qmodel_t *mod = *mod_ptr;
	CalcSurfaceExtents (mod, &mod->surfaces[surfnum]);
}

/*
=================
Mod_LoadFaces
=================
*/
static void Mod_LoadFaces (qmodel_t *mod, byte *mod_base, lump_t *l, qboolean bsp2)
{
	byte	   *ins;
	byte	   *inl;
	msurface_t *out;
	int			i, count, surfnum, lofs;
	int			planenum, side, texinfon;

	if (bsp2)
	{
		ins = NULL;
		inl = mod_base + l->fileofs;
		if (l->filelen % sizeof (dlface_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
		count = l->filelen / sizeof (dlface_t);
	}
	else
	{
		ins = mod_base + l->fileofs;
		inl = NULL;
		if (l->filelen % sizeof (dsface_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
		count = l->filelen / sizeof (dsface_t);
	}
	out = (msurface_t *)Mem_AllocNonZero (count * sizeof (*out));

	// johnfitz -- warn mappers about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i faces exceeds standard limit of 32767.\n", count);
	// johnfitz

	mod->surfaces = out;
	mod->numsurfaces = count;

	for (surfnum = 0; surfnum < count; surfnum++, out++)
	{
		if (bsp2)
		{
			out->firstedge = ReadLongUnaligned (inl + offsetof (dlface_t, firstedge));
			out->numedges = ReadLongUnaligned (inl + offsetof (dlface_t, numedges));
			planenum = ReadLongUnaligned (inl + offsetof (dlface_t, planenum));
			side = ReadLongUnaligned (inl + offsetof (dlface_t, side));
			texinfon = ReadLongUnaligned (inl + offsetof (dlface_t, texinfo));
			for (i = 0; i < MAXLIGHTMAPS; i++)
			{
				out->styles[i] = *(inl + offsetof (dlface_t, styles[i]));
				if (out->styles[i] >= MAX_LIGHTSTYLES && out->styles[i] != 255)
				{
					Con_Warning ("Invalid lightstyle %d\n", out->styles[i]);
					out->styles[i] = 0;
				}
				byte j = out->styles[i];
				if (j < 255)
					out->styles_bitmap |= 1 << (j < 16 ? j : j % 16 + 16);
			}
			lofs = ReadLongUnaligned (inl + offsetof (dlface_t, lightofs));
			inl += sizeof (dlface_t);
		}
		else
		{
			out->firstedge = ReadLongUnaligned (ins + offsetof (dsface_t, firstedge));
			out->numedges = ReadShortUnaligned (ins + offsetof (dsface_t, numedges));
			planenum = ReadShortUnaligned (ins + offsetof (dsface_t, planenum));
			side = ReadShortUnaligned (ins + offsetof (dsface_t, side));
			texinfon = ReadShortUnaligned (ins + offsetof (dsface_t, texinfo));
			for (i = 0; i < MAXLIGHTMAPS; i++)
			{
				out->styles[i] = *(ins + offsetof (dsface_t, styles[i]));
				if (out->styles[i] >= MAX_LIGHTSTYLES && out->styles[i] != 255)
				{
					Con_Warning ("Invalid lightstyle %d\n", out->styles[i]);
					out->styles[i] = 0;
				}
				byte j = out->styles[i];
				if (j < 255)
					out->styles_bitmap |= 1 << (j < 16 ? j : j % 16 + 16);
			}
			lofs = ReadLongUnaligned (ins + offsetof (dsface_t, lightofs));
			ins += sizeof (dsface_t);
		}

		if (!out->styles_bitmap)
			out->styles_bitmap = 1;

		out->flags = 0;
		out->polys = NULL;

		if (side)
			out->flags |= SURF_PLANEBACK;

		out->plane = mod->planes + planenum;

		out->texinfo = mod->texinfo + texinfon;

		// lighting info
		if (mod->bspversion == BSPVERSION_QUAKE64)
			lofs /= 2; // Q64 samples are 16bits instead 8 in normal Quake

		if (lofs == -1)
			out->samples = NULL;
		else
			out->samples = mod->lightdata + (lofs * 3); // johnfitz -- lit support via lordhavoc (was "+ i")

		// johnfitz -- this section rewritten
		out->lightmaptexturenum = -1;
		if (!q_strncasecmp (out->texinfo->texture->name, "sky", 3)) // sky surface //also note -- was strncmp, changed to match qbsp
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
			Mod_PolyForUnlitSurface (mod, out); // no more subdivision
		}
		else if (out->texinfo->texture->name[0] == '*') // warp surface
		{
			out->flags |= SURF_DRAWTURB;

			if (out->texinfo->flags & TEX_SPECIAL)
				out->flags |= SURF_DRAWTILED; // unlit water

			// detect special liquid types
			if (!strncmp (out->texinfo->texture->name, "*lava", 5))
				out->flags |= SURF_DRAWLAVA;
			else if (!strncmp (out->texinfo->texture->name, "*slime", 6))
				out->flags |= SURF_DRAWSLIME;
			else if (!strncmp (out->texinfo->texture->name, "*tele", 5))
				out->flags |= SURF_DRAWTELE;
			else
				out->flags |= SURF_DRAWWATER;

			if (out->flags & SURF_DRAWTILED)
				Mod_PolyForUnlitSurface (mod, out);
		}
		else if (out->texinfo->texture->name[0] == '{') // ericw -- fence textures
		{
			out->flags |= SURF_DRAWFENCE;
		}
		else if (out->texinfo->flags & TEX_MISSING) // texture is missing from bsp
		{
			out->flags |= SURF_NOTEXTURE;
			qboolean missing_samples = !out->samples && out->styles[0] != 255;
			qboolean unlit_texture = out->texinfo->flags & TEX_SPECIAL;

			if (!unlit_texture && missing_samples)
			{
				// unlit surf in a lit texture (mod->numtextures - 2: r_notexture_mip instead of r_notexture_mip2)
				Con_Warning ("Mod_LoadFaces: TEX_MISSING without TEX_SPECIAL missing lightmap samples");
				out->lightmaptexturenum = 0; // set a lightmaptexturenum to at least avoid a crash
			}

			if (unlit_texture || missing_samples) // not lightmapped
			{
				out->flags |= SURF_DRAWTILED;
				Mod_PolyForUnlitSurface (mod, out);
			}
		}
		// johnfitz
	}

	if (!isDedicated)
	{
		if (!Tasks_IsWorker () && (count > 1))
		{
			task_handle_t task = Task_AllocateAssignIndexedFuncAndSubmit ((task_indexed_func_t)Mod_CalcSurfaceExtentsTask, count, &mod, sizeof (qmodel_t *));
			Task_Join (task, SDL_MUTEX_MAXWAIT);
		}
		else
		{
			for (i = 0; i < count; i++)
				Mod_CalcSurfaceExtentsTask (i, &mod);
		}
	}
}

/*
=================
Mod_LoadNodes
=================
*/
static void Mod_LoadNodes_S (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		 i, j, count, p;
	byte	*in;
	mnode_t *out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dsnode_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (dsnode_t);
	out = (mnode_t *)Mem_Alloc (count * sizeof (*out));

	// johnfitz -- warn mappers about exceeding old limits
	if (count > 32767)
		Con_DWarning ("%i nodes exceeds standard limit of 32767.\n", count);
	// johnfitz

	mod->nodes = out;
	mod->numnodes = count;

	for (i = 0; i < count; i++, in += sizeof (dsnode_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof (dsnode_t, mins[j]));
			out->minmaxs[3 + j] = ReadShortUnaligned (in + offsetof (dsnode_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dsnode_t, planenum));
		out->plane = mod->planes + p;

		out->firstsurface = (unsigned short)ReadShortUnaligned (in + offsetof (dsnode_t, firstface)); // johnfitz -- explicit cast as unsigned short
		out->numsurfaces = (unsigned short)ReadShortUnaligned (in + offsetof (dsnode_t, numfaces));	  // johnfitz -- explicit cast as unsigned short

		for (j = 0; j < 2; j++)
		{
			// johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = (unsigned short)ReadShortUnaligned (in + offsetof (dsnode_t, children[j]));
			if (p < count)
				out->children[j] = mod->nodes + p;
			else
			{
				p = 65535 - p; // note this uses 65535 intentionally, -1 is leaf 0
				if (p < mod->numleafs)
					out->children[j] = (mnode_t *)(mod->leafs + p);
				else
				{
					Con_Printf ("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, mod->numleafs);
					out->children[j] = (mnode_t *)(mod->leafs); // map it to the solid leaf
				}
			}
			// johnfitz
		}
	}
}

static void Mod_LoadNodes_L1 (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		 i, j, count, p;
	byte	*in;
	mnode_t *out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dl1node_t))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s", mod->name);

	count = l->filelen / sizeof (dl1node_t);
	out = (mnode_t *)Mem_Alloc (count * sizeof (*out));

	mod->nodes = out;
	mod->numnodes = count;

	for (i = 0; i < count; i++, in += sizeof (dl1node_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof (dl1node_t, mins[j]));
			out->minmaxs[3 + j] = ReadShortUnaligned (in + offsetof (dl1node_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dl1node_t, planenum));
		out->plane = mod->planes + p;

		out->firstsurface = ReadLongUnaligned (in + offsetof (dl1node_t, firstface)); // johnfitz -- explicit cast as unsigned short
		out->numsurfaces = ReadLongUnaligned (in + offsetof (dl1node_t, numfaces));	  // johnfitz -- explicit cast as unsigned short

		for (j = 0; j < 2; j++)
		{
			// johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = ReadLongUnaligned (in + offsetof (dl1node_t, children[j]));
			if (p >= 0 && p < count)
				out->children[j] = mod->nodes + p;
			else
			{
				p = 0xffffffff - p; // note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < mod->numleafs)
					out->children[j] = (mnode_t *)(mod->leafs + p);
				else
				{
					Con_Printf ("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, mod->numleafs);
					out->children[j] = (mnode_t *)(mod->leafs); // map it to the solid leaf
				}
			}
			// johnfitz
		}
	}
}

static void Mod_LoadNodes_L2 (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		 i, j, count, p;
	byte	*in;
	mnode_t *out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dl2node_t))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s", mod->name);

	count = l->filelen / sizeof (dl2node_t);
	out = (mnode_t *)Mem_Alloc (count * sizeof (*out));

	mod->nodes = out;
	mod->numnodes = count;

	for (i = 0; i < count; i++, in += sizeof (dl2node_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadFloatUnaligned (in + offsetof (dl2node_t, mins[j]));
			out->minmaxs[3 + j] = ReadFloatUnaligned (in + offsetof (dl2node_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dl2node_t, planenum));
		out->plane = mod->planes + p;

		out->firstsurface = ReadLongUnaligned (in + offsetof (dl2node_t, firstface)); // johnfitz -- explicit cast as unsigned short
		out->numsurfaces = ReadLongUnaligned (in + offsetof (dl2node_t, numfaces));	  // johnfitz -- explicit cast as unsigned short

		for (j = 0; j < 2; j++)
		{
			// johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = ReadLongUnaligned (in + offsetof (dl2node_t, children[j]));
			if (p > 0 && p < count)
				out->children[j] = mod->nodes + p;
			else
			{
				p = 0xffffffff - p; // note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < mod->numleafs)
					out->children[j] = (mnode_t *)(mod->leafs + p);
				else
				{
					Con_Printf ("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, mod->numleafs);
					out->children[j] = (mnode_t *)(mod->leafs); // map it to the solid leaf
				}
			}
			// johnfitz
		}
	}
}

static void Mod_LoadNodes (qmodel_t *mod, byte *mod_base, lump_t *l, int bsp2)
{
	if (bsp2 == 2)
		Mod_LoadNodes_L2 (mod, mod_base, l);
	else if (bsp2)
		Mod_LoadNodes_L1 (mod, mod_base, l);
	else
		Mod_LoadNodes_S (mod, mod_base, l);
}

static void Mod_ProcessLeafs_S (qmodel_t *mod, byte *in, int filelen)
{
	mleaf_t *out;
	int		 i, j, count, p;

	if (filelen % sizeof (dsleaf_t))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", mod->name);
	count = filelen / sizeof (dsleaf_t);
	out = (mleaf_t *)Mem_Alloc (count * sizeof (*out));

	// johnfitz
	if (count > 32767)
		Host_Error ("Mod_LoadLeafs: %i leafs exceeds limit of 32767.", count);
	// johnfitz

	mod->leafs = out;
	mod->numleafs = count;

	for (i = 0; i < count; i++, in += sizeof (dsleaf_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof (dsleaf_t, mins[j]));
			out->minmaxs[3 + j] = ReadShortUnaligned (in + offsetof (dsleaf_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dsleaf_t, contents));
		out->contents = p;

		out->firstmarksurface =
			mod->marksurfaces + (unsigned short)ReadShortUnaligned (in + offsetof (dsleaf_t, firstmarksurface)); // johnfitz -- unsigned short
		out->nummarksurfaces = (unsigned short)ReadShortUnaligned (in + offsetof (dsleaf_t, nummarksurfaces));	 // johnfitz -- unsigned short

		p = ReadLongUnaligned (in + offsetof (dsleaf_t, visofs));
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = (mod->visdata != NULL) ? (mod->visdata + p) : NULL;
		out->efrags = NULL;

		for (j = 0; j < 4; j++)
			out->ambient_sound_level[j] = *(in + offsetof (dsleaf_t, ambient_level[j]));

		// johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

static void Mod_ProcessLeafs_L1 (qmodel_t *mod, byte *in, int filelen)
{
	mleaf_t *out;
	int		 i, j, count, p;

	if (filelen % sizeof (dl1leaf_t))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", mod->name);

	count = filelen / sizeof (dl1leaf_t);

	out = (mleaf_t *)Mem_Alloc (count * sizeof (*out));

	mod->leafs = out;
	mod->numleafs = count;

	for (i = 0; i < count; i++, in += sizeof (dl1leaf_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof (dl1leaf_t, mins[j]));
			out->minmaxs[3 + j] = ReadShortUnaligned (in + offsetof (dl1leaf_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dl1leaf_t, contents));
		out->contents = p;

		out->firstmarksurface = mod->marksurfaces + ReadLongUnaligned (in + offsetof (dl1leaf_t, firstmarksurface)); // johnfitz -- unsigned short
		out->nummarksurfaces = ReadLongUnaligned (in + offsetof (dl1leaf_t, nummarksurfaces));						 // johnfitz -- unsigned short

		p = ReadLongUnaligned (in + offsetof (dl1leaf_t, visofs));
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = mod->visdata + p;
		out->efrags = NULL;

		for (j = 0; j < 4; j++)
			out->ambient_sound_level[j] = *(in + offsetof (dl1leaf_t, ambient_level[j]));

		// johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

static void Mod_ProcessLeafs_L2 (qmodel_t *mod, byte *in, int filelen)
{
	mleaf_t *out;
	int		 i, j, count, p;

	if (filelen % sizeof (dl2leaf_t))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", mod->name);

	count = filelen / sizeof (dl2leaf_t);

	out = (mleaf_t *)Mem_Alloc (count * sizeof (*out));

	mod->leafs = out;
	mod->numleafs = count;

	for (i = 0; i < count; i++, in += sizeof (dl2leaf_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadFloatUnaligned (in + offsetof (dl2leaf_t, mins[j]));
			out->minmaxs[3 + j] = ReadFloatUnaligned (in + offsetof (dl2leaf_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dl2leaf_t, contents));
		out->contents = p;

		out->firstmarksurface = mod->marksurfaces + ReadLongUnaligned (in + offsetof (dl2leaf_t, firstmarksurface)); // johnfitz -- unsigned short
		out->nummarksurfaces = ReadLongUnaligned (in + offsetof (dl2leaf_t, nummarksurfaces));						 // johnfitz -- unsigned short

		p = ReadLongUnaligned (in + offsetof (dl2leaf_t, visofs));
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = mod->visdata + p;
		out->efrags = NULL;

		for (j = 0; j < 4; j++)
			out->ambient_sound_level[j] = *(in + offsetof (dl2leaf_t, ambient_level[j]));

		// johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

/*
=================
Mod_LoadLeafs
=================
*/
static void Mod_LoadLeafs (qmodel_t *mod, byte *mod_base, lump_t *l, int bsp2)
{
	void *in = (void *)(mod_base + l->fileofs);

	if (bsp2 == 2)
		Mod_ProcessLeafs_L2 (mod, in, l->filelen);
	else if (bsp2)
		Mod_ProcessLeafs_L1 (mod, in, l->filelen);
	else
		Mod_ProcessLeafs_S (mod, in, l->filelen);
}

/*
=================
Mod_CheckWaterVis
=================
*/
static void Mod_CheckWaterVis (qmodel_t *mod)
{
	mleaf_t	   *leaf, *other;
	msurface_t *surf;
	int			i, j, k;
	int			numclusters = mod->submodels[0].visleafs;
	int			contentfound = 0;
	int			contenttransparent = 0;
	int			contenttype;
	unsigned	hascontents = 0;

	if (r_novis.value)
	{ // all can be
		mod->contentstransparent = (SURF_DRAWWATER | SURF_DRAWTELE | SURF_DRAWSLIME | SURF_DRAWLAVA);
		return;
	}

	// pvs is 1-based. leaf 0 sees all (the solid leaf).
	// leaf 0 has no pvs, and does not appear in other leafs either, so watch out for the biases.
	for (i = 0, leaf = mod->leafs + 1; i < numclusters - 1; i++, leaf++)
	{
		byte *vis;
		if (leaf->contents < 0) // err... wtf?
			hascontents = 0;
		if (leaf->contents == CONTENTS_WATER)
		{
			if ((contenttransparent & (SURF_DRAWWATER | SURF_DRAWTELE)) == (SURF_DRAWWATER | SURF_DRAWTELE))
				continue;
			// this check is somewhat risky, but we should be able to get away with it.
			for (contenttype = 0, j = 0; j < leaf->nummarksurfaces; j++)
			{
				surf = &mod->surfaces[leaf->firstmarksurface[j]];
				if (surf->flags & (SURF_DRAWWATER | SURF_DRAWTELE))
				{
					contenttype = surf->flags & (SURF_DRAWWATER | SURF_DRAWTELE);
					break;
				}
			}
			// its possible that this leaf has absolutely no surfaces in it, turb or otherwise.
			if (contenttype == 0)
				continue;
		}
		else if (leaf->contents == CONTENTS_SLIME)
			contenttype = SURF_DRAWSLIME;
		else if (leaf->contents == CONTENTS_LAVA)
			contenttype = SURF_DRAWLAVA;
		// fixme: tele
		else
			continue;
		if (contenttransparent & contenttype)
		{
		nextleaf:
			continue; // found one of this type already
		}
		contentfound |= contenttype;
		vis = Mod_DecompressVis (leaf->compressed_vis, mod);
		for (j = 0; j < (numclusters + 7) / 8; j++)
		{
			if (vis[j])
			{
				for (k = 0; k < 8; k++)
				{
					if (vis[j] & (1u << k))
					{
						other = &mod->leafs[(j << 3) + k + 1];
						if (leaf->contents != other->contents)
						{
							//							Con_Printf("%p:%i sees %p:%i\n", leaf, leaf->contents, other, other->contents);
							contenttransparent |= contenttype;
							goto nextleaf;
						}
					}
				}
			}
		}
	}

	if (!contenttransparent)
	{ // no water leaf saw a non-water leaf
		// but only warn when there's actually water somewhere there...
		if (hascontents & ((1 << -CONTENTS_WATER) | (1 << -CONTENTS_SLIME) | (1 << -CONTENTS_LAVA)))
			Con_DPrintf ("%s is not watervised\n", mod->name);
	}
	else
	{
		Con_DPrintf2 ("%s is vised for transparent", mod->name);
		if (contenttransparent & SURF_DRAWWATER)
			Con_DPrintf2 (" water");
		if (contenttransparent & SURF_DRAWTELE)
			Con_DPrintf2 (" tele");
		if (contenttransparent & SURF_DRAWLAVA)
			Con_DPrintf2 (" lava");
		if (contenttransparent & SURF_DRAWSLIME)
			Con_DPrintf2 (" slime");
		Con_DPrintf2 ("\n");
	}
	// any types that we didn't find are assumed to be transparent.
	// this allows submodels to work okay (eg: ad uses func_illusionary teleporters for some reason).
	mod->contentstransparent = contenttransparent | (~contentfound & (SURF_DRAWWATER | SURF_DRAWTELE | SURF_DRAWSLIME | SURF_DRAWLAVA));
}

/*
=================
Mod_LoadClipnodes
=================
*/
static void Mod_LoadClipnodes (qmodel_t *mod, byte *mod_base, lump_t *l, qboolean bsp2)
{
	byte *ins;
	byte *inl;

	mclipnode_t *out; // johnfitz -- was dclipnode_t
	int			 i, count;
	hull_t		*hull;

	if (bsp2)
	{
		ins = NULL;
		inl = mod_base + l->fileofs;
		if (l->filelen % sizeof (dlclipnode_t))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (dlclipnode_t);
	}
	else
	{
		ins = mod_base + l->fileofs;
		inl = NULL;
		if (l->filelen % sizeof (dsclipnode_t))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (dsclipnode_t);
	}
	out = (mclipnode_t *)Mem_Alloc (count * sizeof (*out));

	// johnfitz -- warn about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i clipnodes exceeds standard limit of 32767.\n", count);
	// johnfitz

	mod->clipnodes = out;
	mod->numclipnodes = count;

	hull = &mod->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &mod->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

	if (bsp2)
	{
		for (i = 0; i < count; i++, out++, inl += sizeof (dlclipnode_t))
		{
			out->planenum = ReadLongUnaligned (inl + offsetof (dlclipnode_t, planenum));

			// johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= mod->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			// johnfitz

			out->children[0] = ReadLongUnaligned (inl + offsetof (dlclipnode_t, children[0]));
			out->children[1] = ReadLongUnaligned (inl + offsetof (dlclipnode_t, children[1]));
			// Spike: FIXME: bounds check
		}
	}
	else
	{
		for (i = 0; i < count; i++, out++, ins += sizeof (dsclipnode_t))
		{
			out->planenum = ReadLongUnaligned (ins + offsetof (dsclipnode_t, planenum));

			// johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= mod->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			// johnfitz

			// johnfitz -- support clipnodes > 32k
			out->children[0] = (unsigned short)ReadShortUnaligned (ins + offsetof (dsclipnode_t, children[0]));
			out->children[1] = (unsigned short)ReadShortUnaligned (ins + offsetof (dsclipnode_t, children[1]));

			if (out->children[0] >= count)
				out->children[0] -= 65536;
			if (out->children[1] >= count)
				out->children[1] -= 65536;
			// johnfitz
		}
	}
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
static void Mod_MakeHull0 (qmodel_t *mod)
{
	mnode_t		*in, *child;
	mclipnode_t *out; // johnfitz -- was dclipnode_t
	int			 i, j, count;
	hull_t		*hull;

	hull = &mod->hulls[0];

	in = mod->nodes;
	count = mod->numnodes;
	out = (mclipnode_t *)Mem_Alloc (count * sizeof (*out));

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->planes;

	for (i = 0; i < count; i++, out++, in++)
	{
		out->planenum = in->plane - mod->planes;
		for (j = 0; j < 2; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - mod->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
static void Mod_LoadMarksurfaces (qmodel_t *mod, byte *mod_base, lump_t *l, int bsp2)
{
	int	 i, j, count;
	int *out;
	if (bsp2)
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof (unsigned int))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (unsigned int);
		out = (int *)Mem_Alloc (count * sizeof (*out));

		mod->marksurfaces = out;
		mod->nummarksurfaces = count;

		for (i = 0; i < count; i++)
		{
			j = ReadLongUnaligned (in + (i * sizeof (int)));
			if (j >= mod->numsurfaces)
				Host_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = j;
		}
	}
	else
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof (short))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (short);
		out = (int *)Mem_Alloc (count * sizeof (*out));

		mod->marksurfaces = out;
		mod->nummarksurfaces = count;

		// johnfitz -- warn mappers about exceeding old limits
		if (count > 32767)
			Con_DWarning ("%i marksurfaces exceeds standard limit of 32767.\n", count);
		// johnfitz

		for (i = 0; i < count; i++)
		{
			j = (unsigned short)ReadShortUnaligned (in + (i * sizeof (short))); // johnfitz -- explicit cast as unsigned short
			if (j >= mod->numsurfaces)
				Sys_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = j;
		}
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
static void Mod_LoadSurfedges (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int	  i, count;
	byte *in;
	int	 *out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (int))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (int);
	out = (int *)Mem_Alloc (count * sizeof (int));

	mod->surfedges = out;
	mod->numsurfedges = count;

	for (i = 0; i < count; i++)
	{
		out[i] = ReadLongUnaligned (in + (i * sizeof (int)));
	}
}

/*
=================
Mod_LoadPlanes
=================
*/
static void Mod_LoadPlanes (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		  i, j;
	mplane_t *out;
	byte	 *in;
	int		  count;
	int		  bits;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dplane_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (dplane_t);
	out = (mplane_t *)Mem_Alloc (count * 2 * sizeof (*out));

	mod->planes = out;
	mod->numplanes = count;

	for (i = 0; i < count; i++, in += sizeof (dplane_t), out++)
	{
		bits = 0;
		for (j = 0; j < 3; j++)
		{
			out->normal[j] = ReadFloatUnaligned (in + offsetof (dplane_t, normal[j]));
			if (out->normal[j] < 0)
				bits |= 1 << j;
		}

		out->dist = ReadFloatUnaligned (in + offsetof (dplane_t, dist));
		out->type = ReadLongUnaligned (in + offsetof (dplane_t, type));
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int	   i;
	vec3_t corner;

	for (i = 0; i < 3; i++)
	{
		corner[i] = fabs (mins[i]) > fabs (maxs[i]) ? fabs (mins[i]) : fabs (maxs[i]);
	}

	return VectorLength (corner);
}

/*
=================
Mod_LoadSubmodels
=================
*/
static void Mod_LoadSubmodels (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	byte	 *in;
	dmodel_t *out;
	int		  i, j, count;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dmodel_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (dmodel_t);
	out = (dmodel_t *)Mem_Alloc (count * sizeof (*out));

	mod->submodels = out;
	mod->numsubmodels = count;

	for (i = 0; i < count; i++, in += sizeof (dmodel_t), out++)
	{
		for (j = 0; j < 3; j++)
		{ // spread the mins / maxs by a pixel
			out->mins[j] = ReadFloatUnaligned (in + offsetof (dmodel_t, mins[j])) - 1;
			out->maxs[j] = ReadFloatUnaligned (in + offsetof (dmodel_t, maxs[j])) + 1;
			out->origin[j] = ReadFloatUnaligned (in + offsetof (dmodel_t, origin[j]));
		}
		for (j = 0; j < MAX_MAP_HULLS; j++)
		{
			out->headnode[j] = ReadLongUnaligned (in + offsetof (dmodel_t, headnode[j]));
		}
		out->visleafs = ReadLongUnaligned (in + offsetof (dmodel_t, visleafs));
		out->firstface = ReadLongUnaligned (in + offsetof (dmodel_t, firstface));
		out->numfaces = ReadLongUnaligned (in + offsetof (dmodel_t, numfaces));
	}

	// johnfitz -- check world visleafs -- adapted from bjp
	out = mod->submodels;

	if (out->visleafs > 8192)
		Con_DWarning ("%i visleafs exceeds standard limit of 8192.\n", out->visleafs);
	// johnfitz
}

/*
=================
Mod_BoundsFromClipNode -- johnfitz

update the model's clipmins and clipmaxs based on each node's plane.

This works because of the way brushes are expanded in hull generation.
Each brush will include all six axial planes, which bound that brush.
Therefore, the bounding box of the hull can be constructed entirely
from axial planes found in the clipnodes for that hull.
=================
*/
#if 0  /* disabled for now -- see in Mod_SetupSubmodels()  */
static void Mod_BoundsFromClipNode (qmodel_t *mod, int hull, int nodenum)
{
	mplane_t    *plane;
	mclipnode_t *node;

	if (nodenum < 0)
		return; // hit a leafnode

	node = &mod->clipnodes[nodenum];
	plane = mod->hulls[hull].planes + node->planenum;
	switch (plane->type)
	{

	case PLANE_X:
		if (plane->signbits == 1)
			mod->clipmins[0] = q_min (mod->clipmins[0], -plane->dist - mod->hulls[hull].clip_mins[0]);
		else
			mod->clipmaxs[0] = q_max (mod->clipmaxs[0], plane->dist - mod->hulls[hull].clip_maxs[0]);
		break;
	case PLANE_Y:
		if (plane->signbits == 2)
			mod->clipmins[1] = q_min (mod->clipmins[1], -plane->dist - mod->hulls[hull].clip_mins[1]);
		else
			mod->clipmaxs[1] = q_max (mod->clipmaxs[1], plane->dist - mod->hulls[hull].clip_maxs[1]);
		break;
	case PLANE_Z:
		if (plane->signbits == 4)
			mod->clipmins[2] = q_min (mod->clipmins[2], -plane->dist - mod->hulls[hull].clip_mins[2]);
		else
			mod->clipmaxs[2] = q_max (mod->clipmaxs[2], plane->dist - mod->hulls[hull].clip_maxs[2]);
		break;
	default:
		// skip nonaxial planes; don't need them
		break;
	}

	Mod_BoundsFromClipNode (mod, hull, node->children[0]);
	Mod_BoundsFromClipNode (mod, hull, node->children[1]);
}
#endif /* #if 0 */

/* EXTERNAL VIS FILE SUPPORT:
 */
typedef struct vispatch_s
{
	char mapname[32];
	int	 filelen; // length of data after header (VIS+Leafs)
} vispatch_t;
#define VISPATCH_HEADER_LEN 36

static FILE *Mod_FindVisibilityExternal (qmodel_t *mod, const char *loadname)
{
	vispatch_t	 header;
	char		 visfilename[MAX_QPATH];
	const char	*shortname;
	unsigned int path_id;
	FILE		*f;
	long		 pos;
	size_t		 r;

	q_snprintf (visfilename, sizeof (visfilename), "maps/%s.vis", loadname);
	if (COM_FOpenFile (visfilename, &f, &path_id) < 0)
	{
		Con_DPrintf ("%s not found, trying ", visfilename);
		q_snprintf (visfilename, sizeof (visfilename), "%s.vis", COM_SkipPath (com_gamedir));
		Con_DPrintf ("%s\n", visfilename);
		if (COM_FOpenFile (visfilename, &f, &path_id) < 0)
		{
			Con_DPrintf ("external vis not found\n");
			return NULL;
		}
	}
	if (path_id < mod->path_id)
	{
		fclose (f);
		Con_DPrintf ("ignored %s from a gamedir with lower priority\n", visfilename);
		return NULL;
	}

	Con_DPrintf ("Found external VIS %s\n", visfilename);

	shortname = COM_SkipPath (mod->name);
	pos = 0;
	while ((r = fread (&header, 1, VISPATCH_HEADER_LEN, f)) == VISPATCH_HEADER_LEN)
	{
		header.filelen = LittleLong (header.filelen);
		if (header.filelen <= 0)
		{ /* bad entry -- don't trust the rest. */
			fclose (f);
			return NULL;
		}
		if (!q_strcasecmp (header.mapname, shortname))
			break;
		pos += header.filelen + VISPATCH_HEADER_LEN;
		fseek (f, pos, SEEK_SET);
	}
	if (r != VISPATCH_HEADER_LEN)
	{
		fclose (f);
		Con_DPrintf ("%s not found in %s\n", shortname, visfilename);
		return NULL;
	}

	return f;
}

static byte *Mod_LoadVisibilityExternal (FILE *f)
{
	int	  filelen;
	byte *visdata;

	filelen = 0;
	if (fread (&filelen, 1, 4, f) != 4)
		return NULL;
	filelen = LittleLong (filelen);
	if (filelen <= 0)
		return NULL;
	Con_DPrintf ("...%d bytes visibility data\n", filelen);
	visdata = (byte *)Mem_Alloc (filelen);
	if (fread (visdata, filelen, 1, f) != 1)
		return NULL;
	return visdata;
}

static void Mod_LoadLeafsExternal (qmodel_t *mod, FILE *f)
{
	int	  filelen;
	void *in;

	filelen = 0;
	if (fread (&filelen, 1, 4, f) != 4)
		Sys_Error ("Invalid leaf");
	filelen = LittleLong (filelen);
	if (filelen <= 0)
		return;
	Con_DPrintf ("...%d bytes leaf data\n", filelen);
	in = Mem_Alloc (filelen);
	if (fread (in, filelen, 1, f) != 1)
		return;
	Mod_ProcessLeafs_S (mod, (byte *)in, filelen);
}

/*
================
Mod_CalcSpecialsAndTextures
================
*/
static void Mod_CalcSpecialsAndTextures (qmodel_t *model)
{
	qboolean is_submodel = model->name[0] == '*';
	model->used_specials = 0;
	TEMP_ALLOC_ZEROED_COND (byte, used_tex, model->numtextures, is_submodel);

	for (int i = 0; i < model->nummodelsurfaces; i++)
	{
		msurface_t *psurf = &model->surfaces[model->firstmodelsurface] + i;
		model->used_specials |= (SURF_DRAWSKY | SURF_DRAWTURB | SURF_DRAWWATER | SURF_DRAWLAVA | SURF_DRAWSLIME | SURF_DRAWTELE) & psurf->flags;
		if (is_submodel && psurf->texinfo->tex_idx >= 0)
			used_tex[psurf->texinfo->tex_idx] = true;
	}

	if (!is_submodel)
		return;

	int total = 0, placed = 0;
	for (int i = 0; i < model->numtextures; i++)
		if (used_tex[i])
			++total;

	texture_t **orig_textures = model->textures;
	model->textures = (texture_t **)Mem_AllocNonZero (total * sizeof (*model->textures));
	model->numtextures = total;

	for (int i = 0; placed < total; i++)
		if (used_tex[i])
			model->textures[placed++] = orig_textures[i];

	TEMP_FREE (used_tex);
}

/*
=================
Mod_SetupSubmodels
set up the submodels (FIXME: this is confusing)
=================
*/
static void Mod_SetupSubmodels (qmodel_t *mod)
{
	texture_t **const orig_textures = mod->textures;
	int const		  orig_numtextures = mod->numtextures;

	int		  i, j;
	float	  radius;
	dmodel_t *bm;

	// johnfitz -- okay, so that i stop getting confused every time i look at this loop, here's how it works:
	// we're looping through the submodels starting at 0.  Submodel 0 is the main model, so we don't have to
	// worry about clobbering data the first time through, since it's the same data.  At the end of the loop,
	// we create a new copy of the data to use the next time through.
	for (i = 0; i < mod->numsubmodels; i++)
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];
		for (j = 1; j < MAX_MAP_HULLS; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			mod->hulls[j].lastclipnode = mod->numclipnodes - 1;
		}

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		// johnfitz -- calculate rotate bounds and yaw bounds
		radius = RadiusFromBounds (mod->mins, mod->maxs);
		mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = mod->ymaxs[0] = mod->ymaxs[1] = mod->ymaxs[2] = radius;
		mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = mod->ymins[0] = mod->ymins[1] = mod->ymins[2] = -radius;
		// johnfitz

		// johnfitz -- correct physics cullboxes so that outlying clip brushes on doors and stuff are handled right
		if (i > 0 || strcmp (mod->name, sv.modelname) != 0) // skip submodel 0 of sv.worldmodel, which is the actual world
		{
			// start with the hull0 bounds
			VectorCopy (mod->maxs, mod->clipmaxs);
			VectorCopy (mod->mins, mod->clipmins);

			// process hull1 (we don't need to process hull2 becuase there's
			// no such thing as a brush that appears in hull2 but not hull1)
			// Mod_BoundsFromClipNode (mod, 1, mod->hulls[1].firstclipnode); // (disabled for now becuase it fucks up on rotating models)
		}
		// johnfitz

		mod->numleafs = bm->visleafs;

		mod->textures = orig_textures;
		mod->numtextures = orig_numtextures;
		Mod_CalcSpecialsAndTextures (mod);

		if (i < mod->numsubmodels - 1)
		{ // duplicate the basic information
			char name[12];

			q_snprintf (name, sizeof (name), "*%i", i + 1);
			qmodel_t *submodel = Mod_FindName (name);
			*submodel = *mod;
			strcpy (submodel->name, name);
#ifdef PSET_SCRIPT
			// Need to NULL this otherwise we double delete in PScript_ClearSurfaceParticles
			submodel->skytrimem = NULL;
#endif
			mod = submodel;
		}
	}
}

/*
=================
Mod_LoadBrushModel
=================
*/
static void Mod_LoadBrushModel (qmodel_t *mod, const char *loadname, void *buffer)
{
	int		   i;
	int		   bsp2;
	dheader_t *header;

	mod->type = mod_brush;

	header = (dheader_t *)buffer;

	mod->bspversion = LittleLong (header->version);

	switch (mod->bspversion)
	{
	case BSPVERSION:
		bsp2 = false;
		break;
	case BSP2VERSION_2PSB:
		bsp2 = 1; // first iteration
		break;
	case BSP2VERSION_BSP2:
		bsp2 = 2; // sanitised revision
		break;
	case BSPVERSION_QUAKE64:
		bsp2 = false;
		break;
	default:
		Sys_Error ("Mod_LoadBrushModel: %s has unsupported version number (%i)", mod->name, mod->bspversion);
		break;
	}

	// swap all the lumps
	byte *mod_base = (byte *)header;

	for (i = 0; i < (int)sizeof (dheader_t) / 4; i++)
		((int *)header)[i] = LittleLong (((int *)header)[i]);

	// load into heap

	Mod_LoadVertexes (mod, mod_base, &header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (mod, mod_base, &header->lumps[LUMP_EDGES], bsp2);
	Mod_LoadSurfedges (mod, mod_base, &header->lumps[LUMP_SURFEDGES]);
	Mod_LoadTextures (mod, mod_base, &header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (mod, mod_base, &header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (mod, mod_base, &header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (mod, mod_base, &header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (mod, mod_base, &header->lumps[LUMP_FACES], bsp2);
	Mod_LoadMarksurfaces (mod, mod_base, &header->lumps[LUMP_MARKSURFACES], bsp2);

	if (mod->bspversion == BSPVERSION && external_vis.value && sv.modelname[0] && !q_strcasecmp (loadname, sv.name))
	{
		FILE *fvis;
		Con_DPrintf ("trying to open external vis file\n");
		fvis = Mod_FindVisibilityExternal (mod, loadname);
		if (fvis)
		{
			mod->leafs = NULL;
			mod->numleafs = 0;
			Con_DPrintf ("found valid external .vis file for map\n");
			mod->visdata = Mod_LoadVisibilityExternal (fvis);
			if (mod->visdata)
			{
				Mod_LoadLeafsExternal (mod, fvis);
			}
			fclose (fvis);
			if (mod->visdata && mod->leafs && mod->numleafs)
			{
				goto visdone;
			}
			Con_DPrintf ("External VIS data failed, using standard vis.\n");
		}
	}

	Mod_LoadVisibility (mod, mod_base, &header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (mod, mod_base, &header->lumps[LUMP_LEAFS], bsp2);
visdone:
	Mod_LoadNodes (mod, mod_base, &header->lumps[LUMP_NODES], bsp2);
	Mod_LoadClipnodes (mod, mod_base, &header->lumps[LUMP_CLIPNODES], bsp2);
	Mod_LoadEntities (mod, mod_base, &header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (mod, mod_base, &header->lumps[LUMP_MODELS]);

	Mod_MakeHull0 (mod);

	mod->numframes = 2; // regular and alternate animation

	Mod_CheckWaterVis (mod);
	Mod_SetupSubmodels (mod);
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

stvert_t	stverts[MAXALIASVERTS];
mtriangle_t triangles[MAXALIASTRIS];

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t *poseverts[MAXALIASFRAMES];
int			posenum;

/*
=================
Mod_LoadAliasFrame
=================
*/
void *Mod_LoadAliasFrame (void *pin, aliashdr_t *pheader, const int index)
{
	maliasframedesc_t *frame = &pheader->frames[index];
	trivertx_t		  *pinframe;
	int				   i;
	daliasframe_t	  *pdaliasframe;

	if (posenum >= MAXALIASFRAMES)
		Sys_Error ("posenum >= MAXALIASFRAMES");

	pdaliasframe = (daliasframe_t *)pin;

	strcpy (frame->name, pdaliasframe->name);
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i = 0; i < 3; i++)
	{
		// these are byte values, so we don't have to worry about
		// endianness
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];
	}

	pinframe = (trivertx_t *)(pdaliasframe + 1);

	poseverts[posenum] = pinframe;
	posenum++;

	pinframe += pheader->numverts;

	return (void *)pinframe;
}

/*
=================
Mod_LoadAliasGroup
=================
*/
void *Mod_LoadAliasGroup (void *pin, aliashdr_t *pheader, const int index)
{
	maliasframedesc_t *frame = &pheader->frames[index];
	daliasgroup_t	  *pingroup;
	int				   i, numframes;
	daliasinterval_t  *pin_intervals;
	void			  *ptemp;

	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i = 0; i < 3; i++)
	{
		// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];
	}

	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	for (i = 0; i < numframes; i++)
	{
		if (posenum >= MAXALIASFRAMES)
			Sys_Error ("posenum >= MAXALIASFRAMES");

		poseverts[posenum] = (trivertx_t *)((daliasframe_t *)ptemp + 1);
		posenum++;

		ptemp = (trivertx_t *)((daliasframe_t *)ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}

//=========================================================

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct
{
	short x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy)                           \
	do                                                        \
	{                                                         \
		if (pos[off] == fillcolor)                            \
		{                                                     \
			pos[off] = 255;                                   \
			fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
			inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;          \
		}                                                     \
		else if (pos[off] != 255)                             \
			fdc = pos[off];                                   \
	} while (0)

void Mod_FloodFillSkin (byte *skin, int skinwidth, int skinheight)
{
	byte		fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t fifo[FLOODFILL_FIFO_SIZE];
	int			inpt = 0, outpt = 0;
	int			filledcolor = -1;
	int			i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		// printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int	  x = fifo[outpt].x, y = fifo[outpt].y;
		int	  fdc = filledcolor;
		byte *pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)
			FLOODFILL_STEP (-1, -1, 0);
		if (x < skinwidth - 1)
			FLOODFILL_STEP (1, 1, 0);
		if (y > 0)
			FLOODFILL_STEP (-skinwidth, 0, -1);
		if (y < skinheight - 1)
			FLOODFILL_STEP (skinwidth, 0, 1);
		skin[x + skinwidth * y] = fdc;
	}
}

/*
===============
Mod_LoadSkinTask
===============
*/
typedef struct load_skin_task_args_s
{
	aliashdr_t *pheader;
	qmodel_t   *mod;
	byte	   *mod_base;
	byte	  **ppskintypes;
} load_skin_task_args_t;

static void Mod_LoadSkinTask (int i, load_skin_task_args_t *args)
{
	int			 j, k, size, groupskins;
	char		 name[MAX_QPATH];
	byte		*skin, *texels;
	byte		*pskintype = args->ppskintypes[i];
	byte		*pinskingroup;
	byte		*pinskinintervals;
	char		 fbr_mask_name[MAX_QPATH]; // johnfitz -- added for fullbright support
	src_offset_t offset;				   // johnfitz
	unsigned int texflags = TEXPREF_PAD;
	qmodel_t	*mod = args->mod;
	byte		*mod_base = args->mod_base;
	aliashdr_t	*pheader = args->pheader;

	size = pheader->skinwidth * pheader->skinheight;

	if (mod->flags & MF_HOLEY)
		texflags |= TEXPREF_ALPHA;

	if (ReadLongUnaligned (pskintype + offsetof (daliasskintype_t, type)) == ALIAS_SKIN_SINGLE)
	{
		skin = pskintype + sizeof (daliasskintype_t);
		Mod_FloodFillSkin (skin, pheader->skinwidth, pheader->skinheight);

		// save 8 bit texels for the player model to remap
		texels = (byte *)Mem_Alloc (size);
		pheader->texels[i] = texels;
		memcpy (texels, skin, size);

		// johnfitz -- rewritten
		q_snprintf (name, sizeof (name), "%s:frame%i", mod->name, i);
		offset = (src_offset_t)(skin) - (src_offset_t)mod_base;
		if (Mod_CheckFullbrights (skin, size))
		{
			pheader->gltextures[i][0] = TexMgr_LoadImage (
				mod, name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, mod->name, offset, texflags | TEXPREF_MIPMAP | TEXPREF_NOBRIGHT);
			q_snprintf (fbr_mask_name, sizeof (fbr_mask_name), "%s:frame%i_glow", mod->name, i);
			pheader->fbtextures[i][0] = TexMgr_LoadImage (
				mod, fbr_mask_name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, mod->name, offset,
				texflags | TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT);
		}
		else
		{
			pheader->gltextures[i][0] =
				TexMgr_LoadImage (mod, name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, mod->name, offset, texflags | TEXPREF_MIPMAP);
			pheader->fbtextures[i][0] = NULL;
		}

		pheader->gltextures[i][3] = pheader->gltextures[i][2] = pheader->gltextures[i][1] = pheader->gltextures[i][0];
		pheader->fbtextures[i][3] = pheader->fbtextures[i][2] = pheader->fbtextures[i][1] = pheader->fbtextures[i][0];
		// johnfitz
	}
	else
	{
		// animating skin group.  yuck.
		pinskingroup = pskintype + sizeof (daliasskintype_t);
		groupskins = ReadLongUnaligned (pinskingroup + offsetof (daliasskingroup_t, numskins));
		pinskinintervals = pinskingroup + sizeof (daliasskingroup_t);
		skin = pinskinintervals + (groupskins * sizeof (daliasskininterval_t));

		for (j = 0; j < groupskins; j++)
		{
			Mod_FloodFillSkin (skin, pheader->skinwidth, pheader->skinheight);
			if (j == 0)
			{
				texels = (byte *)Mem_Alloc (size);
				pheader->texels[i] = texels;
				memcpy (texels, skin, size);
			}

			// johnfitz -- rewritten
			q_snprintf (name, sizeof (name), "%s:frame%i_%i", mod->name, i, j);
			offset = (src_offset_t)(skin) - (src_offset_t)mod_base; // johnfitz
			if (Mod_CheckFullbrights (skin, size))
			{
				pheader->gltextures[i][j & 3] = TexMgr_LoadImage (
					mod, name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, mod->name, offset, texflags | TEXPREF_MIPMAP | TEXPREF_NOBRIGHT);
				q_snprintf (fbr_mask_name, sizeof (fbr_mask_name), "%s:frame%i_%i_glow", mod->name, i, j);
				pheader->fbtextures[i][j & 3] = TexMgr_LoadImage (
					mod, fbr_mask_name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, mod->name, offset,
					texflags | TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT);
			}
			else
			{
				pheader->gltextures[i][j & 3] =
					TexMgr_LoadImage (mod, name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, mod->name, offset, texflags | TEXPREF_MIPMAP);
				pheader->fbtextures[i][j & 3] = NULL;
			}
			// johnfitz

			skin += size;
		}
		k = j;
		for (/**/; j < 4; j++)
			pheader->gltextures[i][j & 3] = pheader->gltextures[i][j - k];
	}
}

/*
===============
Mod_LoadAllSkins
===============
*/
void *Mod_LoadAllSkins (aliashdr_t *pheader, qmodel_t *mod, byte *mod_base, int numskins, byte *pskintype)
{
	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of skins: %d", numskins);

	TEMP_ALLOC (byte *, ppskintypes, numskins);
	int size = pheader->skinwidth * pheader->skinheight;
	for (int i = 0; i < numskins; i++)
	{
		ppskintypes[i] = pskintype;
		if (ReadLongUnaligned (pskintype + offsetof (daliasskintype_t, type)) == ALIAS_SKIN_SINGLE)
		{
			pskintype += sizeof (daliasskintype_t) + size;
		}
		else
		{
			// animating skin group.  yuck.
			byte *pinskingroup = pskintype + sizeof (daliasskintype_t);
			int	  groupskins = ReadLongUnaligned (pinskingroup + offsetof (daliasskingroup_t, numskins));
			byte *pinskinintervals = pinskingroup + sizeof (daliasskingroup_t);
			byte *skin = pinskinintervals + (groupskins * sizeof (daliasskininterval_t));
			pskintype = skin + (groupskins * size);
		}
	}

	load_skin_task_args_t args = {
		.pheader = pheader,
		.mod = mod,
		.mod_base = mod_base,
		.ppskintypes = ppskintypes,
	};
	if (!Tasks_IsWorker () && (numskins > 1))
	{
		task_handle_t task = Task_AllocateAssignIndexedFuncAndSubmit ((task_indexed_func_t)Mod_LoadSkinTask, numskins, &args, sizeof (args));
		Task_Join (task, SDL_MUTEX_MAXWAIT);
	}
	else
	{
		for (int i = 0; i < numskins; i++)
		{
			Mod_LoadSkinTask (i, &args);
		}
	}

	TEMP_FREE (ppskintypes);
	return (void *)pskintype;
}

//=========================================================================

/*
=================
Mod_CalcAliasBounds -- johnfitz -- calculate bounds of alias model for nonrotated, yawrotated, and fullrotated cases
=================
*/
static void Mod_CalcAliasBounds (qmodel_t *mod, aliashdr_t *a, int numvertexes, byte *vertexes)
{
	int	   i, j, k;
	float  dist, yawradius, radius;
	vec3_t v;

	// clear out all data
	for (i = 0; i < 3; i++)
	{
		mod->mins[i] = mod->ymins[i] = mod->rmins[i] = FLT_MAX;
		mod->maxs[i] = mod->ymaxs[i] = mod->rmaxs[i] = -FLT_MAX;
		radius = yawradius = 0;
	}

	switch (a->poseverttype)
	{
	case PV_QUAKE1:
	{
		// process verts
		for (i = 0; i < a->numposes; i++)
			for (j = 0; j < a->numverts; j++)
			{
				for (k = 0; k < 3; k++)
					v[k] = poseverts[i][j].v[k] * a->scale[k] + a->scale_origin[k];

				for (k = 0; k < 3; k++)
				{
					mod->mins[k] = q_min (mod->mins[k], v[k]);
					mod->maxs[k] = q_max (mod->maxs[k], v[k]);
				}
				dist = v[0] * v[0] + v[1] * v[1];
				if (yawradius < dist)
					yawradius = dist;
				dist += v[2] * v[2];
				if (radius < dist)
					radius = dist;
			}
		break;
	}
	case PV_MD5:
	{
		// process verts
		md5vert_t *pv = (md5vert_t *)vertexes;
		for (j = 0; j < numvertexes; j++)
		{
			for (k = 0; k < 3; k++)
				v[k] = pv[j].xyz[k];

			for (k = 0; k < 3; k++)
			{
				mod->mins[k] = q_min (mod->mins[k], v[k]);
				mod->maxs[k] = q_max (mod->maxs[k], v[k]);
			}
			dist = v[0] * v[0] + v[1] * v[1];
			if (yawradius < dist)
				yawradius = dist;
			dist += v[2] * v[2];
			if (radius < dist)
				radius = dist;
		}
		break;
	}
	}

	// rbounds will be used when entity has nonzero pitch or roll
	radius = sqrtf (radius);
	mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = -radius;
	mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = radius;

	// ybounds will be used when entity has nonzero yaw
	yawradius = sqrtf (yawradius);
	mod->ymins[0] = mod->ymins[1] = -yawradius;
	mod->ymaxs[0] = mod->ymaxs[1] = yawradius;
	mod->ymins[2] = mod->mins[2];
	mod->ymaxs[2] = mod->maxs[2];
}

static qboolean nameInList (const char *list, const char *name)
{
	const char *s;
	char		tmp[MAX_QPATH];
	int			i;

	s = list;

	while (*s)
	{
		// make a copy until the next comma or end of string
		i = 0;
		while (*s && *s != ',')
		{
			if (i < MAX_QPATH - 1)
				tmp[i++] = *s;
			s++;
		}
		tmp[i] = '\0';
		// compare it to the model name
		if (!strcmp (name, tmp))
		{
			return true;
		}
		// search forwards to the next comma or end of string
		while (*s && *s == ',')
			s++;
	}
	return false;
}

/*
=================
Mod_SetExtraFlags -- johnfitz -- set up extra flags that aren't in the mdl
=================
*/
void Mod_SetExtraFlags (qmodel_t *mod)
{
	extern cvar_t r_nolerp_list;

	if (!mod)
		return;

	mod->flags &= (0xFF | MF_HOLEY); // only preserve first byte, plus MF_HOLEY

	if (mod->type == mod_alias)
	{
		// nolerp flag
		if (nameInList (r_nolerp_list.string, mod->name))
			mod->flags |= MOD_NOLERP;

		// fullbright hack (TODO: make this a cvar list)
		if (!strcmp (mod->name, "progs/flame2.mdl") || !strcmp (mod->name, "progs/flame.mdl") || !strcmp (mod->name, "progs/boss.mdl"))
			mod->flags |= MOD_FBRIGHTHACK;
	}

#ifdef PSET_SCRIPT
	PScript_UpdateModelEffects (mod);
#endif
}

/*
=================
Mod_LoadAliasModel
=================
*/
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer)
{
	int	  i, j;
	byte *pinstverts;
	byte *pintriangles;
	int	  version, numframes;
	int	  size;
	byte *pframetype;
	byte *pskintype;
	byte *mod_base = (byte *)buffer; // johnfitz

	version = ReadLongUnaligned (mod_base + offsetof (mdl_t, version));
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)", mod->name, version, ALIAS_VERSION);

	//
	// allocate space for a working header, plus all the data except the frames,
	// skin and group info
	//
	size = sizeof (aliashdr_t) + (ReadLongUnaligned (mod_base + offsetof (mdl_t, numframes)) - 1) * sizeof (maliasframedesc_t);
	aliashdr_t *pheader = (aliashdr_t *)Mem_Alloc (size);
	pheader->poseverttype = PV_QUAKE1;

	mod->flags = ReadLongUnaligned (mod_base + offsetof (mdl_t, flags));

	//
	// endian-adjust and copy the data, starting with the alias model header
	//
	pheader->boundingradius = ReadLongUnaligned (mod_base + offsetof (mdl_t, boundingradius));
	pheader->numskins = ReadLongUnaligned (mod_base + offsetof (mdl_t, numskins));
	pheader->skinwidth = ReadLongUnaligned (mod_base + offsetof (mdl_t, skinwidth));
	pheader->skinheight = ReadLongUnaligned (mod_base + offsetof (mdl_t, skinheight));

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		Con_DWarning ("model %s has a skin taller than %d", mod->name, MAX_LBM_HEIGHT);

	pheader->numverts = ReadLongUnaligned (mod_base + offsetof (mdl_t, numverts));

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices (%d; max = %d)", mod->name, pheader->numverts, MAXALIASVERTS);

	pheader->numtris = ReadLongUnaligned (mod_base + offsetof (mdl_t, numtris));

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	if (pheader->numtris > MAXALIASTRIS)
		Sys_Error ("model %s has too many triangles (%d; max = %d)", mod->name, pheader->numtris, MAXALIASTRIS);

	pheader->numframes = ReadLongUnaligned (mod_base + offsetof (mdl_t, numframes));
	numframes = pheader->numframes;
	if (numframes < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d", numframes);

	pheader->size = ReadFloatUnaligned (mod_base + offsetof (mdl_t, size)) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t)ReadLongUnaligned (mod_base + offsetof (mdl_t, synctype));
	mod->numframes = pheader->numframes;

	for (i = 0; i < 3; i++)
	{
		pheader->scale[i] = ReadFloatUnaligned (mod_base + offsetof (mdl_t, scale[i]));
		pheader->scale_origin[i] = ReadFloatUnaligned (mod_base + offsetof (mdl_t, scale_origin[i]));
		pheader->eyeposition[i] = ReadFloatUnaligned (mod_base + offsetof (mdl_t, eyeposition[i]));
	}

	//
	// load the skins
	//
	pskintype = mod_base + sizeof (mdl_t);
	pskintype = Mod_LoadAllSkins (pheader, mod, mod_base, pheader->numskins, pskintype);

	//
	// load base s and t vertices
	//
	pinstverts = pskintype;

	for (i = 0; i < pheader->numverts; i++)
	{
		stverts[i].onseam = ReadLongUnaligned (pinstverts + offsetof (stvert_t, onseam));
		stverts[i].s = ReadLongUnaligned (pinstverts + offsetof (stvert_t, s));
		stverts[i].t = ReadLongUnaligned (pinstverts + offsetof (stvert_t, t));
		pinstverts += sizeof (stvert_t);
	}

	//
	// load triangle lists
	//
	pintriangles = pinstverts;

	for (i = 0; i < pheader->numtris; i++)
	{
		triangles[i].facesfront = ReadLongUnaligned (pintriangles + offsetof (dtriangle_t, facesfront));

		for (j = 0; j < 3; j++)
		{
			triangles[i].vertindex[j] = ReadLongUnaligned (pintriangles + offsetof (dtriangle_t, vertindex[j]));
		}
		pintriangles += sizeof (dtriangle_t);
	}

	//
	// load the frames
	//
	posenum = 0;
	pframetype = pintriangles;

	for (i = 0; i < numframes; i++)
	{
		aliasframetype_t frametype;
		frametype = (aliasframetype_t)ReadLongUnaligned (pframetype + offsetof (daliasframetype_t, type));
		if (frametype == ALIAS_SINGLE)
			pframetype = Mod_LoadAliasFrame (pframetype + sizeof (daliasframetype_t), pheader, i);
		else
			pframetype = Mod_LoadAliasGroup (pframetype + sizeof (daliasframetype_t), pheader, i);
	}

	pheader->numposes = posenum;

	mod->type = mod_alias;

	Mod_SetExtraFlags (mod); // johnfitz

	Mod_CalcAliasBounds (mod, pheader, 0, NULL); // johnfitz

	//
	// build the draw lists
	//
	GL_MakeAliasModelDisplayLists (mod, pheader);

	//
	// move the complete, relocatable alias model to the cache
	//
	mod->extradata[PV_QUAKE1] = (byte *)pheader;
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
static void *Mod_LoadSpriteFrame (qmodel_t *mod, byte *mod_base, void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t *pinframe;
	mspriteframe_t *pspriteframe;
	int				width, height, size, origin[2];
	char			name[64];
	src_offset_t	offset; // johnfitz

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	pspriteframe = (mspriteframe_t *)Mem_Alloc (sizeof (mspriteframe_t));
	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	pspriteframe->smax = 1;
	pspriteframe->tmax = 1;

	q_snprintf (name, sizeof (name), "%s:frame%i", mod->name, framenum);
	offset = (src_offset_t)(pinframe + 1) - (src_offset_t)mod_base; // johnfitz
	pspriteframe->gltexture = TexMgr_LoadImage (
		mod, name, width, height, SRC_INDEXED, (byte *)(pinframe + 1), mod->name, offset,
		TEXPREF_PAD | TEXPREF_ALPHA | TEXPREF_NOPICMIP); // johnfitz -- TexMgr

	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}

/*
=================
Mod_LoadSpriteGroup
=================
*/
static void *Mod_LoadSpriteGroup (qmodel_t *mod, byte *mod_base, void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t	  *pingroup;
	mspritegroup_t	  *pspritegroup;
	int				   i, numframes;
	dspriteinterval_t *pin_intervals;
	float			  *poutintervals;
	void			  *ptemp;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	pspritegroup = (mspritegroup_t *)Mem_Alloc (sizeof (mspritegroup_t) + (numframes - 1) * sizeof (pspritegroup->frames[0]));

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	poutintervals = (float *)Mem_Alloc (numframes * sizeof (float));

	pspritegroup->intervals = poutintervals;

	for (i = 0; i < numframes; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i = 0; i < numframes; i++)
	{
		ptemp = Mod_LoadSpriteFrame (mod, mod_base, ptemp, &pspritegroup->frames[i], framenum * 100 + i);
	}

	return ptemp;
}

/*
=================
Mod_LoadSpriteModel
=================
*/
static void Mod_LoadSpriteModel (qmodel_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t		   *pin;
	msprite_t		   *psprite;
	int					numframes;
	int					size;
	dspriteframetype_t *pframetype;

	pin = (dsprite_t *)buffer;
	byte *mod_base = (byte *)buffer; // johnfitz

	version = LittleLong (pin->version);
	if (version != SPRITE_VERSION)
		Sys_Error (
			"%s has wrong version number "
			"(%i should be %i)",
			mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (pin->numframes);
	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d", numframes);

	size = sizeof (msprite_t) + (numframes - 1) * sizeof (psprite->frames);

	psprite = (msprite_t *)Mem_Alloc (size);

	mod->extradata[0] = (byte *)psprite;

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = (synctype_t)LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth / 2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth / 2;
	mod->mins[2] = -psprite->maxheight / 2;
	mod->maxs[2] = psprite->maxheight / 2;

	//
	// load the frames
	//
	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *)(pin + 1);

	for (i = 0; i < numframes; i++)
	{
		spriteframetype_t frametype;

		frametype = (spriteframetype_t)LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)Mod_LoadSpriteFrame (mod, mod_base, pframetype + 1, &psprite->frames[i].frameptr, i);
		}
		else
		{
			pframetype = (dspriteframetype_t *)Mod_LoadSpriteGroup (mod, mod_base, pframetype + 1, &psprite->frames[i].frameptr, i);
		}
	}

	mod->type = mod_sprite;
}

/*
=================================================================
MD5 Models, for compat with the rerelease and NOT doom3.
=================================================================
md5mesh:
MD5Version 10
commandline ""
numJoints N
numMeshes N
joints {
	"name" ParentIdx ( Pos_X Y Z ) ( Quat_X Y Z )
}
mesh {
	shader "name"	//file-relative path, with _%02d_%02d postfixed for skin/framegroup support. unlike doom3.
	numverts N
	vert # ( S T ) FirstWeight count
	numtris N
	tri # A B C
	numweights N
	weight # JointIdx Scale ( X Y Z )
}

md5anim:
MD5Version 10
commandline ""
numFrames N
numJoints N
frameRate FPS
numAnimatedComponents N	//joints*6ish
hierachy {
	"name" ParentIdx Flags DataStart
}
bounds {
	( X Y Z ) ( X Y Z )
}
baseframe {
	( pos_X Y Z ) ( quad_X Y Z )
}
frame # {
	RAW ...
}

We'll unpack the animation to separate framegroups (one-pose per, for consistency with most q1 models).
*/

/*
================
MD5_ParseCheck
================
*/
static qboolean MD5_ParseCheck (const char *s, const void **buffer)
{
	if (strcmp (com_token, s))
		return false;
	*buffer = COM_Parse (*buffer);
	return true;
}

/*
================
MD5_ParseUInt
================
*/
static size_t MD5_ParseUInt (const void **buffer)
{
	size_t i = SDL_strtoull (com_token, NULL, 0);
	*buffer = COM_Parse (*buffer);
	return i;
}

/*
================
MD5_ParseSInt
================
*/
static long MD5_ParseSInt (const void **buffer)
{
	long i = SDL_strtol (com_token, NULL, 0);
	*buffer = COM_Parse (*buffer);
	return i;
}

/*
================
MD5_ParseFloat
================
*/
static double MD5_ParseFloat (const void **buffer)
{
	double i = SDL_strtod (com_token, NULL);
	*buffer = COM_Parse (*buffer);
	return i;
}

#define MD5EXPECT(s)                                                           \
	do                                                                         \
	{                                                                          \
		if (strcmp (com_token, s))                                             \
			Sys_Error ("Mod_LoadMD5MeshModel(%s): Expected \"%s\"", fname, s); \
		buffer = COM_Parse (buffer);                                           \
	} while (0)
#define MD5UINT()	MD5_ParseUInt (&buffer)
#define MD5SINT()	MD5_ParseSInt (&buffer)
#define MD5FLOAT()	MD5_ParseFloat (&buffer)
#define MD5CHECK(s) MD5_ParseCheck (s, &buffer)

/*
================
md5vertinfo_s
================
*/
typedef struct md5vertinfo_s
{
	size_t		 firstweight;
	unsigned int count;
} md5vertinfo_t;

/*
================
md5weightinfo_s
================
*/
typedef struct md5weightinfo_s
{
	size_t joint_index;
	vec4_t pos;
} md5weightinfo_t;

/*
================
jointinfo_s
================
*/
typedef struct jointinfo_s
{
	ssize_t		parent; //-1 for a root joint
	char		name[32];
	jointpose_t inverse;
} jointinfo_t;

/*
================
Matrix3x4_RM_Transform4
================
*/
static void Matrix3x4_RM_Transform4 (const float *matrix, const float *vector, float *product)
{
	product[0] = matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2] + matrix[3] * vector[3];
	product[1] = matrix[4] * vector[0] + matrix[5] * vector[1] + matrix[6] * vector[2] + matrix[7] * vector[3];
	product[2] = matrix[8] * vector[0] + matrix[9] * vector[1] + matrix[10] * vector[2] + matrix[11] * vector[3];
}

/*
================
GenMatrixPosQuat4Scale
================
*/
static void GenMatrixPosQuat4Scale (const vec3_t pos, const vec4_t quat, const vec3_t scale, float result[12])
{
	const float x2 = quat[0] + quat[0];
	const float y2 = quat[1] + quat[1];
	const float z2 = quat[2] + quat[2];

	const float xx = quat[0] * x2;
	const float xy = quat[0] * y2;
	const float xz = quat[0] * z2;
	const float yy = quat[1] * y2;
	const float yz = quat[1] * z2;
	const float zz = quat[2] * z2;
	const float xw = quat[3] * x2;
	const float yw = quat[3] * y2;
	const float zw = quat[3] * z2;

	result[0 * 4 + 0] = scale[0] * (1.0f - (yy + zz));
	result[1 * 4 + 0] = scale[0] * (xy + zw);
	result[2 * 4 + 0] = scale[0] * (xz - yw);

	result[0 * 4 + 1] = scale[1] * (xy - zw);
	result[1 * 4 + 1] = scale[1] * (1.0f - (xx + zz));
	result[2 * 4 + 1] = scale[1] * (yz + xw);

	result[0 * 4 + 2] = scale[2] * (xz + yw);
	result[1 * 4 + 2] = scale[2] * (yz - xw);
	result[2 * 4 + 2] = scale[2] * (1.0f - (xx + yy));

	result[0 * 4 + 3] = pos[0];
	result[1 * 4 + 3] = pos[1];
	result[2 * 4 + 3] = pos[2];
}

/*
================
Matrix3x4_Invert_Simple
================
*/
static void Matrix3x4_Invert_Simple (const float *in1, float *out)
{
	// we only support uniform scaling, so assume the first row is enough
	// (note the lack of sqrt here, because we're trying to undo the scaling,
	// this means multiplying by the inverse scale twice - squaring it, which
	// makes the sqrt a waste of time)
	const double scale = 1.0 / ((double)in1[0] * (double)in1[0] + (double)in1[1] * (double)in1[1] + (double)in1[2] * (double)in1[2]);

	// invert the rotation by transposing and multiplying by the squared
	// reciprocal of the input matrix scale as described above
	double temp[12];
	temp[0] = in1[0] * scale;
	temp[1] = in1[4] * scale;
	temp[2] = in1[8] * scale;
	temp[4] = in1[1] * scale;
	temp[5] = in1[5] * scale;
	temp[6] = in1[9] * scale;
	temp[8] = in1[2] * scale;
	temp[9] = in1[6] * scale;
	temp[10] = in1[10] * scale;

	// invert the translate
	temp[3] = -(in1[3] * temp[0] + in1[7] * temp[1] + in1[11] * temp[2]);
	temp[7] = -(in1[3] * temp[4] + in1[7] * temp[5] + in1[11] * temp[6]);
	temp[11] = -(in1[3] * temp[8] + in1[7] * temp[9] + in1[11] * temp[10]);

	for (int i = 0; i < 12; ++i)
		out[i] = (float)temp[i];
}

/*
================
MD5_BakeInfluences
================
*/
static void MD5_BakeInfluences (
	const char *fname, jointpose_t *outposes, md5vert_t *vert, struct md5vertinfo_s *vinfo, struct md5weightinfo_s *weight, size_t numverts, size_t numweights)
{
	struct md5weightinfo_s *w;
	vec3_t					pos;
	float					scale;
	unsigned int			maxinfluences = 0;
	float					scaleimprecision = 1;

	for (size_t v = 0; v < numverts; v++, vert++, vinfo++)
	{
		float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		// st were already loaded
		// norm will need to be calculated after we have xyz info
		for (int i = 0; i < 3; ++i)
			vert->xyz[i] = 0;
		for (int i = 0; i < 4; ++i)
		{
			vert->joint_weights[i] = 0;
			vert->joint_indices[i] = 0;
		}

		if (vinfo->firstweight + vinfo->count > numweights)
			Sys_Error ("%s: weight index out of bounds", fname);
		if (maxinfluences < vinfo->count)
			maxinfluences = vinfo->count;
		w = weight + vinfo->firstweight;
		for (unsigned int i = 0; i < vinfo->count; i++, w++)
		{
			Matrix3x4_RM_Transform4 (outposes[w->joint_index].mat, w->pos, pos);
			VectorAdd (vert->xyz, pos, vert->xyz);

			if (i < NUM_JOINT_INFLUENCES)
			{
				weights[i] = w->pos[3];
				vert->joint_indices[i] = w->joint_index;
			}
			else
			{
				// obnoxious code to find the lowest of the current possible joint indexes.
				float  lowval = weights[0];
				size_t lowidx = 0;
				for (size_t k = 1; k < NUM_JOINT_INFLUENCES; ++k)
				{
					if (weights[k] < lowval)
					{
						lowval = weights[k];
						lowidx = k;
					}
				}
				if (weights[lowidx] < w->pos[3])
				{ // found a lower/unset weight, replace it.
					weights[lowidx] = w->pos[3];
					vert->joint_indices[lowidx] = w->joint_index;
				}
			}
		}

		// normalize in case we dropped some weights.
		scale = weights[0] + weights[1] + weights[2] + weights[3];
		if (scale > 0)
		{
			if (scaleimprecision < scale)
				scaleimprecision = scale;
			scale = 1 / scale;
			for (int k = 0; k < NUM_JOINT_INFLUENCES; ++k)
				weights[k] *= scale;
		}
		else // something bad...
		{
			weights[0] = 1;
			weights[1] = weights[2] = weights[3] = 0;
		}

		for (int j = 0; j < 4; ++j)
			vert->joint_weights[j] = (byte)(CLAMP (0.0f, weights[j], 1.0f) * 255.0f);
	}
	if (maxinfluences > NUM_JOINT_INFLUENCES)
		Con_DWarning ("%s uses up to %u influences per vertex (weakest: %g)\n", fname, maxinfluences, scaleimprecision);
}

/*
================
MD5_ComputeNormals
================
*/
static void MD5_ComputeNormals (md5vert_t *vert, size_t numverts, unsigned short *indexes, size_t numindexes)
{
	hash_map_t *pos_to_normal_map = HashMap_Create (vec3_t, vec3_t, &HashVec3, NULL);
	HashMap_Reserve (pos_to_normal_map, numverts);

	for (size_t v = 0; v < numverts; v++)
		vert[v].norm[0] = vert[v].norm[1] = vert[v].norm[2] = 0;
	for (size_t t = 0; t < numindexes; t += 3)
	{
		md5vert_t *verts[3] = {&vert[indexes[t + 0]], &vert[indexes[t + 1]], &vert[indexes[t + 2]]};

		vec3_t d1, d2;
		VectorSubtract (verts[2]->xyz, verts[0]->xyz, d1);
		VectorSubtract (verts[1]->xyz, verts[0]->xyz, d2);
		VectorNormalize (d1);
		VectorNormalize (d2);

		vec3_t norm;
		CrossProduct (d1, d2, norm);
		VectorNormalize (norm);

		const float angle = acos (DotProduct (d1, d2));
		VectorScale (norm, angle, norm);

		vec3_t *found_normal;
		for (int i = 0; i < 3; ++i)
		{
			if ((found_normal = HashMap_Lookup (vec3_t, pos_to_normal_map, &verts[i]->xyz)))
				VectorAdd (norm, *found_normal, *found_normal);
			else
				HashMap_Insert (pos_to_normal_map, &verts[i]->xyz, &norm);
		}
	}

	const uint32_t map_size = HashMap_Size (pos_to_normal_map);
	for (uint32_t i = 0; i < map_size; ++i)
	{
		vec3_t *norm = HashMap_GetValue (vec3_t, pos_to_normal_map, i);
		VectorNormalize (*norm);
	}

	for (size_t v = 0; v < numverts; v++)
	{
		vec3_t *norm = HashMap_Lookup (vec3_t, pos_to_normal_map, &vert[v].xyz);
		if (norm)
			VectorCopy (*norm, vert[v].norm);
	}

	HashMap_Destroy (pos_to_normal_map);
}

/*
================
MD5_HackyModelFlags
================
*/
static unsigned int MD5_HackyModelFlags (const char *name)
{
	unsigned int ret = 0;
	char		 oldmodel[MAX_QPATH];
	mdl_t		*f;
	COM_StripExtension (name, oldmodel, sizeof (oldmodel));
	COM_AddExtension (oldmodel, ".mdl", sizeof (oldmodel));

	f = (mdl_t *)COM_LoadFile (oldmodel, NULL);
	if (f)
	{
		if (com_filesize >= sizeof (*f) && LittleLong (f->ident) == IDPOLYHEADER && LittleLong (f->version) == ALIAS_VERSION)
			ret = f->flags;
		Mem_Free (f);
	}
	return ret;
}

/*
================
md5animctx_s
================
*/
typedef struct md5animctx_s
{
	void		*animfile;
	const void	*buffer;
	char		 fname[MAX_QPATH];
	size_t		 numposes;
	size_t		 numjoints;
	jointpose_t *posedata;
} md5animctx_t;

/*
================
MD5Anim_Begin
This is split into two because aliashdr_t has silly trailing framegroup info.
================
*/
static void MD5Anim_Begin (md5animctx_t *ctx, const char *fname)
{
	// Load an md5anim into it, if we can.
	COM_StripExtension (fname, ctx->fname, sizeof (ctx->fname));
	COM_AddExtension (ctx->fname, ".md5anim", sizeof (ctx->fname));
	fname = ctx->fname;
	ctx->animfile = COM_LoadFile (fname, NULL);
	ctx->numposes = 0;

	if (ctx->animfile)
	{
		const void *buffer = COM_Parse (ctx->animfile);
		MD5EXPECT ("MD5Version");
		MD5EXPECT ("10");
		if (MD5CHECK ("commandline"))
			buffer = COM_Parse (buffer);
		MD5EXPECT ("numFrames");
		ctx->numposes = MD5UINT ();
		MD5EXPECT ("numJoints");
		ctx->numjoints = MD5UINT ();
		MD5EXPECT ("frameRate"); // irrelevant here

		if (ctx->numposes <= 0)
			Sys_Error ("%s has no poses", fname);

		ctx->buffer = buffer;
	}
}

/*
================
MD5Anim_Load
================
*/
static void MD5Anim_Load (md5animctx_t *ctx, jointinfo_t *joints, size_t numjoints)
{
	const char *fname = ctx->fname;
	struct
	{
		unsigned int flags, offset;
	}			*ab;
	size_t		 rawcount;
	float		*raw, *r;
	jointpose_t *outposes;
	const void	*buffer = COM_Parse (ctx->buffer);
	size_t		 j;

	if (!buffer)
	{
		Mem_Free (ctx->animfile);
		return;
	}

	MD5EXPECT ("numAnimatedComponents");
	rawcount = MD5UINT ();

	if (ctx->numjoints != numjoints)
		Sys_Error ("%s has incorrect joint count", fname);

	raw = Mem_Alloc (sizeof (*raw) * (rawcount + 6));
	ab = Mem_Alloc (sizeof (*ab) * ctx->numjoints);

	ctx->posedata = outposes = Mem_Alloc (sizeof (*outposes) * ctx->numjoints * ctx->numposes);

	MD5EXPECT ("hierarchy");
	MD5EXPECT ("{");
	for (j = 0; j < ctx->numjoints; j++)
	{
		// validate stuff
		if (strcmp (joints[j].name, com_token))
			Sys_Error ("%s: joint was renamed", fname);
		buffer = COM_Parse (buffer);
		if (joints[j].parent != MD5SINT ())
			Sys_Error ("%s: joint has wrong parent", fname);
		// new info
		ab[j].flags = MD5UINT ();
		if (ab[j].flags & ~63)
			Sys_Error ("%s: joint has unsupported flags", fname);
		ab[j].offset = MD5UINT ();
		if (ab[j].offset > rawcount + 6)
			Sys_Error ("%s: joint has bad offset", fname);
	}
	MD5EXPECT ("}");
	MD5EXPECT ("bounds");
	MD5EXPECT ("{");
	while (MD5CHECK ("("))
	{
		(void)MD5FLOAT ();
		(void)MD5FLOAT ();
		(void)MD5FLOAT ();
		MD5EXPECT (")");

		MD5EXPECT ("(");
		(void)MD5FLOAT ();
		(void)MD5FLOAT ();
		(void)MD5FLOAT ();
		MD5EXPECT (")");
	}
	MD5EXPECT ("}");

	MD5EXPECT ("baseframe");
	MD5EXPECT ("{");
	while (MD5CHECK ("("))
	{
		(void)MD5FLOAT ();
		(void)MD5FLOAT ();
		(void)MD5FLOAT ();
		MD5EXPECT (")");

		MD5EXPECT ("(");
		(void)MD5FLOAT ();
		(void)MD5FLOAT ();
		(void)MD5FLOAT ();
		MD5EXPECT (")");
	}
	MD5EXPECT ("}");

	while (MD5CHECK ("frame"))
	{
		size_t idx = MD5UINT ();
		if (idx >= ctx->numposes)
			Sys_Error ("%s: invalid pose index", fname);
		MD5EXPECT ("{");
		for (j = 0; j < rawcount; j++)
			raw[j] = MD5FLOAT ();
		MD5EXPECT ("}");

		// okay, we have our raw info, unpack the actual joint info.
		for (j = 0; j < ctx->numjoints; j++)
		{
			vec3_t		  pos = {0, 0, 0};
			static vec3_t scale = {1, 1, 1};
			vec4_t		  quat = {0, 0, 0};
			r = raw + ab[j].offset;
			if (ab[j].flags & 1)
				pos[0] = *r++;
			if (ab[j].flags & 2)
				pos[1] = *r++;
			if (ab[j].flags & 4)
				pos[2] = *r++;

			if (ab[j].flags & 8)
				quat[0] = *r++;
			if (ab[j].flags & 16)
				quat[1] = *r++;
			if (ab[j].flags & 32)
				quat[2] = *r++;

			quat[3] = 1 - DotProduct (quat, quat);
			if (quat[3] < 0)
				quat[3] = 0; // we have no imagination.
			quat[3] = -sqrtf (quat[3]);

			GenMatrixPosQuat4Scale (pos, quat, scale, outposes[idx * ctx->numjoints + j].mat);
		}
	}
	Mem_Free (raw);
	Mem_Free (ab);
	Mem_Free (ctx->animfile);
}

/*
================
Mod_LoadMD5MeshModel
================
*/
static void Mod_LoadMD5MeshModel (qmodel_t *mod, const void *buffer)
{
	const char		*fname = mod->name;
	unsigned short	*poutindexes = NULL;
	md5vert_t		*poutvertexes = NULL;
	aliashdr_t		*outhdr, *surf;
	size_t			 hdrsize;
	size_t			 numjoints, j;
	size_t			 nummeshes, m;
	char			 texname[MAX_QPATH];
	md5vertinfo_t	*vinfo;
	md5weightinfo_t *weight;
	size_t			 numweights;

	md5animctx_t anim = {NULL};

	buffer = COM_Parse (buffer);

	MD5EXPECT ("MD5Version");
	MD5EXPECT ("10");
	if (MD5CHECK ("commandline"))
		buffer = COM_Parse (buffer);
	MD5EXPECT ("numJoints");
	numjoints = MD5UINT ();
	MD5EXPECT ("numMeshes");
	nummeshes = MD5UINT ();

	if (numjoints <= 0)
		Sys_Error ("%s has no joints", mod->name);
	if (nummeshes <= 0)
		Sys_Error ("%s has no meshes", mod->name);

	if (strcmp (com_token, "joints"))
		Sys_Error ("Mod_LoadMD5MeshModel(%s): Expected \"%s\"", fname, "joints");
	MD5Anim_Begin (&anim, fname);
	buffer = COM_Parse (buffer);

	hdrsize = sizeof (*outhdr) - sizeof (outhdr->frames);
	hdrsize += sizeof (outhdr->frames) * anim.numposes;
	outhdr = Mem_Alloc (hdrsize * numjoints);
	TEMP_ALLOC_ZEROED (jointinfo_t, joint_infos, numjoints);
	TEMP_ALLOC_ZEROED (jointpose_t, joint_poses, numjoints);

	MD5EXPECT ("{");
	for (j = 0; j < numjoints; j++)
	{
		vec3_t		  pos;
		static vec3_t scale = {1, 1, 1};
		vec4_t		  quat;
		q_strlcpy (joint_infos[j].name, com_token, sizeof (joint_infos[j].name));
		buffer = COM_Parse (buffer);
		joint_infos[j].parent = MD5SINT ();
		if (joint_infos[j].parent < -1 && joint_infos[j].parent >= (ssize_t)numjoints)
			Sys_Error ("joint index out of bounds");
		MD5EXPECT ("(");
		pos[0] = MD5FLOAT ();
		pos[1] = MD5FLOAT ();
		pos[2] = MD5FLOAT ();
		MD5EXPECT (")");
		MD5EXPECT ("(");
		quat[0] = MD5FLOAT ();
		quat[1] = MD5FLOAT ();
		quat[2] = MD5FLOAT ();
		quat[3] = 1 - DotProduct (quat, quat);
		if (quat[3] < 0)
			quat[3] = 0; // we have no imagination.
		quat[3] = -sqrtf (quat[3]);
		MD5EXPECT (")");

		GenMatrixPosQuat4Scale (pos, quat, scale, joint_poses[j].mat);
		Matrix3x4_Invert_Simple (joint_poses[j].mat, joint_infos[j].inverse.mat); // absolute, so we can just invert now.
	}

	if (strcmp (com_token, "}"))
		Sys_Error ("Mod_LoadMD5MeshModel(%s): Expected \"%s\"", fname, "}");
	MD5Anim_Load (&anim, joint_infos, numjoints);
	buffer = COM_Parse (buffer);

	int index_offset = 0;
	int vertex_offset = 0;
	for (m = 0; m < nummeshes; m++)
	{
		MD5EXPECT ("mesh");
		MD5EXPECT ("{");

		surf = (aliashdr_t *)((byte *)outhdr + m * hdrsize);
		if (m + 1 < nummeshes)
			surf->nextsurface = (aliashdr_t *)((byte *)outhdr + (m + 1) * hdrsize);
		else
			surf->nextsurface = NULL;

		surf->poseverttype = PV_MD5;
		for (j = 0; j < 3; j++)
		{
			surf->scale_origin[j] = 0;
			surf->scale[j] = 1.0;
		}

		surf->numjoints = numjoints;

		if (anim.numposes)
		{
			for (j = 0; j < anim.numposes; j++)
			{
				surf->frames[j].firstpose = j;
				surf->frames[j].numposes = 1;
				surf->frames[j].interval = 0.1;
			}
			surf->numframes = j;
		}

		MD5EXPECT ("shader");
		// MD5 violation: the skin is a single material. adding prefixes/postfixes here is the wrong thing to do.
		// but we do so anyway, because rerelease compat.
		for (surf->numskins = 0; surf->numskins < MAX_SKINS; surf->numskins++)
		{
			unsigned int fwidth, fheight, f;
			void		*data;
			for (f = 0; f < countof (surf->gltextures[0]); f++)
			{
				q_snprintf (texname, sizeof (texname), "progs/%s_%02u_%02u", com_token, surf->numskins, f);

				enum srcformat fmt = SRC_RGBA;
				data = Image_LoadImage (texname, (int *)&fwidth, (int *)&fheight, &fmt);
				if (data) // load external image
				{
					surf->gltextures[surf->numskins][f] =
						TexMgr_LoadImage (mod, texname, fwidth, fheight, fmt, data, texname, 0, TEXPREF_ALPHA | TEXPREF_NOBRIGHT | TEXPREF_MIPMAP);
					surf->fbtextures[surf->numskins][f] = NULL;
					if (fmt == SRC_INDEXED)
					{
						if (f == 0)
						{
							size_t size = fwidth * fheight;
							byte  *texels = (byte *)Mem_Alloc (size);
							surf->texels[surf->numskins] = texels;
							memcpy (texels, data, size);
						}
						// 8bit base texture. use it for fullbrights.
						for (j = 0; j < fwidth * fheight; j++)
						{
							if (((byte *)data)[j] > 223)
							{
								surf->fbtextures[surf->numskins][f] = TexMgr_LoadImage (
									mod, va ("%s_luma", texname), fwidth, fheight, SRC_INDEXED, data, texname, 0,
									TEXPREF_ALPHA | TEXPREF_FULLBRIGHT | TEXPREF_MIPMAP);
								break;
							}
						}
					}
					else
					{
						// we found a 32bit base texture.
						if (!surf->fbtextures[surf->numskins][f])
						{
							q_snprintf (texname, sizeof (texname), "progs/%s_%02u_%02u_glow", com_token, surf->numskins, f);
							surf->fbtextures[surf->numskins][f] =
								TexMgr_LoadImage (mod, texname, surf->skinwidth, surf->skinheight, SRC_RGBA, NULL, texname, 0, TEXPREF_MIPMAP);
						}
						if (!surf->fbtextures[surf->numskins][f])
						{
							q_snprintf (texname, sizeof (texname), "progs/%s_%02u_%02u_luma", com_token, surf->numskins, f);
							surf->fbtextures[surf->numskins][f] =
								TexMgr_LoadImage (mod, texname, surf->skinwidth, surf->skinheight, SRC_RGBA, NULL, texname, 0, TEXPREF_MIPMAP);
						}
					}

					Mem_Free (data);
				}
				else
					break;
			}
			if (f == 0)
				break; // no images loaded...

			// this stuff is hideous.
			if (f < 2)
			{
				surf->gltextures[surf->numskins][1] = surf->gltextures[surf->numskins][0];
				surf->fbtextures[surf->numskins][1] = surf->fbtextures[surf->numskins][0];
			}
			if (f == 3)
				Con_Warning ("progs/%s_%02u_##: 3 skinframes found...\n", com_token, surf->numskins);
			if (f < 4)
			{
				surf->gltextures[surf->numskins][3] = surf->gltextures[surf->numskins][1];
				surf->gltextures[surf->numskins][2] = surf->gltextures[surf->numskins][0];

				surf->fbtextures[surf->numskins][3] = surf->fbtextures[surf->numskins][1];
				surf->fbtextures[surf->numskins][2] = surf->fbtextures[surf->numskins][0];
			}
		}

		surf->skinwidth = surf->gltextures[0][0] ? surf->gltextures[0][0]->width : 1;
		surf->skinheight = surf->gltextures[0][0] ? surf->gltextures[0][0]->height : 1;
		surf->numposes = 1;

		buffer = COM_Parse (buffer);
		MD5EXPECT ("numverts");
		surf->numverts_vbo = surf->numverts = MD5UINT ();

		vinfo = Mem_Alloc (sizeof (*vinfo) * surf->numverts);
		poutvertexes = Mem_Realloc (poutvertexes, sizeof (*poutvertexes) * (vertex_offset + surf->numverts));
		while (MD5CHECK ("vert"))
		{
			size_t idx = MD5UINT ();
			if (idx >= (size_t)surf->numverts)
				Sys_Error ("vertex index out of bounds");
			MD5EXPECT ("(");
			poutvertexes[vertex_offset + idx].st[0] = MD5FLOAT ();
			poutvertexes[vertex_offset + idx].st[1] = MD5FLOAT ();
			MD5EXPECT (")");
			vinfo[idx].firstweight = MD5UINT ();
			vinfo[idx].count = MD5UINT ();
		}
		vertex_offset += surf->numverts;

		MD5EXPECT ("numtris");
		surf->numtris = MD5UINT ();
		surf->numindexes = surf->numtris * 3;
		poutindexes = Mem_Realloc (poutindexes, sizeof (*poutindexes) * (index_offset + surf->numindexes));
		while (MD5CHECK ("tri"))
		{
			size_t idx = MD5UINT ();
			if (idx >= (size_t)surf->numtris)
				Sys_Error ("triangle index out of bounds");
			idx *= 3;
			for (j = 0; j < 3; j++)
			{
				size_t t = MD5UINT ();
				if (t > (size_t)surf->numverts)
					Sys_Error ("vertex index out of bounds");
				poutindexes[index_offset + idx + j] = t;
			}
		}
		index_offset += surf->numindexes;

		// md5 is a gpu-unfriendly interchange format. :(
		MD5EXPECT ("numweights");
		numweights = MD5UINT ();
		weight = Mem_Alloc (sizeof (*weight) * numweights);
		while (MD5CHECK ("weight"))
		{
			size_t idx = MD5UINT ();
			if (idx >= numweights)
				Sys_Error ("weight index out of bounds");

			weight[idx].joint_index = MD5UINT ();
			if (weight[idx].joint_index >= numjoints)
				Sys_Error ("joint index out of bounds");
			weight[idx].pos[3] = MD5FLOAT ();
			MD5EXPECT ("(");
			weight[idx].pos[0] = MD5FLOAT () * weight[idx].pos[3];
			weight[idx].pos[1] = MD5FLOAT () * weight[idx].pos[3];
			weight[idx].pos[2] = MD5FLOAT () * weight[idx].pos[3];
			MD5EXPECT (")");
		}
		// so make it gpu-friendly.
		MD5_BakeInfluences (fname, joint_poses, poutvertexes, vinfo, weight, surf->numverts, numweights);
		// and now make up the normals that the format lacks. we'll still probably have issues from seams, but then so did qme, so at least its faithful... :P
		MD5_ComputeNormals (poutvertexes, surf->numverts, poutindexes, surf->numindexes);

		Mem_Free (weight);
		Mem_Free (vinfo);

		MD5EXPECT ("}");
	}

	TEMP_ALLOC_ZEROED (jointpose_t, inverted_joints, anim.numjoints * anim.numposes);
	TEMP_ALLOC_ZEROED (jointpose_t, concat_joints, anim.numjoints);
	for (size_t pose_index = 0; pose_index < anim.numposes; ++pose_index)
	{
		const jointpose_t *in_pose = anim.posedata + (pose_index * anim.numjoints);
		const jointpose_t *out_pose = inverted_joints + (pose_index * anim.numjoints);
		for (size_t joint_index = 0; joint_index < anim.numjoints; ++joint_index)
		{
			// concat it onto the parent (relative->abs)
			if (joint_infos[joint_index].parent < 0)
				memcpy (concat_joints[joint_index].mat, in_pose[joint_index].mat, sizeof (jointpose_t));
			else
				R_ConcatTransforms (
					(void *)concat_joints[joint_infos[joint_index].parent].mat, (void *)in_pose[joint_index].mat, (void *)concat_joints[joint_index].mat);
			// and finally invert it
			R_ConcatTransforms ((void *)concat_joints[joint_index].mat, (void *)joint_infos[joint_index].inverse.mat, (void *)out_pose[joint_index].mat);
		}
	}
	Mem_Free (anim.posedata);

	GLMesh_UploadBuffers (mod, outhdr, poutindexes, (byte *)poutvertexes, NULL, inverted_joints);
	TEMP_FREE (concat_joints);
	TEMP_FREE (inverted_joints);

	// the md5 format does not have its own modelflags, yet we still need to know about trails and rotating etc
	mod->flags = MD5_HackyModelFlags (mod->name);

	mod->synctype = ST_FRAMETIME; // keep MD5 animations synced to when .frame is changed. framegroups are otherwise not very useful.
	mod->type = mod_alias;
	mod->extradata[PV_MD5] = (byte *)outhdr;

	Mod_CalcAliasBounds (mod, outhdr, vertex_offset, (byte *)poutvertexes); // johnfitz

	TEMP_FREE (joint_poses);
	TEMP_FREE (joint_infos)
	Mem_Free (poutvertexes);
	Mem_Free (poutindexes);
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print (void)
{
	int		  i;
	qmodel_t *mod;

	Con_SafePrintf ("Cached models:\n"); // johnfitz -- safeprint instead of print
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		Con_SafePrintf ("%8p %8p: %s\n", mod->extradata[0], mod->extradata[1], mod->name); // johnfitz -- safeprint instead of print
	}
	Con_Printf ("%i models\n", mod_numknown); // johnfitz -- print the total too
}
