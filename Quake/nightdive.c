#include "quakedef.h"

cvar_t campaign = {"campaign","undefined",CVAR_NONE};

void ND_Init()
{
    extern cvar_t campaign;
	Cvar_RegisterVariable (&campaign);
}

qboolean ND_ParseServerMessage(int cmd)
{
    if (cmd == 52)
    {
        Con_Printf ("Nightdive server command: %i\n", cmd);
        return true;
    }
    
    return false;
}

void SV_Physics_Nightdive(edict_t *ent)
{
    //Con_Printf ("Nightdive phys %s\n", PR_GetString(ent->v.classname));
}
