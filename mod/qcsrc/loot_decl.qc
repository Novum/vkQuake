/*
==============================================================================

LOOT DECLARATIONS

Included right after defs.qc. QuakeC must see a name before it is used,
and the stock files (combat.qc, items.qc, weapons.qc, client.qc) call
into the loot code, which lives at the end in loot_game.qc. So this file
declares the shared fields, globals and functions up front.

==============================================================================
*/

// ---------------------------------------------------------------------------
// Engine extension builtins (vkQuake supports these, stock Quake does not)
// ---------------------------------------------------------------------------

string(string s1, ...) strcat = #115; // FRIK_FILE: join strings

// ---------------------------------------------------------------------------
// Extra spawn parms
//
// Quake carries the player between maps in parm1..parm16. vkQuake also
// carries parm17..parm64 if the progs declare them by name. We use them
// for the gun slots.
//
// parm17      magic number: proves the slots below are real data
// parm18..21  slot 1: gun_a, gun_b, gun_s, ammo in magazine
// parm22..25  slot 2
// parm26..29  slot 3
// parm30..33  slot 4
// parm34      selected slot
// ---------------------------------------------------------------------------

float parm17, parm18, parm19, parm20, parm21, parm22, parm23, parm24, parm25;
float parm26, parm27, parm28, parm29, parm30, parm31, parm32, parm33, parm34;

float LOOT_PARM_MAGIC = 24601;

// ---------------------------------------------------------------------------
// Player fields
// ---------------------------------------------------------------------------

// four equipped gun slots. a == 0 means the slot is empty.
.float slot1_a, slot1_b, slot1_s, slot1_m;
.float slot2_a, slot2_b, slot2_s, slot2_m;
.float slot3_a, slot3_b, slot3_s, slot3_m;
.float slot4_a, slot4_b, slot4_s, slot4_m;

.float loot_slot;        // 0 = axe, 1..4 = gun slot
.float loot_reload_end;  // time the reload finishes, 0 = not reloading
.float loot_burst_left;  // shots still to fire in this burst
.float loot_burst_next;  // time of the next burst shot

.entity loot_near;       // gun on the floor we are standing on
.float loot_near_time;
.entity loot_card_gun;   // gun whose card we showed last
.float loot_card_time;

// ---------------------------------------------------------------------------
// Fields on a gun lying in the world (classname "loot_gun")
// ---------------------------------------------------------------------------

.float gun_a, gun_b, gun_s;
.float gun_m;            // ammo in its magazine, -1 = full
.float loot_pickup_time; // can't be picked up before this (just dropped)
.float loot_splash;      // on projectiles: explosion fraction

// ---------------------------------------------------------------------------
// Functions in loot_game.qc used by stock files
// ---------------------------------------------------------------------------

void() Loot_Precache;
void() Loot_RefreshWeapon;
void() Loot_Attack;
void() Loot_WeaponFrame;
void() Loot_AmmoThink;
void(float n) Loot_SelectSlot;
void(float dir) Loot_CycleSlot;
void() Loot_Reload;
void() Loot_SwapWithFloor;
void() Loot_DropCurrent;
void() Loot_FillEmptySlots;
void(float imp) Loot_DevImpulse;
void(entity m) Loot_MonsterDrop;
void(float type, float bias) Loot_MapGun;
void() Loot_NewParms;
void() Loot_SaveParms;
void() Loot_DecodeParms;
