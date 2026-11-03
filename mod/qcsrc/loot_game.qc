/*
==============================================================================

LOOT GAME

Connects loot_core.qc to the Quake world:
- four gun slots on the player, with magazines and reloads
- one fire function that reads the gun's stats
- guns lying on the floor, glowing in their rarity color
- monster drops, and map weapons turned into random guns
- carrying the slots from map to map

Controls (bound in mod/autoexec.cfg):
    1-4  gun slots       5  axe
    R    reload          E  swap with the gun on the floor
    G    drop gun        mouse wheel  next / previous gun

==============================================================================
*/

// ---------------------------------------------------------------------------
// Precache: every model and sound must be loaded when the map starts
// ---------------------------------------------------------------------------

void() Loot_Precache =
{
    precache_model("progs/v_light.mdl");

    precache_model("progs/g_shot.mdl");
    precache_model("progs/g_nail.mdl");
    precache_model("progs/g_nail2.mdl");
    precache_model("progs/g_rock.mdl");
    precache_model("progs/g_rock2.mdl");
    precache_model("progs/g_light.mdl");

    precache_model("progs/spike.mdl");
};

// ---------------------------------------------------------------------------
// Gun level
// Phase 7 will scale this with map progress. For now every gun is level 1.
// ---------------------------------------------------------------------------

float() Loot_AreaLevel =
{
    return 1;
};


// a fresh seed from Quake's random(). random() only has about 15 bits,
// so two calls are combined to fill the 24 bits a seed can use.
float() Loot_NewSeed =
{
    local float hi, lo;

    hi = floor(random() * 4096);
    lo = floor(random() * 4096);
    if (hi > 4095)
    {
        hi = 4095;
    }
    if (lo > 4095)
    {
        lo = 4095;
    }
    return hi * 4096 + lo;
};

// ---------------------------------------------------------------------------
// Slots
//
// QuakeC has no arrays, so each slot has its own four fields.
// Slot_Load copies one slot into cur_*, Slot_Store writes it back.
// ---------------------------------------------------------------------------

float cur_a, cur_b, cur_s, cur_m;

void(entity p, float n) Slot_Load =
{
    if (n == 1)
    {
        cur_a = p.slot1_a; cur_b = p.slot1_b; cur_s = p.slot1_s; cur_m = p.slot1_m;
    }
    else if (n == 2)
    {
        cur_a = p.slot2_a; cur_b = p.slot2_b; cur_s = p.slot2_s; cur_m = p.slot2_m;
    }
    else if (n == 3)
    {
        cur_a = p.slot3_a; cur_b = p.slot3_b; cur_s = p.slot3_s; cur_m = p.slot3_m;
    }
    else if (n == 4)
    {
        cur_a = p.slot4_a; cur_b = p.slot4_b; cur_s = p.slot4_s; cur_m = p.slot4_m;
    }
    else
    {
        cur_a = cur_b = cur_s = cur_m = 0;
    }
};


void(entity p, float n, float a, float b, float s, float m) Slot_Store =
{
    if (n == 1)
    {
        p.slot1_a = a; p.slot1_b = b; p.slot1_s = s; p.slot1_m = m;
    }
    else if (n == 2)
    {
        p.slot2_a = a; p.slot2_b = b; p.slot2_s = s; p.slot2_m = m;
    }
    else if (n == 3)
    {
        p.slot3_a = a; p.slot3_b = b; p.slot3_s = s; p.slot3_m = m;
    }
    else if (n == 4)
    {
        p.slot4_a = a; p.slot4_b = b; p.slot4_s = s; p.slot4_m = m;
    }
};


// first empty slot, or 0 if all four are full
float(entity p) Slot_FindFree =
{
    if (!p.slot1_a)
    {
        return 1;
    }
    if (!p.slot2_a)
    {
        return 2;
    }
    if (!p.slot3_a)
    {
        return 3;
    }
    if (!p.slot4_a)
    {
        return 4;
    }
    return 0;
};

// ---------------------------------------------------------------------------
// Mapping gun types onto Quake's weapons (models, HUD icons, ammo)
// ---------------------------------------------------------------------------

// HUD icon. Also what player.qc checks to pick run animations.
float(float t, float proj) Loot_TypeFlag =
{
    if (t == LT_PISTOL)
    {
        return IT_SHOTGUN;
    }
    if (t == LT_SMG)
    {
        return IT_NAILGUN;
    }
    if (t == LT_SHOTGUN)
    {
        return IT_SUPER_SHOTGUN;
    }
    if (t == LT_RIFLE)
    {
        return IT_SUPER_NAILGUN;
    }
    if (t == LT_SNIPER)
    {
        return IT_LIGHTNING;
    }
    if (proj == LP_GRENADE)
    {
        return IT_GRENADE_LAUNCHER;
    }
    return IT_ROCKET_LAUNCHER;
};


string(float t, float proj) Loot_ViewModel =
{
    if (t == LT_PISTOL)
    {
        return "progs/v_shot.mdl";
    }
    if (t == LT_SMG)
    {
        return "progs/v_nail.mdl";
    }
    if (t == LT_SHOTGUN)
    {
        return "progs/v_shot2.mdl";
    }
    if (t == LT_RIFLE)
    {
        return "progs/v_nail2.mdl";
    }
    if (t == LT_SNIPER)
    {
        return "progs/v_light.mdl";
    }
    if (proj == LP_GRENADE)
    {
        return "progs/v_rock.mdl";
    }
    return "progs/v_rock2.mdl";
};


string(float t, float proj) Loot_WorldModel =
{
    if (t == LT_PISTOL || t == LT_SHOTGUN)
    {
        return "progs/g_shot.mdl";
    }
    if (t == LT_SMG)
    {
        return "progs/g_nail.mdl";
    }
    if (t == LT_RIFLE)
    {
        return "progs/g_nail2.mdl";
    }
    if (t == LT_SNIPER)
    {
        return "progs/g_light.mdl";
    }
    if (proj == LP_GRENADE)
    {
        return "progs/g_rock.mdl";
    }
    return "progs/g_rock2.mdl";
};


float(entity p, float ammo) Loot_PoolGet =
{
    if (ammo == LA_SHELLS)
    {
        return p.ammo_shells;
    }
    if (ammo == LA_NAILS)
    {
        return p.ammo_nails;
    }
    if (ammo == LA_ROCKETS)
    {
        return p.ammo_rockets;
    }
    return p.ammo_cells;
};


void(entity p, float ammo, float v) Loot_PoolSet =
{
    if (ammo == LA_SHELLS)
    {
        p.ammo_shells = v;
    }
    else if (ammo == LA_NAILS)
    {
        p.ammo_nails = v;
    }
    else if (ammo == LA_ROCKETS)
    {
        p.ammo_rockets = v;
    }
    else
    {
        p.ammo_cells = v;
    }
};


float(float ammo) Loot_AmmoItemBit =
{
    if (ammo == LA_SHELLS)
    {
        return IT_SHELLS;
    }
    if (ammo == LA_NAILS)
    {
        return IT_NAILS;
    }
    if (ammo == LA_ROCKETS)
    {
        return IT_ROCKETS;
    }
    return IT_CELLS;
};

// ---------------------------------------------------------------------------
// Current weapon
// ---------------------------------------------------------------------------

/*
Loot_RefreshWeapon

Called from W_SetCurrentAmmo (self = player). Sets the view model, the
HUD icons and the HUD ammo number from the selected slot.
The big HUD ammo number shows the MAGAZINE. The small numbers at the top
of the HUD still show the reserve pools.
*/
void() Loot_RefreshWeapon =
{
    local float i;

    // one HUD icon per equipped gun
    self.items = self.items - (self.items & (IT_SHOTGUN | IT_SUPER_SHOTGUN | IT_NAILGUN | IT_SUPER_NAILGUN | IT_GRENADE_LAUNCHER | IT_ROCKET_LAUNCHER | IT_LIGHTNING));
    self.items = self.items | IT_AXE;
    i = 1;
    while (i <= 4)
    {
        Slot_Load(self, i);
        if (cur_a)
        {
            Loot_Stats(cur_a, cur_b, cur_s);
            self.items = self.items | Loot_TypeFlag(lg_type, ls_proj);
        }
        i = i + 1;
    }

    if (self.loot_slot)
    {
        Slot_Load(self, self.loot_slot);
        if (!cur_a)
        {
            self.loot_slot = 0;
        }
    }

    self.weaponframe = 0;

    if (!self.loot_slot)
    {
        self.weapon = IT_AXE;
        self.currentammo = 0;
        self.weaponmodel = "progs/v_axe.mdl";
        return;
    }

    Loot_Stats(cur_a, cur_b, cur_s);
    self.weapon = Loot_TypeFlag(lg_type, ls_proj);
    self.weaponmodel = Loot_ViewModel(lg_type, ls_proj);
    self.currentammo = cur_m;
    self.items = self.items | Loot_AmmoItemBit(ls_ammo);
};


// n = 0 selects the axe
void(float n) Loot_SelectSlot =
{
    if (n)
    {
        Slot_Load(self, n);
        if (!cur_a)
        {
            sprint(self, "That slot is empty.\n");
            return;
        }
    }

    self.loot_slot = n;
    self.loot_reload_end = 0; // switching cancels a reload
    self.loot_burst_left = 0;
    W_SetCurrentAmmo();

    if (n)
    {
        Slot_Load(self, n);
        Loot_Stats(cur_a, cur_b, cur_s);
        sprint(self, Loot_S4(ftos(n), ": ", Loot_Name(), "\n"));
    }
};


// next (dir 1) or previous (dir -1) gun. Falls back to the axe.
void(float dir) Loot_CycleSlot =
{
    local float n, tries;

    n = self.loot_slot;
    tries = 0;
    while (tries < 4)
    {
        n = n + dir;
        if (n > 4)
        {
            n = 1;
        }
        if (n < 1)
        {
            n = 4;
        }
        Slot_Load(self, n);
        if (cur_a)
        {
            if (n != self.loot_slot)
            {
                Loot_SelectSlot(n);
            }
            return;
        }
        tries = tries + 1;
    }
    Loot_SelectSlot(0);
};


// switch to any gun that can still shoot, else the axe
void() Loot_SwitchToLoadedGun =
{
    local float n;

    n = 1;
    while (n <= 4)
    {
        if (n != self.loot_slot)
        {
            Slot_Load(self, n);
            if (cur_a)
            {
                Loot_Stats(cur_a, cur_b, cur_s);
                if (cur_m > 0 || Loot_PoolGet(self, ls_ammo) > 0)
                {
                    Loot_SelectSlot(n);
                    return;
                }
            }
        }
        n = n + 1;
    }
    Loot_SelectSlot(0);
};

// ---------------------------------------------------------------------------
// Reload
// ---------------------------------------------------------------------------

void() Loot_Reload =
{
    if (!self.loot_slot || self.loot_reload_end)
    {
        return;
    }

    Slot_Load(self, self.loot_slot);
    if (!cur_a)
    {
        return;
    }
    Loot_Stats(cur_a, cur_b, cur_s);

    if (cur_m >= ls_mag)
    {
        return; // already full
    }

    if (Loot_PoolGet(self, ls_ammo) < 1)
    {
        if (cur_m < 1)
        {
            sprint(self, "Out of ammo!\n");
            Loot_SwitchToLoadedGun();
        }
        return;
    }

    self.loot_reload_end = time + ls_reload;
    self.loot_burst_left = 0;
    sound(self, CHAN_ITEM, "weapons/lock4.wav", 1, ATTN_NORM);
};


// move ammo from the reserve pool into the magazine
void() Loot_FinishReload =
{
    local float need, pool;

    self.loot_reload_end = 0;

    Slot_Load(self, self.loot_slot);
    if (!cur_a)
    {
        return;
    }
    Loot_Stats(cur_a, cur_b, cur_s);

    need = ls_mag - cur_m;
    pool = Loot_PoolGet(self, ls_ammo);
    if (need > pool)
    {
        need = pool;
    }
    if (need < 1)
    {
        return;
    }

    cur_m = cur_m + need;
    Loot_PoolSet(self, ls_ammo, pool - need);
    Slot_Store(self, self.loot_slot, cur_a, cur_b, cur_s, cur_m);
    self.currentammo = cur_m;
};

// ---------------------------------------------------------------------------
// Shooting
// ---------------------------------------------------------------------------

// an explosion that never hurts "ignore" (used so your own explosive
// bullets don't hurt you)
void(vector org, float shotdmg, entity attacker, entity ignore) Loot_Explode =
{
    local entity boom;

    boom = spawn();
    setorigin(boom, org);
    T_RadiusDamage(boom, attacker, shotdmg, ignore);
    remove(boom);

    WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
    WriteByte(MSG_BROADCAST, TE_EXPLOSION);
    WriteCoord(MSG_BROADCAST, org_x);
    WriteCoord(MSG_BROADCAST, org_y);
    WriteCoord(MSG_BROADCAST, org_z);
};


// Like TraceAttack in weapons.qc, but the blood effect is capped.
// The particle count is sent as one byte, so huge sniper damage
// would wrap around to a random number.
void(float damage, vector dir) Loot_TraceAttack =
{
    local vector vel, org;
    local float blood;

    vel = normalize(dir + v_up * crandom() + v_right * crandom());
    vel = vel + 2 * trace_plane_normal;
    vel = vel * 200;

    org = trace_endpos - dir * 4;

    if (trace_ent.takedamage)
    {
        blood = damage;
        if (blood > 60)
        {
            blood = 60;
        }
        SpawnBlood(org, vel * 0.2, blood);
        AddMultiDamage(trace_ent, damage);
    }
    else
    {
        WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
        WriteByte(MSG_BROADCAST, TE_GUNSHOT);
        WriteCoord(MSG_BROADCAST, org_x);
        WriteCoord(MSG_BROADCAST, org_y);
        WriteCoord(MSG_BROADCAST, org_z);
    }
};


// instant shots: pistol, shotgun, rifle, sniper
void(float shotdmg, float pellets, float spread, float splash, float beam) Loot_FireHitscan =
{
    local vector dir, direction, src, splashpos, beamend;
    local float n, gotsplash;

    makevectors(self.v_angle);
    dir = aim(self, 100000);

    src = self.origin + v_forward * 10;
    src_z = self.absmin_z + self.size_z * 0.7;

    gotsplash = FALSE;
    splashpos = '0 0 0';
    beamend = src + dir * 4096;

    ClearMultiDamage();
    n = pellets;
    while (n > 0)
    {
        direction = dir + crandom() * spread * v_right + crandom() * spread * v_up;
        traceline(src, src + direction * 4096, FALSE, self);
        beamend = trace_endpos;
        if (trace_fraction != 1.0)
        {
            Loot_TraceAttack(shotdmg, direction);
            if (!gotsplash)
            {
                gotsplash = TRUE;
                splashpos = trace_endpos - direction * 4;
            }
        }
        n = n - 1;
    }
    ApplyMultiDamage();

    if (beam)
    {
        WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
        WriteByte(MSG_BROADCAST, TE_LIGHTNING2);
        WriteEntity(MSG_BROADCAST, self);
        WriteCoord(MSG_BROADCAST, src_x);
        WriteCoord(MSG_BROADCAST, src_y);
        WriteCoord(MSG_BROADCAST, src_z);
        WriteCoord(MSG_BROADCAST, beamend_x);
        WriteCoord(MSG_BROADCAST, beamend_y);
        WriteCoord(MSG_BROADCAST, beamend_z);
    }

    if (splash > 0 && gotsplash)
    {
        Loot_Explode(splashpos, shotdmg * pellets * splash, self, self);
    }
};


// spike_touch from weapons.qc, but damage comes from the gun
void() Loot_SpikeTouch =
{
    if (other == self.owner)
    {
        return;
    }
    if (other.solid == SOLID_TRIGGER)
    {
        return;
    }
    if (pointcontents(self.origin) == CONTENT_SKY)
    {
        remove(self);
        return;
    }

    if (other.takedamage)
    {
        spawn_touchblood(self.dmg);
        T_Damage(other, self, self.owner, self.dmg);
    }
    else
    {
        WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
        WriteByte(MSG_BROADCAST, TE_SPIKE);
        WriteCoord(MSG_BROADCAST, self.origin_x);
        WriteCoord(MSG_BROADCAST, self.origin_y);
        WriteCoord(MSG_BROADCAST, self.origin_z);
    }

    if (self.loot_splash > 0)
    {
        Loot_Explode(self.origin, self.dmg * self.loot_splash, self.owner, self.owner);
    }

    remove(self);
};


void(float shotdmg, float pellets, float spread, float shotspeed, float splash) Loot_FireSpikes =
{
    local vector dir, direction;
    local float n;

    makevectors(self.v_angle);
    dir = aim(self, 1000);

    n = pellets;
    while (n > 0)
    {
        direction = normalize(dir + crandom() * spread * v_right + crandom() * spread * v_up);
        launch_spike(self.origin + '0 0 16' + v_right * crandom() * 4, direction);
        newmis.touch = Loot_SpikeTouch;
        newmis.dmg = shotdmg;
        newmis.loot_splash = splash;
        newmis.velocity = direction * shotspeed;
        n = n - 1;
    }
};


// T_MissileTouch from weapons.qc, damage from the gun
void() Loot_RocketTouch =
{
    local float damg;

    if (other == self.owner)
    {
        return;
    }
    if (pointcontents(self.origin) == CONTENT_SKY)
    {
        remove(self);
        return;
    }

    damg = self.dmg;
    if (other.health)
    {
        if (other.classname == "monster_shambler")
        {
            damg = damg * 0.5; // mostly immune
        }
        T_Damage(other, self, self.owner, damg);
    }

    // the direct hit already took its damage, so skip it here
    T_RadiusDamage(self, self.owner, self.dmg * (1.2 + self.loot_splash), other);

    self.origin = self.origin - 8 * normalize(self.velocity);

    WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
    WriteByte(MSG_BROADCAST, TE_EXPLOSION);
    WriteCoord(MSG_BROADCAST, self.origin_x);
    WriteCoord(MSG_BROADCAST, self.origin_y);
    WriteCoord(MSG_BROADCAST, self.origin_z);

    BecomeExplosion();
};


void(float shotdmg, float pellets, float spread, float shotspeed, float splash) Loot_FireRockets =
{
    local entity missile;
    local vector dir, direction;
    local float n;

    makevectors(self.v_angle);
    dir = aim(self, 1000);

    n = pellets;
    while (n > 0)
    {
        direction = normalize(dir + crandom() * spread * v_right + crandom() * spread * v_up);

        missile = spawn();
        missile.owner = self;
        missile.movetype = MOVETYPE_FLYMISSILE;
        missile.solid = SOLID_BBOX;
        missile.classname = "missile";
        missile.dmg = shotdmg;
        missile.loot_splash = splash;

        missile.velocity = direction * shotspeed;
        missile.angles = vectoangles(missile.velocity);
        missile.touch = Loot_RocketTouch;
        missile.nextthink = time + 5;
        missile.think = SUB_Remove;

        setmodel(missile, "progs/missile.mdl");
        setsize(missile, '0 0 0', '0 0 0');
        setorigin(missile, self.origin + v_forward * 8 + '0 0 16' + v_right * crandom() * 6);
        n = n - 1;
    }
};


void() Loot_GrenadeExplode =
{
    T_RadiusDamage(self, self.owner, self.dmg * (1.4 + self.loot_splash), world);

    WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
    WriteByte(MSG_BROADCAST, TE_EXPLOSION);
    WriteCoord(MSG_BROADCAST, self.origin_x);
    WriteCoord(MSG_BROADCAST, self.origin_y);
    WriteCoord(MSG_BROADCAST, self.origin_z);

    BecomeExplosion();
};


void() Loot_GrenadeTouch =
{
    if (other == self.owner)
    {
        return;
    }
    if (other.takedamage == DAMAGE_AIM)
    {
        Loot_GrenadeExplode();
        return;
    }
    sound(self, CHAN_WEAPON, "weapons/bounce.wav", 1, ATTN_NORM);
    if (self.velocity == '0 0 0')
    {
        self.avelocity = '0 0 0';
    }
};


void(float shotdmg, float pellets, float spread, float shotspeed, float splash) Loot_FireGrenades =
{
    local entity missile;
    local float n;

    makevectors(self.v_angle);

    n = pellets;
    while (n > 0)
    {
        missile = spawn();
        missile.owner = self;
        missile.movetype = MOVETYPE_BOUNCE;
        missile.solid = SOLID_BBOX;
        missile.classname = "grenade";
        missile.dmg = shotdmg;
        missile.loot_splash = splash;

        missile.velocity = v_forward * shotspeed + v_up * 200 + crandom() * v_right * (10 + spread * 600) + crandom() * v_up * (10 + spread * 600);
        missile.avelocity = '300 300 300';
        missile.angles = vectoangles(missile.velocity);
        missile.touch = Loot_GrenadeTouch;
        missile.nextthink = time + 2.5;
        missile.think = Loot_GrenadeExplode;

        setmodel(missile, "progs/grenade.mdl");
        setsize(missile, '0 0 0', '0 0 0');
        setorigin(missile, self.origin);
        n = n - 1;
    }
};


void() Loot_PlaySound =
{
    local string s;

    if (ls_proj == LP_GRENADE)
    {
        s = "weapons/grenade.wav";
    }
    else if (lg_type == LT_PISTOL)
    {
        s = "weapons/guncock.wav";
    }
    else if (lg_type == LT_SMG)
    {
        s = "weapons/rocket1i.wav";
    }
    else if (lg_type == LT_SHOTGUN)
    {
        s = "weapons/shotgn2.wav";
    }
    else if (lg_type == LT_RIFLE)
    {
        s = "weapons/spike2.wav";
    }
    else if (lg_type == LT_SNIPER)
    {
        s = "weapons/lhit.wav";
    }
    else
    {
        s = "weapons/sgun1.wav";
    }
    sound(self, CHAN_WEAPON, s, 1, ATTN_NORM);
};


/*
Loot_FireOnce

One trigger pull (or one shot of a burst).
Needs Slot_Load + Loot_Stats already done for the selected slot.
*/
void() Loot_FireOnce =
{
    local float kick;

    Loot_PlaySound();

    kick = 1 + ls_damage * ls_pellets / 30;
    if (kick > 6)
    {
        kick = 6;
    }
    self.punchangle_x = 0 - kick;

    if (ls_proj == LP_HITSCAN)
    {
        Loot_FireHitscan(ls_damage, ls_pellets, ls_spread, ls_splash, ls_beam);
    }
    else if (ls_proj == LP_SPIKE)
    {
        Loot_FireSpikes(ls_damage, ls_pellets, ls_spread, ls_speed, ls_splash);
    }
    else if (ls_proj == LP_GRENADE)
    {
        Loot_FireGrenades(ls_damage, ls_pellets, ls_spread, ls_speed, ls_splash);
    }
    else
    {
        Loot_FireRockets(ls_damage, ls_pellets, ls_spread, ls_speed, ls_splash);
    }

    cur_m = cur_m - 1;
    Slot_Store(self, self.loot_slot, cur_a, cur_b, cur_s, cur_m);
    self.currentammo = cur_m;
};


// called from W_Attack when a gun slot is selected
void() Loot_Attack =
{
    if (time < self.loot_reload_end)
    {
        return;
    }

    Slot_Load(self, self.loot_slot);
    if (!cur_a)
    {
        Loot_SelectSlot(0);
        return;
    }
    Loot_Stats(cur_a, cur_b, cur_s);

    if (cur_m < 1)
    {
        Loot_Reload();
        return;
    }

    self.show_hostile = time + 1; // wake monsters up

    if (lg_type == LT_LAUNCHER)
    {
        player_rocket1();
    }
    else if (lg_type == LT_SNIPER)
    {
        player_lshot1();
    }
    else
    {
        player_shot1();
    }

    Loot_FireOnce();
    self.attack_finished = time + ls_delay;

    if (ls_burst > 1)
    {
        self.loot_burst_left = ls_burst - 1;
        self.loot_burst_next = time + 0.07;
    }
};


// every frame, from W_WeaponFrame: finish reloads, fire burst shots
void() Loot_WeaponFrame =
{
    if (self.loot_reload_end && time >= self.loot_reload_end)
    {
        Loot_FinishReload();
    }

    if (self.loot_burst_left > 0 && time >= self.loot_burst_next)
    {
        Slot_Load(self, self.loot_slot);
        if (!cur_a || cur_m < 1 || self.loot_reload_end)
        {
            self.loot_burst_left = 0;
            return;
        }
        Loot_Stats(cur_a, cur_b, cur_s);
        Loot_FireOnce();
        self.loot_burst_left = self.loot_burst_left - 1;
        self.loot_burst_next = time + 0.07;
    }
};


// every frame, from PlayerPreThink: empty magazine means reload
void() Loot_AmmoThink =
{
    if (!self.loot_slot || self.loot_reload_end || self.loot_burst_left > 0)
    {
        return;
    }
    if (time < self.attack_finished)
    {
        return;
    }
    if (self.currentammo > 0)
    {
        return;
    }
    Loot_Reload();
};

// ---------------------------------------------------------------------------
// Guns on the floor
// ---------------------------------------------------------------------------

// particle palette colors: white, green, blue, purple, orange
float(float r) Loot_RarityColor =
{
    if (r == LR_COMMON)
    {
        return 8;
    }
    if (r == LR_UNCOMMON)
    {
        return 48;
    }
    if (r == LR_RARE)
    {
        return 208;
    }
    if (r == LR_EPIC)
    {
        return 152;
    }
    return 224;
};


void() Loot_ItemThink =
{
    local float col;

    Loot_Unpack(self.gun_a, self.gun_b);
    col = Loot_RarityColor(lg_rarity);

    particle(self.origin + '0 0 24', '0 0 8', col, 2 + lg_rarity * 3);

    // legendary: a pillar of light you can see across the room
    if (lg_rarity == LR_LEGENDARY)
    {
        particle(self.origin + '0 0 56', '0 0 40', col, 6);
        particle(self.origin + '0 0 96', '0 0 40', col, 6);
    }

    self.nextthink = time + 0.25;
};


void(entity g) Loot_ItemLight =
{
    Loot_Unpack(g.gun_a, g.gun_b);
    if (lg_rarity >= LR_EPIC)
    {
        g.effects = g.effects | EF_DIMLIGHT;
    }
};


void() Loot_ItemTouch;

// a gun lying in the world. m = -1 means a full magazine.
entity(vector org, vector vel, float a, float b, float s, float m) Loot_SpawnItem =
{
    local entity g;

    g = spawn();
    g.classname = "loot_gun";
    g.gun_a = a;
    g.gun_b = b;
    g.gun_s = s;
    g.gun_m = m;

    Loot_Stats(a, b, s);
    setmodel(g, Loot_WorldModel(lg_type, ls_proj));

    g.flags = FL_ITEM;
    g.solid = SOLID_TRIGGER;
    g.movetype = MOVETYPE_TOSS;
    setsize(g, '-16 -16 0', '16 16 56');
    setorigin(g, org);
    g.velocity = vel;

    g.touch = Loot_ItemTouch;
    g.think = Loot_ItemThink;
    g.nextthink = time + 0.1;
    Loot_ItemLight(g);
    return g;
};


// put gun entity g into slot n of player p, then remove g
void(entity p, float n, entity g, float doselect) Loot_GiveToSlot =
{
    local entity oself;
    local float m;

    Loot_Stats(g.gun_a, g.gun_b, g.gun_s);
    m = g.gun_m;
    if (m < 0)
    {
        m = ls_mag;
    }
    Slot_Store(p, n, g.gun_a, g.gun_b, g.gun_s, m);

    sound(p, CHAN_ITEM, "weapons/pkup.wav", 1, ATTN_NORM);
    stuffcmd(p, "bf\n");
    sprint(p, Loot_S5("You got: ", Loot_Name(), " (", Loot_RarityName(lg_rarity), ")\n"));

    // map weapons can trigger events when picked up, keep that working
    oself = self;
    self = g;
    activator = p;
    SUB_UseTargets();
    self = oself;

    remove(g);

    oself = self;
    self = p;
    if (doselect || !self.loot_slot)
    {
        Loot_SelectSlot(n);
    }
    else
    {
        W_SetCurrentAmmo(); // update HUD icons
    }
    self = oself;
};


void(entity p, entity g) Loot_ShowCard =
{
    local float slot, cmp;
    local string text;

    cmp = -1;
    slot = p.loot_slot;
    if (!slot)
    {
        slot = 1;
    }
    Slot_Load(p, slot);
    if (cur_a)
    {
        Loot_Stats(cur_a, cur_b, cur_s);
        cmp = ls_dps;
    }

    text = Loot_Card(g.gun_a, g.gun_b, g.gun_s, cmp);
    text = strcat(text, "\nE: swap with your gun");
    centerprint(p, text);
};


void() Loot_ItemTouch =
{
    local float free;

    if (other.classname != "player")
    {
        return;
    }
    if (other.health <= 0)
    {
        return;
    }
    if (time < self.loot_pickup_time)
    {
        return;
    }

    // a free slot: just take it
    free = Slot_FindFree(other);
    if (free)
    {
        Loot_GiveToSlot(other, free, self, FALSE);
        return;
    }

    // all slots full: show the card, E swaps
    other.loot_near = self;
    other.loot_near_time = time;
    if (other.loot_card_gun == self && time < other.loot_card_time + 1.5)
    {
        return;
    }
    other.loot_card_gun = self;
    other.loot_card_time = time;
    Loot_ShowCard(other, self);
};


// throw a gun from the player's hands
entity(float a, float b, float s, float m) Loot_ThrowGun =
{
    local entity g;

    makevectors(self.v_angle);
    g = Loot_SpawnItem(self.origin, v_forward * 250 + '0 0 200', a, b, s, m);
    g.loot_pickup_time = time + 1.5;
    return g;
};


// impulse 14: swap the selected gun with the one on the floor
void() Loot_SwapWithFloor =
{
    local entity g;
    local float n;

    g = self.loot_near;
    if (!g || time > self.loot_near_time + 0.3)
    {
        return;
    }
    if (g.classname != "loot_gun")
    {
        return;
    }

    n = self.loot_slot;
    if (!n)
    {
        n = Slot_FindFree(self);
    }
    if (!n)
    {
        n = 1;
    }

    Slot_Load(self, n);
    if (cur_a)
    {
        Loot_ThrowGun(cur_a, cur_b, cur_s, cur_m);
    }

    self.loot_near = world;
    self.loot_card_gun = world;
    Loot_GiveToSlot(self, n, g, TRUE);
};


// impulse 15: drop the selected gun
void() Loot_DropCurrent =
{
    if (!self.loot_slot)
    {
        return;
    }
    Slot_Load(self, self.loot_slot);
    if (!cur_a)
    {
        return;
    }

    Loot_ThrowGun(cur_a, cur_b, cur_s, cur_m);
    Slot_Store(self, self.loot_slot, 0, 0, 0, 0);
    Loot_CycleSlot(1);
};

// ---------------------------------------------------------------------------
// Where guns come from
// ---------------------------------------------------------------------------

// Called from Killed() in combat.qc. Tougher monsters drop more often
// and roll better rarity.
void(entity m) Loot_MonsterDrop =
{
    local float chance, bias, drops;
    local vector vel;

    chance = 0.2;
    bias = 0;
    drops = 1;

    if (m.classname == "monster_dog")
    {
        chance = 0.1;
    }
    else if (m.classname == "monster_fish")
    {
        chance = 0.05;
    }
    else if (m.classname == "monster_army")
    {
        chance = 0.25;
    }
    else if (m.classname == "monster_knight" || m.classname == "monster_wizard" || m.classname == "monster_enforcer")
    {
        chance = 0.3;
    }
    else if (m.classname == "monster_ogre" || m.classname == "monster_demon1" || m.classname == "monster_hell_knight")
    {
        chance = 0.5;
        bias = 0.5;
    }
    else if (m.classname == "monster_shalrath")
    {
        chance = 0.6;
        bias = 1;
    }
    else if (m.classname == "monster_shambler")
    {
        chance = 1;
        bias = 1.5;
    }
    else if (m.classname == "monster_oldone")
    {
        chance = 1;
        bias = 4;
        drops = 3;
    }

    if (random() >= chance)
    {
        return;
    }

    while (drops > 0)
    {
        Loot_Roll(Loot_NewSeed(), Loot_AreaLevel(), bias, -1, -1);
        vel = '0 0 1' * (250 + random() * 100) + '1 0 0' * crandom() * 120 + '0 1 0' * crandom() * 120;
        Loot_SpawnItem(m.origin, vel, lr_a, lr_b, lr_s, -1);
        drops = drops - 1;
    }
};


void() Loot_PlaceThink =
{
    self.flags = FL_ITEM;
    self.solid = SOLID_TRIGGER;
    self.movetype = MOVETYPE_TOSS;
    self.velocity = '0 0 0';
    self.origin_z = self.origin_z + 6;
    if (!droptofloor(0, 0))
    {
        dprint("Loot gun fell out of level at ");
        dprint(vtos(self.origin));
        dprint("\n");
        remove(self);
        return;
    }
    self.think = Loot_ItemThink;
    self.nextthink = time + 0.1;
};


// Called by the weapon_* spawn functions in items.qc. The map's weapon
// becomes a random gun of a matching type, with a rarity bonus.
void(float type, float bias) Loot_MapGun =
{
    Loot_Roll(Loot_NewSeed(), Loot_AreaLevel(), bias, type, -1);

    self.classname = "loot_gun";
    self.gun_a = lr_a;
    self.gun_b = lr_b;
    self.gun_s = lr_s;
    self.gun_m = -1;

    Loot_Stats(lr_a, lr_b, lr_s);
    setmodel(self, Loot_WorldModel(lg_type, ls_proj));
    setsize(self, '-16 -16 0', '16 16 56');
    self.touch = Loot_ItemTouch;
    Loot_ItemLight(self);

    // like StartItem: wait for the world to settle, then drop to floor
    self.think = Loot_PlaceThink;
    self.nextthink = time + 0.2;
};

// ---------------------------------------------------------------------------
// Carrying guns between maps
// ---------------------------------------------------------------------------

// a new game: a common pistol in slot 1
void() Loot_NewParms =
{
    parm17 = LOOT_PARM_MAGIC;

    Loot_Roll(Loot_NewSeed(), 1, 0, LT_PISTOL, LR_COMMON);
    parm18 = lr_a;
    parm19 = lr_b;
    parm20 = lr_s;
    parm21 = -1;

    parm22 = parm23 = parm24 = parm25 = 0;
    parm26 = parm27 = parm28 = parm29 = 0;
    parm30 = parm31 = parm32 = parm33 = 0;
    parm34 = 1;
};


// leaving a map (self = player)
void() Loot_SaveParms =
{
    parm17 = LOOT_PARM_MAGIC;
    parm18 = self.slot1_a; parm19 = self.slot1_b; parm20 = self.slot1_s; parm21 = self.slot1_m;
    parm22 = self.slot2_a; parm23 = self.slot2_b; parm24 = self.slot2_s; parm25 = self.slot2_m;
    parm26 = self.slot3_a; parm27 = self.slot3_b; parm28 = self.slot3_s; parm29 = self.slot3_m;
    parm30 = self.slot4_a; parm31 = self.slot4_b; parm32 = self.slot4_s; parm33 = self.slot4_m;
    parm34 = self.loot_slot;
};


// entering a map (self = player)
void() Loot_DecodeParms =
{
    local float n;

    // On a brand new game vkQuake does not fill parm17+ from SetNewParms
    // by name, so they can hold junk. The magic number tells us.
    if (parm17 != LOOT_PARM_MAGIC)
    {
        Loot_NewParms();
    }

    self.slot1_a = parm18; self.slot1_b = parm19; self.slot1_s = parm20; self.slot1_m = parm21;
    self.slot2_a = parm22; self.slot2_b = parm23; self.slot2_s = parm24; self.slot2_m = parm25;
    self.slot3_a = parm26; self.slot3_b = parm27; self.slot3_s = parm28; self.slot3_m = parm29;
    self.slot4_a = parm30; self.slot4_b = parm31; self.slot4_s = parm32; self.slot4_m = parm33;
    self.loot_slot = parm34;

    // -1 magazine means "full"
    n = 1;
    while (n <= 4)
    {
        Slot_Load(self, n);
        if (cur_a && cur_m < 0)
        {
            Loot_Stats(cur_a, cur_b, cur_s);
            Slot_Store(self, n, cur_a, cur_b, cur_s, ls_mag);
        }
        n = n + 1;
    }

    self.loot_reload_end = 0;
    self.loot_burst_left = 0;

    sprint(self, "1-4 guns, 5 axe, R reload, E swap, G drop\n");
};

// ---------------------------------------------------------------------------
// Cheats and developer tools (single player only)
// ---------------------------------------------------------------------------

// impulse 9 also fills empty slots with random guns
void() Loot_FillEmptySlots =
{
    local float n;

    n = Slot_FindFree(self);
    while (n)
    {
        Loot_Roll(Loot_NewSeed(), Loot_AreaLevel(), 1, -1, -1);
        Loot_Stats(lr_a, lr_b, lr_s);
        Slot_Store(self, n, lr_a, lr_b, lr_s, ls_mag);
        n = Slot_FindFree(self);
    }
    if (!self.loot_slot)
    {
        self.loot_slot = 1;
    }
};


/*
impulse 200  spawn a random gun in front of you
impulse 201  spawn a gun with high rarity odds
impulse 202  print the selected gun's card and raw numbers to the console
*/
void(float imp) Loot_DevImpulse =
{
    local float bias;
    local entity g;

    if (deathmatch || coop)
    {
        return;
    }

    if (imp == 202)
    {
        if (!self.loot_slot)
        {
            sprint(self, "Axe selected.\n");
            return;
        }
        Slot_Load(self, self.loot_slot);
        sprint(self, Loot_Card(cur_a, cur_b, cur_s, -1));
        sprint(self, Loot_S6("a=", ftos(cur_a), " b=", ftos(cur_b), " s=", ftos(cur_s)));
        sprint(self, "\n");
        return;
    }

    bias = 0;
    if (imp == 201)
    {
        bias = 3;
    }

    Loot_Roll(Loot_NewSeed(), Loot_AreaLevel(), bias, -1, -1);
    makevectors(self.v_angle);
    g = Loot_SpawnItem(self.origin + v_forward * 32, v_forward * 200 + '0 0 200', lr_a, lr_b, lr_s, -1);
    g.loot_pickup_time = time + 0.5;
};
