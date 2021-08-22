#include "quakedef.h"


qboolean nightdiveEnabled;
cvar_t campaign = {"campaign","undefined",CVAR_NONE};

void ND_Init()
{
    nightdiveEnabled = false;

    // Detect 2021 RERELEASE
    // CHECK SOMETHING IN PAK FILE... LOC FILE MAYBE?
    //

    extern cvar_t campaign;
	Cvar_RegisterVariable (&campaign);
}

qboolean ND_ParseServerMessage(int cmd)
{
    if (nightdiveEnabled)
    {
        switch (cmd)
        {
            default:
                break;
            case svc_achievement:
                // DO SOMETHING???
                return true;
        }
    }
    return false;
}

void SV_Physics_Toss (edict_t *ent);

void SV_Physics_Nightdive(edict_t *ent)
{
    SV_Physics_Toss(ent); // Just toss for now
}
