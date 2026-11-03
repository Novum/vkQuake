/*  Copyright (C) 1996-1997  Id Software, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

/*
==============================================================================

SHAMBLER

==============================================================================
*/

$cd id1/models/shams
$origin 0 0 24
$base base
$skin base

$frame stand1 stand2 stand3 stand4 stand5 stand6 stand7 stand8 stand9
$frame stand10 stand11 stand12 stand13 stand14 stand15 stand16 stand17

$frame walk1 walk2 walk3 walk4 walk5 walk6 walk7
$frame walk8 walk9 walk10 walk11 walk12

$frame run1 run2 run3 run4 run5 run6

$frame smash1 smash2 smash3 smash4 smash5 smash6 smash7
$frame smash8 smash9 smash10 smash11 smash12

$frame swingr1 swingr2 swingr3 swingr4 swingr5
$frame swingr6 swingr7 swingr8 swingr9

$frame swingl1 swingl2 swingl3 swingl4 swingl5
$frame swingl6 swingl7 swingl8 swingl9

$frame magic1 magic2 magic3 magic4 magic5
$frame magic6 magic7 magic8 magic9 magic10 magic11 magic12

$frame pain1 pain2 pain3 pain4 pain5 pain6

$frame death1 death2 death3 death4 death5 death6
$frame death7 death8 death9 death10 death11


void() sham_stand1  = [ $stand1,  sham_stand2  ] { ai_stand(); };
void() sham_stand2  = [ $stand2,  sham_stand3  ] { ai_stand(); };
void() sham_stand3  = [ $stand3,  sham_stand4  ] { ai_stand(); };
void() sham_stand4  = [ $stand4,  sham_stand5  ] { ai_stand(); };
void() sham_stand5  = [ $stand5,  sham_stand6  ] { ai_stand(); };
void() sham_stand6  = [ $stand6,  sham_stand7  ] { ai_stand(); };
void() sham_stand7  = [ $stand7,  sham_stand8  ] { ai_stand(); };
void() sham_stand8  = [ $stand8,  sham_stand9  ] { ai_stand(); };
void() sham_stand9  = [ $stand9,  sham_stand10 ] { ai_stand(); };
void() sham_stand10 = [ $stand10, sham_stand11 ] { ai_stand(); };
void() sham_stand11 = [ $stand11, sham_stand12 ] { ai_stand(); };
void() sham_stand12 = [ $stand12, sham_stand13 ] { ai_stand(); };
void() sham_stand13 = [ $stand13, sham_stand14 ] { ai_stand(); };
void() sham_stand14 = [ $stand14, sham_stand15 ] { ai_stand(); };
void() sham_stand15 = [ $stand15, sham_stand16 ] { ai_stand(); };
void() sham_stand16 = [ $stand16, sham_stand17 ] { ai_stand(); };
void() sham_stand17 = [ $stand17, sham_stand1  ] { ai_stand(); };

void() sham_walk1  = [ $walk1,  sham_walk2  ] { ai_walk(10); };
void() sham_walk2  = [ $walk2,  sham_walk3  ] { ai_walk(9); };
void() sham_walk3  = [ $walk3,  sham_walk4  ] { ai_walk(9); };
void() sham_walk4  = [ $walk4,  sham_walk5  ] { ai_walk(5); };
void() sham_walk5  = [ $walk5,  sham_walk6  ] { ai_walk(6); };
void() sham_walk6  = [ $walk6,  sham_walk7  ] { ai_walk(12); };
void() sham_walk7  = [ $walk7,  sham_walk8  ] { ai_walk(8); };
void() sham_walk8  = [ $walk8,  sham_walk9  ] { ai_walk(3); };
void() sham_walk9  = [ $walk9,  sham_walk10 ] { ai_walk(13); };
void() sham_walk10 = [ $walk10, sham_walk11 ] { ai_walk(9); };
void() sham_walk11 = [ $walk11, sham_walk12 ] { ai_walk(7); };
void() sham_walk12 = [ $walk12, sham_walk1  ] {
    ai_walk(7);
    if (random() > 0.8)
    {
        sound(self, CHAN_VOICE, "shambler/sidle.wav", 1, ATTN_IDLE);
    }
};

void() sham_run1 = [ $run1, sham_run2 ] { ai_run(20); };
void() sham_run2 = [ $run2, sham_run3 ] { ai_run(24); };
void() sham_run3 = [ $run3, sham_run4 ] { ai_run(20); };
void() sham_run4 = [ $run4, sham_run5 ] { ai_run(20); };
void() sham_run5 = [ $run5, sham_run6 ] { ai_run(24); };
void() sham_run6 = [ $run6, sham_run1 ] {
    ai_run(20);
    if (random() > 0.8)
    {
        sound(self, CHAN_VOICE, "shambler/sidle.wav", 1, ATTN_IDLE);
    }
};

void() sham_smash1  = [ $smash1,  sham_smash2  ] { sound(self, CHAN_VOICE, "shambler/melee1.wav", 1, ATTN_NORM); ai_charge(2); };
void() sham_smash2  = [ $smash2,  sham_smash3  ] { ai_charge(6); };
void() sham_smash3  = [ $smash3,  sham_smash4  ] { ai_charge(6); };
void() sham_smash4  = [ $smash4,  sham_smash5  ] { ai_charge(5); };
void() sham_smash5  = [ $smash5,  sham_smash6  ] { ai_charge(4); };
void() sham_smash6  = [ $smash6,  sham_smash7  ] { ai_charge(1); };
void() sham_smash7  = [ $smash7,  sham_smash8  ] { ai_charge(0); };
void() sham_smash8  = [ $smash8,  sham_smash9  ] { ai_charge(0); };
void() sham_smash9  = [ $smash9,  sham_smash10 ] { ai_charge(0); };
void() sham_smash10 = [ $smash10, sham_smash11 ] {
    local vector delta;
    local float ldmg;

    if (!self.enemy)
    {
        return;
    }

    ai_charge(0);

    delta = self.enemy.origin - self.origin;
    if (vlen(delta) > 100)
    {
        return;
    }

    if (!CanDamage(self.enemy, self))
    {
        return;
    }

    ldmg = (random() + random() + random()) * 40;
    T_Damage(self.enemy, self, self, ldmg);
    sound(self, CHAN_VOICE, "shambler/smack.wav", 1, ATTN_NORM);

    SpawnMeatSpray(self.origin + v_forward * 16, crandom() * 100 * v_right);
    SpawnMeatSpray(self.origin + v_forward * 16, crandom() * 100 * v_right);
};
void() sham_smash11 = [ $smash11, sham_smash12 ] { ai_charge(5); };
void() sham_smash12 = [ $smash12, sham_run1    ] { ai_charge(4); };

void() sham_swingr1;

void(float side) ShamClaw =
{
    local vector delta;
    local float ldmg;

    if (!self.enemy)
    {
        return;
    }

    ai_charge(10);

    delta = self.enemy.origin - self.origin;
    if (vlen(delta) > 100)
    {
        return;
    }

    ldmg = (random() + random() + random()) * 20;
    T_Damage(self.enemy, self, self, ldmg);
    sound(self, CHAN_VOICE, "shambler/smack.wav", 1, ATTN_NORM);

    if (side)
    {
        makevectors(self.angles);
        SpawnMeatSpray(self.origin + v_forward * 16, side * v_right);
    }
};


void() sham_swingl1 = [ $swingl1, sham_swingl2 ] { sound(self, CHAN_VOICE, "shambler/melee2.wav", 1, ATTN_NORM); ai_charge(5); };
void() sham_swingl2 = [ $swingl2, sham_swingl3 ] { ai_charge(3); };
void() sham_swingl3 = [ $swingl3, sham_swingl4 ] { ai_charge(7); };
void() sham_swingl4 = [ $swingl4, sham_swingl5 ] { ai_charge(3); };
void() sham_swingl5 = [ $swingl5, sham_swingl6 ] { ai_charge(7); };
void() sham_swingl6 = [ $swingl6, sham_swingl7 ] { ai_charge(9); };
void() sham_swingl7 = [ $swingl7, sham_swingl8 ] { ai_charge(5); ShamClaw(250); };
void() sham_swingl8 = [ $swingl8, sham_swingl9 ] { ai_charge(4); };
void() sham_swingl9 = [ $swingl9, sham_run1    ] {
    ai_charge(8);
    if (random() < 0.5)
    {
        self.think = sham_swingr1;
    }
};

void() sham_swingr1 = [ $swingr1, sham_swingr2 ] { sound(self, CHAN_VOICE, "shambler/melee1.wav", 1, ATTN_NORM); ai_charge(1); };
void() sham_swingr2 = [ $swingr2, sham_swingr3 ] { ai_charge(8); };
void() sham_swingr3 = [ $swingr3, sham_swingr4 ] { ai_charge(14); };
void() sham_swingr4 = [ $swingr4, sham_swingr5 ] { ai_charge(7); };
void() sham_swingr5 = [ $swingr5, sham_swingr6 ] { ai_charge(3); };
void() sham_swingr6 = [ $swingr6, sham_swingr7 ] { ai_charge(6); };
void() sham_swingr7 = [ $swingr7, sham_swingr8 ] { ai_charge(6); ShamClaw(-250); };
void() sham_swingr8 = [ $swingr8, sham_swingr9 ] { ai_charge(3); };
void() sham_swingr9 = [ $swingr9, sham_run1    ] {
    ai_charge(1);
    ai_charge(10);
    if (random() < 0.5)
    {
        self.think = sham_swingl1;
    }
};

void() sham_melee =
{
    local float chance;

    chance = random();
    if (chance > 0.6 || self.health == 600)
    {
        sham_smash1();
    }
    else if (chance > 0.3)
    {
        sham_swingr1();
    }
    else
    {
        sham_swingl1();
    }
};


//============================================================================

void() CastLightning =
{
    local vector org, dir;

    self.effects = self.effects | EF_MUZZLEFLASH;

    ai_face();

    org = self.origin + '0 0 40';

    dir = self.enemy.origin + '0 0 16' - org;
    dir = normalize(dir);

    traceline(org, self.origin + dir * 600, TRUE, self);

    WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
    WriteByte(MSG_BROADCAST, TE_LIGHTNING1);
    WriteEntity(MSG_BROADCAST, self);
    WriteCoord(MSG_BROADCAST, org_x);
    WriteCoord(MSG_BROADCAST, org_y);
    WriteCoord(MSG_BROADCAST, org_z);
    WriteCoord(MSG_BROADCAST, trace_endpos_x);
    WriteCoord(MSG_BROADCAST, trace_endpos_y);
    WriteCoord(MSG_BROADCAST, trace_endpos_z);

    LightningDamage(org, trace_endpos, self, 10);
};


void() sham_magic1  = [ $magic1,  sham_magic2  ] { ai_face(); sound(self, CHAN_WEAPON, "shambler/sattck1.wav", 1, ATTN_NORM); };
void() sham_magic2  = [ $magic2,  sham_magic3  ] { ai_face(); };
void() sham_magic3  = [ $magic3,  sham_magic4  ] {
    ai_face();
    self.nextthink = self.nextthink + 0.2;

    local entity o;

    self.effects = self.effects | EF_MUZZLEFLASH;
    ai_face();
    self.owner = spawn();
    o = self.owner;
    setmodel(o, "progs/s_light.mdl");
    setorigin(o, self.origin);
    o.angles = self.angles;
    o.nextthink = time + 0.7;
    o.think = SUB_Remove;
};
void() sham_magic4  = [ $magic4,  sham_magic5  ] { self.effects = self.effects | EF_MUZZLEFLASH; self.owner.frame = 1; };
void() sham_magic5  = [ $magic5,  sham_magic6  ] { self.effects = self.effects | EF_MUZZLEFLASH; self.owner.frame = 2; };
void() sham_magic6  = [ $magic6,  sham_magic9  ] {
    remove(self.owner);
    CastLightning();
    sound(self, CHAN_WEAPON, "shambler/sboom.wav", 1, ATTN_NORM);
};
void() sham_magic9  = [ $magic9,  sham_magic10 ] { CastLightning(); };
void() sham_magic10 = [ $magic10, sham_magic11 ] { CastLightning(); };
void() sham_magic11 = [ $magic11, sham_magic12 ] {
    if (skill == 3)
    {
        CastLightning();
    }
};
void() sham_magic12 = [ $magic12, sham_run1    ] {};

void() sham_pain1 = [ $pain1, sham_pain2 ] {};
void() sham_pain2 = [ $pain2, sham_pain3 ] {};
void() sham_pain3 = [ $pain3, sham_pain4 ] {};
void() sham_pain4 = [ $pain4, sham_pain5 ] {};
void() sham_pain5 = [ $pain5, sham_pain6 ] {};
void() sham_pain6 = [ $pain6, sham_run1  ] {};

void(entity attacker, float damage) sham_pain =
{
    sound(self, CHAN_VOICE, "shambler/shurt2.wav", 1, ATTN_NORM);

    if (self.health <= 0)
    {
        return; // already dying, don't go into pain frame
    }

    if ((random() * 400) > damage)
    {
        return; // didn't flinch
    }

    if (self.pain_finished > time)
    {
        return;
    }

    self.pain_finished = time + 2;
    sham_pain1();
};


//============================================================================

void() sham_death1  = [ $death1,  sham_death2  ] {};
void() sham_death2  = [ $death2,  sham_death3  ] {};
void() sham_death3  = [ $death3,  sham_death4  ] { self.solid = SOLID_NOT; };
void() sham_death4  = [ $death4,  sham_death5  ] {};
void() sham_death5  = [ $death5,  sham_death6  ] {};
void() sham_death6  = [ $death6,  sham_death7  ] {};
void() sham_death7  = [ $death7,  sham_death8  ] {};
void() sham_death8  = [ $death8,  sham_death9  ] {};
void() sham_death9  = [ $death9,  sham_death10 ] {};
void() sham_death10 = [ $death10, sham_death11 ] {};
void() sham_death11 = [ $death11, sham_death11 ] {};

void() sham_die =
{
    // check for gib
    if (self.health < -60)
    {
        sound(self, CHAN_VOICE, "player/udeath.wav", 1, ATTN_NORM);
        ThrowHead("progs/h_shams.mdl", self.health);
        ThrowGib("progs/gib1.mdl", self.health);
        ThrowGib("progs/gib2.mdl", self.health);
        ThrowGib("progs/gib3.mdl", self.health);
        return;
    }

    // regular death
    sound(self, CHAN_VOICE, "shambler/sdeath.wav", 1, ATTN_NORM);
    sham_death1();
};


//============================================================================

/*QUAKED monster_shambler (1 0 0) (-32 -32 -24) (32 32 64) Ambush
*/
void() monster_shambler =
{
    if (deathmatch)
    {
        remove(self);
        return;
    }

    precache_model("progs/shambler.mdl");
    precache_model("progs/s_light.mdl");
    precache_model("progs/h_shams.mdl");
    precache_model("progs/bolt.mdl");

    precache_sound("shambler/sattck1.wav");
    precache_sound("shambler/sboom.wav");
    precache_sound("shambler/sdeath.wav");
    precache_sound("shambler/shurt2.wav");
    precache_sound("shambler/sidle.wav");
    precache_sound("shambler/ssight.wav");
    precache_sound("shambler/melee1.wav");
    precache_sound("shambler/melee2.wav");
    precache_sound("shambler/smack.wav");

    self.solid = SOLID_SLIDEBOX;
    self.movetype = MOVETYPE_STEP;
    setmodel(self, "progs/shambler.mdl");

    setsize(self, VEC_HULL2_MIN, VEC_HULL2_MAX);
    self.health = 600;

    self.th_stand = sham_stand1;
    self.th_walk = sham_walk1;
    self.th_run = sham_run1;
    self.th_die = sham_die;
    self.th_melee = sham_melee;
    self.th_missile = sham_magic1;
    self.th_pain = sham_pain;

    walkmonster_start();
};
