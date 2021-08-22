#define MOVETYPE_NIGHTDIVE 11
#define svc_achievement 52

extern qboolean nightdiveEnabled;

void ND_Init (void);
qboolean ND_ParseServerMessage (int cmd);
void SV_Physics_Nightdive(edict_t *ent);
