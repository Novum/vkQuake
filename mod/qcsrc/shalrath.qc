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

SHAL-RATH

==============================================================================
*/

$cd id1/models/shalrath
$origin 0 0 24
$base base
$skin skin
$scale 0.7

$frame attack1 attack2 attack3 attack4 attack5 attack6 attack7 attack8
$frame attack9 attack10 attack11

$frame pain1 pain2 pain3 pain4 pain5

$frame death1 death2 death3 death4 death5 death6 death7

$frame walk1 walk2 walk3 walk4 walk5 walk6 walk7 walk8 walk9 walk10
$frame walk11 walk12


void() ShalMissile;

void() shal_stand = [ $walk1, shal_stand ] { ai_stand(); };

void() shal_walk1  = [ $walk2,  shal_walk2  ] {
    if (random() < 0.2)
    {
        sound(self, CHAN_VOICE, "shalrath/idle.wav", 1, ATTN_IDLE);
    }
    ai_walk(6);
};
void() shal_walk2  = [ $walk3,  shal_walk3  ] { ai_walk(4); };
void() shal_walk3  = [ $walk4,  shal_walk4  ] { ai_walk(0); };
void() shal_walk4  = [ $walk5,  shal_walk5  ] { ai_walk(0); };
void() shal_walk5  = [ $walk6,  shal_walk6  ] { ai_walk(0); };
void() shal_walk6  = [ $walk7,  shal_walk7  ] { ai_walk(0); };
void() shal_walk7  = [ $walk8,  shal_walk8  ] { ai_walk(5); };
void() shal_walk8  = [ $walk9,  shal_walk9  ] { ai_walk(6); };
void() shal_walk9  = [ $walk10, shal_walk10 ] { ai_walk(5); };
void() shal_walk10 = [ $walk11, shal_walk11 ] { ai_walk(0); };
void() shal_walk11 = [ $walk12, shal_walk12 ] { ai_walk(4); };
void() shal_walk12 = [ $walk1,  shal_walk1  ] { ai_walk(5); };

void() shal_run1  = [ $walk2,  shal_run2  ] {
    if (random() < 0.2)
    {
        sound(self, CHAN_VOICE, "shalrath/idle.wav", 1, ATTN_IDLE);
    }
    ai_run(6);
};
void() shal_run2  = [ $walk3,  shal_run3  ] { ai_run(4); };
void() shal_run3  = [ $walk4,  shal_run4  ] { ai_run(0); };
void() shal_run4  = [ $walk5,  shal_run5  ] { ai_run(0); };
void() shal_run5  = [ $walk6,  shal_run6  ] { ai_run(0); };
void() shal_run6  = [ $walk7,  shal_run7  ] { ai_run(0); };
void() shal_run7  = [ $walk8,  shal_run8  ] { ai_run(5); };
void() shal_run8  = [ $walk9,  shal_run9  ] { ai_run(6); };
void() shal_run9  = [ $walk10, shal_run10 ] { ai_run(5); };
void() shal_run10 = [ $walk11, shal_run11 ] { ai_run(0); };
void() shal_run11 = [ $walk12, shal_run12 ] { ai_run(4); };
void() shal_run12 = [ $walk1,  shal_run1  ] { ai_run(5); };

void() shal_attack1  = [ $attack1,  shal_attack2  ] { sound(self, CHAN_VOICE, "shalrath/attack.wav", 1, ATTN_NORM); ai_face(); };
void() shal_attack2  = [ $attack2,  shal_attack3  ] { ai_face(); };
void() shal_attack3  = [ $attack3,  shal_attack4  ] { ai_face(); };
void() shal_attack4  = [ $attack4,  shal_attack5  ] { ai_face(); };
void() shal_attack5  = [ $attack5,  shal_attack6  ] { ai_face(); };
void() shal_attack6  = [ $attack6,  shal_attack7  ] { ai_face(); };
void() shal_attack7  = [ $attack7,  shal_attack8  ] { ai_face(); };
void() shal_attack8  = [ $attack8,  shal_attack9  ] { ai_face(); };
void() shal_attack9  = [ $attack9,  shal_attack10 ] { ShalMissile(); };
void() shal_attack10 = [ $attack10, shal_attack11 ] { ai_face(); };
void() shal_attack11 = [ $attack11, shal_run1     ] {};

void() shal_pain1 = [ $pain1, shal_pain2 ] {};
void() shal_pain2 = [ $pain2, shal_pain3 ] {};
void() shal_pain3 = [ $pain3, shal_pain4 ] {};
void() shal_pain4 = [ $pain4, shal_pain5 ] {};
void() shal_pain5 = [ $pain5, shal_run1  ] {};

void() shal_death1 = [ $death1, shal_death2 ] {};
void() shal_death2 = [ $death2, shal_death3 ] {};
void() shal_death3 = [ $death3, shal_death4 ] {};
void() shal_death4 = [ $death4, shal_death5 ] {};
void() shal_death5 = [ $death5, shal_death6 ] {};
void() shal_death6 = [ $death6, shal_death7 ] {};
void() shal_death7 = [ $death7, shal_death7 ] {};

void(entity attacker, float damage) shalrath_pain =
{
    if (self.pain_finished > time)
    {
        return;
    }

    sound(self, CHAN_VOICE, "shalrath/pain.wav", 1, ATTN_NORM);
    shal_pain1();
    self.pain_finished = time + 3;
};


void() shalrath_die =
{
    // check for gib
    if (self.health < -90)
    {
        sound(self, CHAN_VOICE, "player/udeath.wav", 1, ATTN_NORM);
        ThrowHead("progs/h_shal.mdl", self.health);
        ThrowGib("progs/gib1.mdl", self.health);
        ThrowGib("progs/gib2.mdl", self.health);
        ThrowGib("progs/gib3.mdl", self.health);
        return;
    }

    sound(self, CHAN_VOICE, "shalrath/death.wav", 1, ATTN_NORM);
    shal_death1();
    self.solid = SOLID_NOT;
    // insert death sounds here
};


/*
================
ShalMissile
================
*/
void() ShalMissileTouch;
void() ShalHome;

void() ShalMissile =
{
    local entity missile;
    local vector dir;
    local float dist, flytime;

    dir = normalize((self.enemy.origin + '0 0 10') - self.origin);
    dist = vlen(self.enemy.origin - self.origin);
    flytime = dist * 0.002;
    if (flytime < 0.1)
    {
        flytime = 0.1;
    }

    self.effects = self.effects | EF_MUZZLEFLASH;
    sound(self, CHAN_WEAPON, "shalrath/attack2.wav", 1, ATTN_NORM);

    missile = spawn();
    missile.owner = self;

    missile.solid = SOLID_BBOX;
    missile.movetype = MOVETYPE_FLYMISSILE;
    setmodel(missile, "progs/v_spike.mdl");

    setsize(missile, '0 0 0', '0 0 0');

    missile.origin = self.origin + '0 0 10';
    missile.velocity = dir * 400;
    missile.avelocity = '300 300 300';
    missile.nextthink = flytime + time;
    missile.think = ShalHome;
    missile.enemy = self.enemy;
    missile.touch = ShalMissileTouch;
};


void() ShalHome =
{
    local vector dir, vtemp;

    vtemp = self.enemy.origin + '0 0 10';

    if (self.enemy.health < 1)
    {
        remove(self);
        return;
    }

    dir = normalize(vtemp - self.origin);
    if (skill == 3)
    {
        self.velocity = dir * 350;
    }
    else
    {
        self.velocity = dir * 250;
    }

    self.nextthink = time + 0.2;
    self.think = ShalHome;
};


void() ShalMissileTouch =
{
    if (other == self.owner)
    {
        return; // don't explode on owner
    }

    if (other.classname == "monster_zombie")
    {
        T_Damage(other, self, self, 110);
    }
    T_RadiusDamage(self, self.owner, 40, world);
    sound(self, CHAN_WEAPON, "weapons/r_exp3.wav", 1, ATTN_NORM);

    WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
    WriteByte(MSG_BROADCAST, TE_EXPLOSION);
    WriteCoord(MSG_BROADCAST, self.origin_x);
    WriteCoord(MSG_BROADCAST, self.origin_y);
    WriteCoord(MSG_BROADCAST, self.origin_z);

    self.velocity = '0 0 0';
    self.touch = SUB_Null;
    setmodel(self, "progs/s_explod.spr");
    self.solid = SOLID_NOT;
    s_explode1();
};


//=================================================================

/*QUAKED monster_shalrath (1 0 0) (-32 -32 -24) (32 32 48) Ambush
*/
void() monster_shalrath =
{
    if (deathmatch)
    {
        remove(self);
        return;
    }

    precache_model2("progs/shalrath.mdl");
    precache_model2("progs/h_shal.mdl");
    precache_model2("progs/v_spike.mdl");

    precache_sound2("shalrath/attack.wav");
    precache_sound2("shalrath/attack2.wav");
    precache_sound2("shalrath/death.wav");
    precache_sound2("shalrath/idle.wav");
    precache_sound2("shalrath/pain.wav");
    precache_sound2("shalrath/sight.wav");

    self.solid = SOLID_SLIDEBOX;
    self.movetype = MOVETYPE_STEP;

    setmodel(self, "progs/shalrath.mdl");
    setsize(self, VEC_HULL2_MIN, VEC_HULL2_MAX);
    self.health = 400;

    self.th_stand = shal_stand;
    self.th_walk = shal_walk1;
    self.th_run = shal_run1;
    self.th_die = shalrath_die;
    self.th_pain = shalrath_pain;
    self.th_missile = shal_attack1;

    self.think = walkmonster_start;
    self.nextthink = time + 0.1 + random() * 0.1;
};
