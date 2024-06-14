/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// gl_fog.c -- global and volumetric fog

#include "quakedef.h"

//==============================================================================
//
//  GLOBAL FOG
//
//==============================================================================

#define DEFAULT_DENSITY 0.0
#define DEFAULT_GRAY	0.3

float fog_density;
float fog_red;
float fog_green;
float fog_blue;

float old_density;
float old_red;
float old_green;
float old_blue;

float fade_time; // duration of fade
float fade_done; // time when fade will be done

/*
=============
Fog_Update

update internal variables
=============
*/
void Fog_Update (float density, float red, float green, float blue, float time)
{
	// save previous settings for fade
	if (time > 0)
	{
		// check for a fade in progress
		if (fade_done > cl.time && fade_time != 0.0f)
		{
			float f;

			f = (fade_done - cl.time) / fade_time;
			old_density = f * old_density + (1.0 - f) * fog_density;
			old_red = f * old_red + (1.0 - f) * fog_red;
			old_green = f * old_green + (1.0 - f) * fog_green;
			old_blue = f * old_blue + (1.0 - f) * fog_blue;
		}
		else
		{
			old_density = fog_density;
			old_red = fog_red;
			old_green = fog_green;
			old_blue = fog_blue;
		}
	}

	fog_density = density;
	fog_red = red;
	fog_green = green;
	fog_blue = blue;
	fade_time = time;
	fade_done = cl.time + time;
}

/*
=============
Fog_ParseServerMessage

handle an SVC_FOG message from server
=============
*/
void Fog_ParseServerMessage (void)
{
	float density, red, green, blue, time;

	density = MSG_ReadByte () / 255.0;
	red = MSG_ReadByte () / 255.0;
	green = MSG_ReadByte () / 255.0;
	blue = MSG_ReadByte () / 255.0;
	time = q_max (0.0, MSG_ReadShort () / 100.0);

	Fog_Update (density, red, green, blue, time);
}

/*
=============
Fog_FogCommand_f

handle the 'fog' console command
=============
*/
void Fog_FogCommand_f (void)
{
	switch (Cmd_Argc ())
	{
	default:
	case 1:
		Con_Printf ("usage:\n");
		Con_Printf ("   fog <density>\n");
		Con_Printf ("   fog <red> <green> <blue>\n");
		Con_Printf ("   fog <density> <red> <green> <blue>\n");
		Con_Printf ("current values:\n");
		Con_Printf ("   \"density\" is \"%f\"\n", fog_density);
		Con_Printf ("   \"red\" is \"%f\"\n", fog_red);
		Con_Printf ("   \"green\" is \"%f\"\n", fog_green);
		Con_Printf ("   \"blue\" is \"%f\"\n", fog_blue);
		break;
	case 2:
		Fog_Update (q_max (0.0, atof (Cmd_Argv (1))), fog_red, fog_green, fog_blue, 0.0);
		break;
	case 3: // TEST
		Fog_Update (q_max (0.0, atof (Cmd_Argv (1))), fog_red, fog_green, fog_blue, atof (Cmd_Argv (2)));
		break;
	case 4:
		Fog_Update (fog_density, CLAMP (0.0, atof (Cmd_Argv (1)), 1.0), CLAMP (0.0, atof (Cmd_Argv (2)), 1.0), CLAMP (0.0, atof (Cmd_Argv (3)), 1.0), 0.0);
		break;
	case 5:
		Fog_Update (
			q_max (0.0, atof (Cmd_Argv (1))), CLAMP (0.0, atof (Cmd_Argv (2)), 1.0), CLAMP (0.0, atof (Cmd_Argv (3)), 1.0),
			CLAMP (0.0, atof (Cmd_Argv (4)), 1.0), 0.0);
		break;
	case 6: // TEST
		Fog_Update (
			q_max (0.0, atof (Cmd_Argv (1))), CLAMP (0.0, atof (Cmd_Argv (2)), 1.0), CLAMP (0.0, atof (Cmd_Argv (3)), 1.0),
			CLAMP (0.0, atof (Cmd_Argv (4)), 1.0), atof (Cmd_Argv (5)));
		break;
	}
}

/*
=============
Fog_ParseWorldspawn

called at map load
=============
*/
void Fog_ParseWorldspawn (void)
{
	char		key[128], value[4096];
	const char *data;

	// initially no fog
	fog_density = DEFAULT_DENSITY;
	fog_red = DEFAULT_GRAY;
	fog_green = DEFAULT_GRAY;
	fog_blue = DEFAULT_GRAY;

	old_density = DEFAULT_DENSITY;
	old_red = DEFAULT_GRAY;
	old_green = DEFAULT_GRAY;
	old_blue = DEFAULT_GRAY;

	fade_time = 0.0;
	fade_done = 0.0;

	data = COM_Parse (cl.worldmodel->entities);
	if (!data)
		return; // error
	if (com_token[0] != '{')
		return; // error
	while (1)
	{
		data = COM_Parse (data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			q_strlcpy (key, com_token + 1, sizeof (key));
		else
			q_strlcpy (key, com_token, sizeof (key));
		while (key[0] && key[strlen (key) - 1] == ' ') // remove trailing spaces
			key[strlen (key) - 1] = 0;
		data = COM_ParseEx (data, CPE_ALLOWTRUNC);
		if (!data)
			return; // error
		q_strlcpy (value, com_token, sizeof (value));

		if (!strcmp ("fog", key))
		{
			sscanf (value, "%f %f %f %f", &fog_density, &fog_red, &fog_green, &fog_blue);
		}
	}
}

/*
=============
Fog_GetColor

calculates fog color for this frame, taking into account fade times
=============
*/
void Fog_GetColor (float *c)
{
	float f;
	int	  i;

	if (fade_done > cl.time && fade_time != 0.0f)
	{
		f = (fade_done - cl.time) / fade_time;
		c[0] = f * old_red + (1.0 - f) * fog_red;
		c[1] = f * old_green + (1.0 - f) * fog_green;
		c[2] = f * old_blue + (1.0 - f) * fog_blue;
		c[3] = 1.0;
	}
	else
	{
		c[0] = fog_red;
		c[1] = fog_green;
		c[2] = fog_blue;
		c[3] = 1.0;
	}

	// find closest 24-bit RGB value, so solid-colored sky can match the fog perfectly
	for (i = 0; i < 3; i++)
		c[i] = (float)(Q_rint (c[i] * 255)) / 255.0f;
}

/*
=============
Fog_GetDensity

returns current density of fog
=============
*/
float Fog_GetDensity (void)
{
	float f;

	if (fade_done > cl.time && fade_time != 0.0f)
	{
		f = (fade_done - cl.time) / fade_time;
		return f * old_density + (1.0 - f) * fog_density;
	}
	else
		return fog_density;
}

/*
=============
Fog_ResetFade

called when client time may jump
=============
*/
void Fog_ResetFade (void)
{
	fade_done = 0.0;
}

/*
=============
Fog_GetFogCommand

so fog is preserved when starting a demo recording or in savegames
=============
*/
const char *Fog_GetFogCommand (qboolean always)
{
	if (fade_done || always)
		return va ("\nfog %g %g %g %g\n", fog_density, fog_red, fog_green, fog_blue);
	return NULL;
}

/*
=============
Fog_SetupFrame

called at the beginning of each frame
=============
*/
void Fog_SetupFrame (cb_context_t *cbx)
{
	float fog_color[4];
	Fog_GetColor (fog_color);
	float fog_values[4] = {CLAMP (0.0f, fog_color[0], 1.0f), CLAMP (0.0f, fog_color[1], 1.0f), CLAMP (0.0f, fog_color[2], 1.0f), Fog_GetDensity () / 64.0f};
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipelines[0]);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 16 * sizeof (float), 4 * sizeof (float), fog_values);
}

/*
=============
Fog_EnableGFog

called before drawing stuff that should be fogged
=============
*/
void Fog_EnableGFog (cb_context_t *cbx)
{
	float fog_color[4];
	Fog_GetColor (fog_color);
	float fog_values[4] = {CLAMP (0.0f, fog_color[0], 1.0f), CLAMP (0.0f, fog_color[1], 1.0f), CLAMP (0.0f, fog_color[2], 1.0f), Fog_GetDensity () / 64.0f};
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 16 * sizeof (float), 4 * sizeof (float), fog_values);
}

/*
=============
Fog_DisableGFog

called after drawing stuff that should be fogged
=============
*/
void Fog_DisableGFog (cb_context_t *cbx)
{
	float fog_values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	assert (cbx->current_pipeline.layout.handle == vulkan_globals.basic_pipeline_layout.handle);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 16 * sizeof (float), 4 * sizeof (float), fog_values);
}

//==============================================================================
//
//  VOLUMETRIC FOG
//
//==============================================================================

cvar_t r_vfog = {"r_vfog", "1", CVAR_NONE};

void Fog_DrawVFog (void) {}
void Fog_MarkModels (void) {}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
=============
Fog_NewMap

called whenever a map is loaded
=============
*/
void Fog_NewMap (void)
{
	Fog_ParseWorldspawn (); // for global fog
	Fog_MarkModels ();		// for volumetric fog
}

/*
=============
Fog_Init

called when quake initializes
=============
*/
void Fog_Init (void)
{
	Cmd_AddCommand ("fog", Fog_FogCommand_f);

	// Cvar_RegisterVariable (&r_vfog);

	// set up global fog
	fog_density = DEFAULT_DENSITY;
	fog_red = DEFAULT_GRAY;
	fog_green = DEFAULT_GRAY;
	fog_blue = DEFAULT_GRAY;
}
