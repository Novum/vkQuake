#define MOVETYPE_NIGHTDIVE 11

void ND_Init (void);
qboolean ND_ParseServerMessage (int cmd);
void SV_Physics_Nightdive(edict_t *ent);
