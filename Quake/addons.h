/* Mod downloads. Menu presentation follows Ironwail (GPL-2.0-or-later). */
#ifndef QUAKE_ADDONS_H
#define QUAKE_ADDONS_H

#include "json.h"

#define ADDON_SOURCE_COUNT 2

typedef struct
{
	char	   *url;
	const char *sha256;
} addon_part_t;

typedef struct addon_s
{
	int			  source;
	char		  dir[MAX_QPATH];
	char		  name[128];
	char		  author[256];
	char		  date[32];
	char		  size[32];
	char		  version[64];
	char		  revision[17];
	addon_part_t *parts;
	const char	 *sha256;
	uint64_t	  bytes;
	qboolean	  zip;
} addon_t;

void				   Addons_Init (void);
void				   Addons_Shutdown (void);
void				   Addons_Poll (void);
const char			  *Addons_SourceName (int source);
const addon_t		  *Addons_List (int source, int *count);
const char			  *Addons_Message (void);
const char			  *Addons_Installing (void);
int					   Addons_Progress (void);
const filelist_item_t *Addons_FindInstalled (const addon_t *addon);
int					   Addons_InstalledInfo (const char *dir, char *version, size_t size, char revision[17]);
qboolean			   Addons_UpdateAvailable (const addon_t *addon);
qboolean			   Addons_Install (const addon_t *addon);
void				   Addons_Cancel (void);
unsigned int		   Addons_Revision (void);

#endif
