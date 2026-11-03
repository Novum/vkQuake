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

ZOMBIE

==============================================================================
*/

$cd id1/models/zombie

$origin 0 0 24

$base base
$skin skin

$frame stand1 stand2 stand3 stand4 stand5 stand6 stand7 stand8
$frame stand9 stand10 stand11 stand12 stand13 stand14 stand15

$frame walk1 walk2 walk3 walk4 walk5 walk6 walk7 walk8 walk9 walk10 walk11
$frame walk12 walk13 walk14 walk15 walk16 walk17 walk18 walk19

$frame run1 run2 run3 run4 run5 run6 run7 run8 run9 run10 run11 run12
$frame run13 run14 run15 run16 run17 run18

$frame atta1 atta2 atta3 atta4 atta5 atta6 atta7 atta8 atta9 atta10 atta11
$frame atta12 atta13

$frame attb1 attb2 attb3 attb4 attb5 attb6 attb7 attb8 attb9 attb10 attb11
$frame attb12 attb13 attb14

$frame attc1 attc2 attc3 attc4 attc5 attc6 attc7 attc8 attc9 attc10 attc11
$frame attc12

$frame paina1 paina2 paina3 paina4 paina5 paina6 paina7 paina8 paina9 paina10
$frame paina11 paina12

$frame painb1 painb2 painb3 painb4 painb5 painb6 painb7 painb8 painb9 painb10
$frame painb11 painb12 painb13 painb14 painb15 painb16 painb17 painb18 painb19
$frame painb20 painb21 painb22 painb23 painb24 painb25 painb26 painb27 painb28

$frame painc1 painc2 painc3 painc4 painc5 painc6 painc7 painc8 painc9 painc10
$frame painc11 painc12 painc13 painc14 painc15 painc16 painc17 painc18

$frame paind1 paind2 paind3 paind4 paind5 paind6 paind7 paind8 paind9 paind10
$frame paind11 paind12 paind13

$frame paine1 paine2 paine3 paine4 paine5 paine6 paine7 paine8 paine9 paine10
$frame paine11 paine12 paine13 paine14 paine15 paine16 paine17 paine18 paine19
$frame paine20 paine21 paine22 paine23 paine24 paine25 paine26 paine27 paine28
$frame paine29 paine30

$frame cruc_1 cruc_2 cruc_3 cruc_4 cruc_5 cruc_6


float SPAWN_CRUCIFIED = 1;

//=============================================================================

.float inpain;

void() zombie_stand1  = [ $stand1,  zombie_stand2  ] { ai_stand(); };
void() zombie_stand2  = [ $stand2,  zombie_stand3  ] { ai_stand(); };
void() zombie_stand3  = [ $stand3,  zombie_stand4  ] { ai_stand(); };
void() zombie_stand4  = [ $stand4,  zombie_stand5  ] { ai_stand(); };
void() zombie_stand5  = [ $stand5,  zombie_stand6  ] { ai_stand(); };
void() zombie_stand6  = [ $stand6,  zombie_stand7  ] { ai_stand(); };
void() zombie_stand7  = [ $stand7,  zombie_stand8  ] { ai_stand(); };
void() zombie_stand8  = [ $stand8,  zombie_stand9  ] { ai_stand(); };
void() zombie_stand9  = [ $stand9,  zombie_stand10 ] { ai_stand(); };
void() zombie_stand10 = [ $stand10, zombie_stand11 ] { ai_stand(); };
void() zombie_stand11 = [ $stand11, zombie_stand12 ] { ai_stand(); };
void() zombie_stand12 = [ $stand12, zombie_stand13 ] { ai_stand(); };
void() zombie_stand13 = [ $stand13, zombie_stand14 ] { ai_stand(); };
void() zombie_stand14 = [ $stand14, zombie_stand15 ] { ai_stand(); };
void() zombie_stand15 = [ $stand15, zombie_stand1  ] { ai_stand(); };

void() zombie_cruc1 = [ $cruc_1, zombie_cruc2 ] {
    if (random() < 0.1)
    {
        sound(self, CHAN_VOICE, "zombie/idle_w2.wav", 1, ATTN_STATIC);
    }
};
void() zombie_cruc2 = [ $cruc_2, zombie_cruc3 ] { self.nextthink = time + 0.1 + random() * 0.1; };
void() zombie_cruc3 = [ $cruc_3, zombie_cruc4 ] { self.nextthink = time + 0.1 + random() * 0.1; };
void() zombie_cruc4 = [ $cruc_4, zombie_cruc5 ] { self.nextthink = time + 0.1 + random() * 0.1; };
void() zombie_cruc5 = [ $cruc_5, zombie_cruc6 ] { self.nextthink = time + 0.1 + random() * 0.1; };
void() zombie_cruc6 = [ $cruc_6, zombie_cruc1 ] { self.nextthink = time + 0.1 + random() * 0.1; };

void() zombie_walk1  = [ $walk1,  zombie_walk2  ] { ai_walk(0); };
void() zombie_walk2  = [ $walk2,  zombie_walk3  ] { ai_walk(2); };
void() zombie_walk3  = [ $walk3,  zombie_walk4  ] { ai_walk(3); };
void() zombie_walk4  = [ $walk4,  zombie_walk5  ] { ai_walk(2); };
void() zombie_walk5  = [ $walk5,  zombie_walk6  ] { ai_walk(1); };
void() zombie_walk6  = [ $walk6,  zombie_walk7  ] { ai_walk(0); };
void() zombie_walk7  = [ $walk7,  zombie_walk8  ] { ai_walk(0); };
void() zombie_walk8  = [ $walk8,  zombie_walk9  ] { ai_walk(0); };
void() zombie_walk9  = [ $walk9,  zombie_walk10 ] { ai_walk(0); };
void() zombie_walk10 = [ $walk10, zombie_walk11 ] { ai_walk(0); };
void() zombie_walk11 = [ $walk11, zombie_walk12 ] { ai_walk(2); };
void() zombie_walk12 = [ $walk12, zombie_walk13 ] { ai_walk(2); };
void() zombie_walk13 = [ $walk13, zombie_walk14 ] { ai_walk(1); };
void() zombie_walk14 = [ $walk14, zombie_walk15 ] { ai_walk(0); };
void() zombie_walk15 = [ $walk15, zombie_walk16 ] { ai_walk(0); };
void() zombie_walk16 = [ $walk16, zombie_walk17 ] { ai_walk(0); };
void() zombie_walk17 = [ $walk17, zombie_walk18 ] { ai_walk(0); };
void() zombie_walk18 = [ $walk18, zombie_walk19 ] { ai_walk(0); };
void() zombie_walk19 = [ $walk19, zombie_walk1  ] {
    ai_walk(0);
    if (random() < 0.2)
    {
        sound(self, CHAN_VOICE, "zombie/z_idle.wav", 1, ATTN_IDLE);
    }
};

void() zombie_run1  = [ $run1,  zombie_run2  ] { ai_run(1); self.inpain = 0; };
void() zombie_run2  = [ $run2,  zombie_run3  ] { ai_run(1); };
void() zombie_run3  = [ $run3,  zombie_run4  ] { ai_run(0); };
void() zombie_run4  = [ $run4,  zombie_run5  ] { ai_run(1); };
void() zombie_run5  = [ $run5,  zombie_run6  ] { ai_run(2); };
void() zombie_run6  = [ $run6,  zombie_run7  ] { ai_run(3); };
void() zombie_run7  = [ $run7,  zombie_run8  ] { ai_run(4); };
void() zombie_run8  = [ $run8,  zombie_run9  ] { ai_run(4); };
void() zombie_run9  = [ $run9,  zombie_run10 ] { ai_run(2); };
void() zombie_run10 = [ $run10, zombie_run11 ] { ai_run(0); };
void() zombie_run11 = [ $run11, zombie_run12 ] { ai_run(0); };
void() zombie_run12 = [ $run12, zombie_run13 ] { ai_run(0); };
void() zombie_run13 = [ $run13, zombie_run14 ] { ai_run(2); };
void() zombie_run14 = [ $run14, zombie_run15 ] { ai_run(4); };
void() zombie_run15 = [ $run15, zombie_run16 ] { ai_run(6); };
void() zombie_run16 = [ $run16, zombie_run17 ] { ai_run(7); };
void() zombie_run17 = [ $run17, zombie_run18 ] { ai_run(3); };
void() zombie_run18 = [ $run18, zombie_run1  ] {
    ai_run(8);
    if (random() < 0.2)
    {
        sound(self, CHAN_VOICE, "zombie/z_idle.wav", 1, ATTN_IDLE);
    }
    else if (random() > 0.8)
    {
        sound(self, CHAN_VOICE, "zombie/z_idle1.wav", 1, ATTN_IDLE);
    }
};

/*
=============================================================================

ATTACKS

=============================================================================
*/

void() ZombieGrenadeTouch =
{
    if (other == self.owner)
    {
        return; // don't explode on owner
    }

    if (other.takedamage)
    {
        T_Damage(other, self, self.owner, 10);
        sound(self, CHAN_WEAPON, "zombie/z_hit.wav", 1, ATTN_NORM);
        remove(self);
        return;
    }

    sound(self, CHAN_WEAPON, "zombie/z_miss.wav", 1, ATTN_NORM); // bounce sound

    self.velocity = '0 0 0';
    self.avelocity = '0 0 0';
    self.touch = SUB_Remove;
};


/*
================
ZombieFireGrenade
================
*/
void(vector st) ZombieFireGrenade =
{
    local entity missile;
    local vector org;

    sound(self, CHAN_WEAPON, "zombie/z_shot1.wav", 1, ATTN_NORM);

    missile = spawn();
    missile.owner = self;
    missile.movetype = MOVETYPE_BOUNCE;
    missile.solid = SOLID_BBOX;

    // calc org
    org = self.origin + st_x * v_forward + st_y * v_right + (st_z - 24) * v_up;

    // set missile speed
    makevectors(self.angles);

    missile.velocity = normalize(self.enemy.origin - org);
    missile.velocity = missile.velocity * 600;
    missile.velocity_z = 200;

    missile.avelocity = '3000 1000 2000';

    missile.touch = ZombieGrenadeTouch;

    // set missile duration
    missile.nextthink = time + 2.5;
    missile.think = SUB_Remove;

    setmodel(missile, "progs/zom_gib.mdl");
    setsize(missile, '0 0 0', '0 0 0');
    setorigin(missile, org);
};


void() zombie_atta1  = [ $atta1,  zombie_atta2  ] { ai_face(); };
void() zombie_atta2  = [ $atta2,  zombie_atta3  ] { ai_face(); };
void() zombie_atta3  = [ $atta3,  zombie_atta4  ] { ai_face(); };
void() zombie_atta4  = [ $atta4,  zombie_atta5  ] { ai_face(); };
void() zombie_atta5  = [ $atta5,  zombie_atta6  ] { ai_face(); };
void() zombie_atta6  = [ $atta6,  zombie_atta7  ] { ai_face(); };
void() zombie_atta7  = [ $atta7,  zombie_atta8  ] { ai_face(); };
void() zombie_atta8  = [ $atta8,  zombie_atta9  ] { ai_face(); };
void() zombie_atta9  = [ $atta9,  zombie_atta10 ] { ai_face(); };
void() zombie_atta10 = [ $atta10, zombie_atta11 ] { ai_face(); };
void() zombie_atta11 = [ $atta11, zombie_atta12 ] { ai_face(); };
void() zombie_atta12 = [ $atta12, zombie_atta13 ] { ai_face(); };
void() zombie_atta13 = [ $atta13, zombie_run1   ] { ai_face(); ZombieFireGrenade('-10 -22 30'); };

void() zombie_attb1  = [ $attb1,  zombie_attb2  ] { ai_face(); };
void() zombie_attb2  = [ $attb2,  zombie_attb3  ] { ai_face(); };
void() zombie_attb3  = [ $attb3,  zombie_attb4  ] { ai_face(); };
void() zombie_attb4  = [ $attb4,  zombie_attb5  ] { ai_face(); };
void() zombie_attb5  = [ $attb5,  zombie_attb6  ] { ai_face(); };
void() zombie_attb6  = [ $attb6,  zombie_attb7  ] { ai_face(); };
void() zombie_attb7  = [ $attb7,  zombie_attb8  ] { ai_face(); };
void() zombie_attb8  = [ $attb8,  zombie_attb9  ] { ai_face(); };
void() zombie_attb9  = [ $attb9,  zombie_attb10 ] { ai_face(); };
void() zombie_attb10 = [ $attb10, zombie_attb11 ] { ai_face(); };
void() zombie_attb11 = [ $attb11, zombie_attb12 ] { ai_face(); };
void() zombie_attb12 = [ $attb12, zombie_attb13 ] { ai_face(); };
void() zombie_attb13 = [ $attb13, zombie_attb14 ] { ai_face(); };
void() zombie_attb14 = [ $attb13, zombie_run1   ] { ai_face(); ZombieFireGrenade('-10 -24 29'); };

void() zombie_attc1  = [ $attc1,  zombie_attc2  ] { ai_face(); };
void() zombie_attc2  = [ $attc2,  zombie_attc3  ] { ai_face(); };
void() zombie_attc3  = [ $attc3,  zombie_attc4  ] { ai_face(); };
void() zombie_attc4  = [ $attc4,  zombie_attc5  ] { ai_face(); };
void() zombie_attc5  = [ $attc5,  zombie_attc6  ] { ai_face(); };
void() zombie_attc6  = [ $attc6,  zombie_attc7  ] { ai_face(); };
void() zombie_attc7  = [ $attc7,  zombie_attc8  ] { ai_face(); };
void() zombie_attc8  = [ $attc8,  zombie_attc9  ] { ai_face(); };
void() zombie_attc9  = [ $attc9,  zombie_attc10 ] { ai_face(); };
void() zombie_attc10 = [ $attc10, zombie_attc11 ] { ai_face(); };
void() zombie_attc11 = [ $attc11, zombie_attc12 ] { ai_face(); };
void() zombie_attc12 = [ $attc12, zombie_run1   ] { ai_face(); ZombieFireGrenade('-12 -19 29'); };

void() zombie_missile =
{
    local float r;

    r = random();

    if (r < 0.3)
    {
        zombie_atta1();
    }
    else if (r < 0.6)
    {
        zombie_attb1();
    }
    else
    {
        zombie_attc1();
    }
};


/*
=============================================================================

PAIN

=============================================================================
*/

void() zombie_paina1  = [ $paina1,  zombie_paina2  ] { sound(self, CHAN_VOICE, "zombie/z_pain.wav", 1, ATTN_NORM); };
void() zombie_paina2  = [ $paina2,  zombie_paina3  ] { ai_painforward(3); };
void() zombie_paina3  = [ $paina3,  zombie_paina4  ] { ai_painforward(1); };
void() zombie_paina4  = [ $paina4,  zombie_paina5  ] { ai_pain(1); };
void() zombie_paina5  = [ $paina5,  zombie_paina6  ] { ai_pain(3); };
void() zombie_paina6  = [ $paina6,  zombie_paina7  ] { ai_pain(1); };
void() zombie_paina7  = [ $paina7,  zombie_paina8  ] {};
void() zombie_paina8  = [ $paina8,  zombie_paina9  ] {};
void() zombie_paina9  = [ $paina9,  zombie_paina10 ] {};
void() zombie_paina10 = [ $paina10, zombie_paina11 ] {};
void() zombie_paina11 = [ $paina11, zombie_paina12 ] {};
void() zombie_paina12 = [ $paina12, zombie_run1    ] {};

void() zombie_painb1  = [ $painb1,  zombie_painb2  ] { sound(self, CHAN_VOICE, "zombie/z_pain1.wav", 1, ATTN_NORM); };
void() zombie_painb2  = [ $painb2,  zombie_painb3  ] { ai_pain(2); };
void() zombie_painb3  = [ $painb3,  zombie_painb4  ] { ai_pain(8); };
void() zombie_painb4  = [ $painb4,  zombie_painb5  ] { ai_pain(6); };
void() zombie_painb5  = [ $painb5,  zombie_painb6  ] { ai_pain(2); };
void() zombie_painb6  = [ $painb6,  zombie_painb7  ] {};
void() zombie_painb7  = [ $painb7,  zombie_painb8  ] {};
void() zombie_painb8  = [ $painb8,  zombie_painb9  ] {};
void() zombie_painb9  = [ $painb9,  zombie_painb10 ] { sound(self, CHAN_BODY, "zombie/z_fall.wav", 1, ATTN_NORM); };
void() zombie_painb10 = [ $painb10, zombie_painb11 ] {};
void() zombie_painb11 = [ $painb11, zombie_painb12 ] {};
void() zombie_painb12 = [ $painb12, zombie_painb13 ] {};
void() zombie_painb13 = [ $painb13, zombie_painb14 ] {};
void() zombie_painb14 = [ $painb14, zombie_painb15 ] {};
void() zombie_painb15 = [ $painb15, zombie_painb16 ] {};
void() zombie_painb16 = [ $painb16, zombie_painb17 ] {};
void() zombie_painb17 = [ $painb17, zombie_painb18 ] {};
void() zombie_painb18 = [ $painb18, zombie_painb19 ] {};
void() zombie_painb19 = [ $painb19, zombie_painb20 ] {};
void() zombie_painb20 = [ $painb20, zombie_painb21 ] {};
void() zombie_painb21 = [ $painb21, zombie_painb22 ] {};
void() zombie_painb22 = [ $painb22, zombie_painb23 ] {};
void() zombie_painb23 = [ $painb23, zombie_painb24 ] {};
void() zombie_painb24 = [ $painb24, zombie_painb25 ] {};
void() zombie_painb25 = [ $painb25, zombie_painb26 ] { ai_painforward(1); };
void() zombie_painb26 = [ $painb26, zombie_painb27 ] {};
void() zombie_painb27 = [ $painb27, zombie_painb28 ] {};
void() zombie_painb28 = [ $painb28, zombie_run1    ] {};

void() zombie_painc1  = [ $painc1,  zombie_painc2  ] { sound(self, CHAN_VOICE, "zombie/z_pain1.wav", 1, ATTN_NORM); };
void() zombie_painc2  = [ $painc2,  zombie_painc3  ] {};
void() zombie_painc3  = [ $painc3,  zombie_painc4  ] { ai_pain(3); };
void() zombie_painc4  = [ $painc4,  zombie_painc5  ] { ai_pain(1); };
void() zombie_painc5  = [ $painc5,  zombie_painc6  ] {};
void() zombie_painc6  = [ $painc6,  zombie_painc7  ] {};
void() zombie_painc7  = [ $painc7,  zombie_painc8  ] {};
void() zombie_painc8  = [ $painc8,  zombie_painc9  ] {};
void() zombie_painc9  = [ $painc9,  zombie_painc10 ] {};
void() zombie_painc10 = [ $painc10, zombie_painc11 ] {};
void() zombie_painc11 = [ $painc11, zombie_painc12 ] { ai_painforward(1); };
void() zombie_painc12 = [ $painc12, zombie_painc13 ] { ai_painforward(1); };
void() zombie_painc13 = [ $painc13, zombie_painc14 ] {};
void() zombie_painc14 = [ $painc14, zombie_painc15 ] {};
void() zombie_painc15 = [ $painc15, zombie_painc16 ] {};
void() zombie_painc16 = [ $painc16, zombie_painc17 ] {};
void() zombie_painc17 = [ $painc17, zombie_painc18 ] {};
void() zombie_painc18 = [ $painc18, zombie_run1    ] {};

void() zombie_paind1  = [ $paind1,  zombie_paind2  ] { sound(self, CHAN_VOICE, "zombie/z_pain.wav", 1, ATTN_NORM); };
void() zombie_paind2  = [ $paind2,  zombie_paind3  ] {};
void() zombie_paind3  = [ $paind3,  zombie_paind4  ] {};
void() zombie_paind4  = [ $paind4,  zombie_paind5  ] {};
void() zombie_paind5  = [ $paind5,  zombie_paind6  ] {};
void() zombie_paind6  = [ $paind6,  zombie_paind7  ] {};
void() zombie_paind7  = [ $paind7,  zombie_paind8  ] {};
void() zombie_paind8  = [ $paind8,  zombie_paind9  ] {};
void() zombie_paind9  = [ $paind9,  zombie_paind10 ] { ai_pain(1); };
void() zombie_paind10 = [ $paind10, zombie_paind11 ] {};
void() zombie_paind11 = [ $paind11, zombie_paind12 ] {};
void() zombie_paind12 = [ $paind12, zombie_paind13 ] {};
void() zombie_paind13 = [ $paind13, zombie_run1    ] {};

void() zombie_paine1  = [ $paine1,  zombie_paine2  ] { sound(self, CHAN_VOICE, "zombie/z_pain.wav", 1, ATTN_NORM); self.health = 60; };
void() zombie_paine2  = [ $paine2,  zombie_paine3  ] { ai_pain(8); };
void() zombie_paine3  = [ $paine3,  zombie_paine4  ] { ai_pain(5); };
void() zombie_paine4  = [ $paine4,  zombie_paine5  ] { ai_pain(3); };
void() zombie_paine5  = [ $paine5,  zombie_paine6  ] { ai_pain(1); };
void() zombie_paine6  = [ $paine6,  zombie_paine7  ] { ai_pain(2); };
void() zombie_paine7  = [ $paine7,  zombie_paine8  ] { ai_pain(1); };
void() zombie_paine8  = [ $paine8,  zombie_paine9  ] { ai_pain(1); };
void() zombie_paine9  = [ $paine9,  zombie_paine10 ] { ai_pain(2); };
void() zombie_paine10 = [ $paine10, zombie_paine11 ] { sound(self, CHAN_BODY, "zombie/z_fall.wav", 1, ATTN_NORM); self.solid = SOLID_NOT; };
void() zombie_paine11 = [ $paine11, zombie_paine12 ] { self.nextthink = self.nextthink + 5; self.health = 60; };
void() zombie_paine12 = [ $paine12, zombie_paine13 ] {
    // see if ok to stand up
    self.health = 60;
    sound(self, CHAN_VOICE, "zombie/z_idle.wav", 1, ATTN_IDLE);
    self.solid = SOLID_SLIDEBOX;
    if (!walkmove(0, 0))
    {
        self.think = zombie_paine11;
        self.solid = SOLID_NOT;
        return;
    }
};
void() zombie_paine13 = [ $paine13, zombie_paine14 ] {};
void() zombie_paine14 = [ $paine14, zombie_paine15 ] {};
void() zombie_paine15 = [ $paine15, zombie_paine16 ] {};
void() zombie_paine16 = [ $paine16, zombie_paine17 ] {};
void() zombie_paine17 = [ $paine17, zombie_paine18 ] {};
void() zombie_paine18 = [ $paine18, zombie_paine19 ] {};
void() zombie_paine19 = [ $paine19, zombie_paine20 ] {};
void() zombie_paine20 = [ $paine20, zombie_paine21 ] {};
void() zombie_paine21 = [ $paine21, zombie_paine22 ] {};
void() zombie_paine22 = [ $paine22, zombie_paine23 ] {};
void() zombie_paine23 = [ $paine23, zombie_paine24 ] {};
void() zombie_paine24 = [ $paine24, zombie_paine25 ] {};
void() zombie_paine25 = [ $paine25, zombie_paine26 ] { ai_painforward(5); };
void() zombie_paine26 = [ $paine26, zombie_paine27 ] { ai_painforward(3); };
void() zombie_paine27 = [ $paine27, zombie_paine28 ] { ai_painforward(1); };
void() zombie_paine28 = [ $paine28, zombie_paine29 ] { ai_pain(1); };
void() zombie_paine29 = [ $paine29, zombie_paine30 ] {};
void() zombie_paine30 = [ $paine30, zombie_run1    ] {};

void() zombie_die =
{
    sound(self, CHAN_VOICE, "zombie/z_gib.wav", 1, ATTN_NORM);
    ThrowHead("progs/h_zombie.mdl", self.health);
    ThrowGib("progs/gib1.mdl", self.health);
    ThrowGib("progs/gib2.mdl", self.health);
    ThrowGib("progs/gib3.mdl", self.health);
};


/*
=================
zombie_pain

Zombies can only be killed (gibbed) by doing 60 hit points of damage
in a single frame (rockets, grenades, quad shotgun, quad nailgun).

A hit of 25 points or more (super shotgun, quad nailgun) will always put it
down to the ground.

A hit of from 10 to 40 points in one frame will cause it to go down if it
has been twice in two seconds, otherwise it goes into one of the four
fast pain frames.

A hit of less than 10 points of damage (winged by a shotgun) will be ignored.

FIXME: don't use pain_finished because of nightmare hack
=================
*/
void(entity attacker, float damage) zombie_pain =
{
    local float r;

    self.health = 60; // always reset health

    if (damage < 9)
    {
        return; // totally ignore
    }

    if (self.inpain == 2)
    {
        return; // down on ground, so don't reset any counters
    }

    // go down immediately if a big enough hit
    if (damage >= 25)
    {
        self.inpain = 2;
        zombie_paine1();
        return;
    }

    if (self.inpain)
    {
        // if hit again in next three seconds while not in pain frames, definitely drop
        self.pain_finished = time + 3;
        return; // currently going through an animation, don't change
    }

    if (self.pain_finished > time)
    {
        // hit again, so drop down
        self.inpain = 2;
        zombie_paine1();
        return;
    }

    // go into one of the fast pain animations
    self.inpain = 1;

    r = random();
    if (r < 0.25)
    {
        zombie_paina1();
    }
    else if (r < 0.5)
    {
        zombie_painb1();
    }
    else if (r < 0.75)
    {
        zombie_painc1();
    }
    else
    {
        zombie_paind1();
    }
};


//============================================================================

/*QUAKED monster_zombie (1 0 0) (-16 -16 -24) (16 16 32) Crucified ambush

If crucified, stick the bounding box 12 pixels back into a wall to look right.
*/
void() monster_zombie =
{
    if (deathmatch)
    {
        remove(self);
        return;
    }

    precache_model("progs/zombie.mdl");
    precache_model("progs/h_zombie.mdl");
    precache_model("progs/zom_gib.mdl");

    precache_sound("zombie/z_idle.wav");
    precache_sound("zombie/z_idle1.wav");
    precache_sound("zombie/z_shot1.wav");
    precache_sound("zombie/z_gib.wav");
    precache_sound("zombie/z_pain.wav");
    precache_sound("zombie/z_pain1.wav");
    precache_sound("zombie/z_fall.wav");
    precache_sound("zombie/z_miss.wav");
    precache_sound("zombie/z_hit.wav");
    precache_sound("zombie/idle_w2.wav");

    self.solid = SOLID_SLIDEBOX;
    self.movetype = MOVETYPE_STEP;

    setmodel(self, "progs/zombie.mdl");

    setsize(self, '-16 -16 -24', '16 16 40');
    self.health = 60;

    self.th_stand = zombie_stand1;
    self.th_walk = zombie_walk1;
    self.th_run = zombie_run1;
    self.th_pain = zombie_pain;
    self.th_die = zombie_die;
    self.th_missile = zombie_missile;

    if ((self.spawnflags & SPAWN_CRUCIFIED))
    {
        self.movetype = MOVETYPE_NONE;
        zombie_cruc1();
    }
    else
    {
        walkmonster_start();
    }
};
