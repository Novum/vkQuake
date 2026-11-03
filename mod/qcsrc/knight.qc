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

KNIGHT

==============================================================================
*/

$cd id1/models/knight
$origin 0 0 24
$base base
$skin badass3

$frame stand1 stand2 stand3 stand4 stand5 stand6 stand7 stand8 stand9

$frame runb1 runb2 runb3 runb4 runb5 runb6 runb7 runb8

//frame runc1 runc2 runc3 runc4 runc5 runc6

$frame runattack1 runattack2 runattack3 runattack4 runattack5
$frame runattack6 runattack7 runattack8 runattack9 runattack10
$frame runattack11

$frame pain1 pain2 pain3

$frame painb1 painb2 painb3 painb4 painb5 painb6 painb7 painb8 painb9
$frame painb10 painb11

//frame attack1 attack2 attack3 attack4 attack5 attack6 attack7
//frame attack8 attack9 attack10 attack11

$frame attackb1 attackb1 attackb2 attackb3 attackb4 attackb5
$frame attackb6 attackb7 attackb8 attackb9 attackb10

$frame walk1 walk2 walk3 walk4 walk5 walk6 walk7 walk8 walk9
$frame walk10 walk11 walk12 walk13 walk14

$frame kneel1 kneel2 kneel3 kneel4 kneel5

$frame standing2 standing3 standing4 standing5

$frame death1 death2 death3 death4 death5 death6 death7 death8
$frame death9 death10

$frame deathb1 deathb2 deathb3 deathb4 deathb5 deathb6 deathb7 deathb8
$frame deathb9 deathb10 deathb11


void() knight_stand1 = [ $stand1, knight_stand2 ] { ai_stand(); };
void() knight_stand2 = [ $stand2, knight_stand3 ] { ai_stand(); };
void() knight_stand3 = [ $stand3, knight_stand4 ] { ai_stand(); };
void() knight_stand4 = [ $stand4, knight_stand5 ] { ai_stand(); };
void() knight_stand5 = [ $stand5, knight_stand6 ] { ai_stand(); };
void() knight_stand6 = [ $stand6, knight_stand7 ] { ai_stand(); };
void() knight_stand7 = [ $stand7, knight_stand8 ] { ai_stand(); };
void() knight_stand8 = [ $stand8, knight_stand9 ] { ai_stand(); };
void() knight_stand9 = [ $stand9, knight_stand1 ] { ai_stand(); };

void() knight_walk1  = [ $walk1,  knight_walk2  ] {
    if (random() < 0.2)
    {
        sound(self, CHAN_VOICE, "knight/idle.wav", 1, ATTN_IDLE);
    }
    ai_walk(3);
};
void() knight_walk2  = [ $walk2,  knight_walk3  ] { ai_walk(2); };
void() knight_walk3  = [ $walk3,  knight_walk4  ] { ai_walk(3); };
void() knight_walk4  = [ $walk4,  knight_walk5  ] { ai_walk(4); };
void() knight_walk5  = [ $walk5,  knight_walk6  ] { ai_walk(3); };
void() knight_walk6  = [ $walk6,  knight_walk7  ] { ai_walk(3); };
void() knight_walk7  = [ $walk7,  knight_walk8  ] { ai_walk(3); };
void() knight_walk8  = [ $walk8,  knight_walk9  ] { ai_walk(4); };
void() knight_walk9  = [ $walk9,  knight_walk10 ] { ai_walk(3); };
void() knight_walk10 = [ $walk10, knight_walk11 ] { ai_walk(3); };
void() knight_walk11 = [ $walk11, knight_walk12 ] { ai_walk(2); };
void() knight_walk12 = [ $walk12, knight_walk13 ] { ai_walk(3); };
void() knight_walk13 = [ $walk13, knight_walk14 ] { ai_walk(4); };
void() knight_walk14 = [ $walk14, knight_walk1  ] { ai_walk(3); };

void() knight_run1 = [ $runb1, knight_run2 ] {
    if (random() < 0.2)
    {
        sound(self, CHAN_VOICE, "knight/idle.wav", 1, ATTN_IDLE);
    }
    ai_run(16);
};
void() knight_run2 = [ $runb2, knight_run3 ] { ai_run(20); };
void() knight_run3 = [ $runb3, knight_run4 ] { ai_run(13); };
void() knight_run4 = [ $runb4, knight_run5 ] { ai_run(7); };
void() knight_run5 = [ $runb5, knight_run6 ] { ai_run(16); };
void() knight_run6 = [ $runb6, knight_run7 ] { ai_run(20); };
void() knight_run7 = [ $runb7, knight_run8 ] { ai_run(14); };
void() knight_run8 = [ $runb8, knight_run1 ] { ai_run(6); };

void() knight_runatk1  = [ $runattack1,  knight_runatk2  ] {
    if (random() > 0.5)
    {
        sound(self, CHAN_WEAPON, "knight/sword2.wav", 1, ATTN_NORM);
    }
    else
    {
        sound(self, CHAN_WEAPON, "knight/sword1.wav", 1, ATTN_NORM);
    }
    ai_charge(20);
};
void() knight_runatk2  = [ $runattack2,  knight_runatk3  ] { ai_charge_side(); };
void() knight_runatk3  = [ $runattack3,  knight_runatk4  ] { ai_charge_side(); };
void() knight_runatk4  = [ $runattack4,  knight_runatk5  ] { ai_charge_side(); };
void() knight_runatk5  = [ $runattack5,  knight_runatk6  ] { ai_melee_side(); };
void() knight_runatk6  = [ $runattack6,  knight_runatk7  ] { ai_melee_side(); };
void() knight_runatk7  = [ $runattack7,  knight_runatk8  ] { ai_melee_side(); };
void() knight_runatk8  = [ $runattack8,  knight_runatk9  ] { ai_melee_side(); };
void() knight_runatk9  = [ $runattack9,  knight_runatk10 ] { ai_melee_side(); };
void() knight_runatk10 = [ $runattack10, knight_runatk11 ] { ai_charge_side(); };
void() knight_runatk11 = [ $runattack11, knight_run1     ] { ai_charge(10); };

void() knight_atk1  = [ $attackb1,  knight_atk2  ] { sound(self, CHAN_WEAPON, "knight/sword1.wav", 1, ATTN_NORM); ai_charge(0); };
void() knight_atk2  = [ $attackb2,  knight_atk3  ] { ai_charge(7); };
void() knight_atk3  = [ $attackb3,  knight_atk4  ] { ai_charge(4); };
void() knight_atk4  = [ $attackb4,  knight_atk5  ] { ai_charge(0); };
void() knight_atk5  = [ $attackb5,  knight_atk6  ] { ai_charge(3); };
void() knight_atk6  = [ $attackb6,  knight_atk7  ] { ai_charge(4); ai_melee(); };
void() knight_atk7  = [ $attackb7,  knight_atk8  ] { ai_charge(1); ai_melee(); };
void() knight_atk8  = [ $attackb8,  knight_atk9  ] { ai_charge(3); ai_melee(); };
void() knight_atk9  = [ $attackb9,  knight_atk10 ] { ai_charge(1); };
void() knight_atk10 = [ $attackb10, knight_run1  ] { ai_charge(5); };

//void() knight_atk9  = [ $attack9,  knight_atk10 ] {};
//void() knight_atk10 = [ $attack10, knight_atk11 ] {};
//void() knight_atk11 = [ $attack11, knight_run1  ] {};

//===========================================================================

void() knight_pain1 = [ $pain1, knight_pain2 ] {};
void() knight_pain2 = [ $pain2, knight_pain3 ] {};
void() knight_pain3 = [ $pain3, knight_run1  ] {};

void() knight_painb1  = [ $painb1,  knight_painb2  ] { ai_painforward(0); };
void() knight_painb2  = [ $painb2,  knight_painb3  ] { ai_painforward(3); };
void() knight_painb3  = [ $painb3,  knight_painb4  ] {};
void() knight_painb4  = [ $painb4,  knight_painb5  ] {};
void() knight_painb5  = [ $painb5,  knight_painb6  ] { ai_painforward(2); };
void() knight_painb6  = [ $painb6,  knight_painb7  ] { ai_painforward(4); };
void() knight_painb7  = [ $painb7,  knight_painb8  ] { ai_painforward(2); };
void() knight_painb8  = [ $painb8,  knight_painb9  ] { ai_painforward(5); };
void() knight_painb9  = [ $painb9,  knight_painb10 ] { ai_painforward(5); };
void() knight_painb10 = [ $painb10, knight_painb11 ] { ai_painforward(0); };
void() knight_painb11 = [ $painb11, knight_run1    ] {};

void(entity attacker, float damage) knight_pain =
{
    local float r;

    if (self.pain_finished > time)
    {
        return;
    }

    r = random();

    sound(self, CHAN_VOICE, "knight/khurt.wav", 1, ATTN_NORM);
    if (r < 0.85)
    {
        knight_pain1();
        self.pain_finished = time + 1;
    }
    else
    {
        knight_painb1();
        self.pain_finished = time + 1;
    }
};


//===========================================================================

void() knight_bow1  = [ $kneel1, knight_bow2  ] { ai_turn(); };
void() knight_bow2  = [ $kneel2, knight_bow3  ] { ai_turn(); };
void() knight_bow3  = [ $kneel3, knight_bow4  ] { ai_turn(); };
void() knight_bow4  = [ $kneel4, knight_bow5  ] { ai_turn(); };
void() knight_bow5  = [ $kneel5, knight_bow5  ] { ai_turn(); };
void() knight_bow6  = [ $kneel4, knight_bow7  ] { ai_turn(); };
void() knight_bow7  = [ $kneel3, knight_bow8  ] { ai_turn(); };
void() knight_bow8  = [ $kneel2, knight_bow9  ] { ai_turn(); };
void() knight_bow9  = [ $kneel1, knight_bow10 ] { ai_turn(); };
void() knight_bow10 = [ $walk1,  knight_walk1 ] { ai_turn(); };

void() knight_die1  = [ $death1,  knight_die2  ] {};
void() knight_die2  = [ $death2,  knight_die3  ] {};
void() knight_die3  = [ $death3,  knight_die4  ] { self.solid = SOLID_NOT; };
void() knight_die4  = [ $death4,  knight_die5  ] {};
void() knight_die5  = [ $death5,  knight_die6  ] {};
void() knight_die6  = [ $death6,  knight_die7  ] {};
void() knight_die7  = [ $death7,  knight_die8  ] {};
void() knight_die8  = [ $death8,  knight_die9  ] {};
void() knight_die9  = [ $death9,  knight_die10 ] {};
void() knight_die10 = [ $death10, knight_die10 ] {};

void() knight_dieb1  = [ $deathb1,  knight_dieb2  ] {};
void() knight_dieb2  = [ $deathb2,  knight_dieb3  ] {};
void() knight_dieb3  = [ $deathb3,  knight_dieb4  ] { self.solid = SOLID_NOT; };
void() knight_dieb4  = [ $deathb4,  knight_dieb5  ] {};
void() knight_dieb5  = [ $deathb5,  knight_dieb6  ] {};
void() knight_dieb6  = [ $deathb6,  knight_dieb7  ] {};
void() knight_dieb7  = [ $deathb7,  knight_dieb8  ] {};
void() knight_dieb8  = [ $deathb8,  knight_dieb9  ] {};
void() knight_dieb9  = [ $deathb9,  knight_dieb10 ] {};
void() knight_dieb10 = [ $deathb10, knight_dieb11 ] {};
void() knight_dieb11 = [ $deathb11, knight_dieb11 ] {};

void() knight_die =
{
    // check for gib
    if (self.health < -40)
    {
        sound(self, CHAN_VOICE, "player/udeath.wav", 1, ATTN_NORM);
        ThrowHead("progs/h_knight.mdl", self.health);
        ThrowGib("progs/gib1.mdl", self.health);
        ThrowGib("progs/gib2.mdl", self.health);
        ThrowGib("progs/gib3.mdl", self.health);
        return;
    }

    // regular death
    sound(self, CHAN_VOICE, "knight/kdeath.wav", 1, ATTN_NORM);

    if (random() < 0.5)
    {
        knight_die1();
    }
    else
    {
        knight_dieb1();
    }
};


/*QUAKED monster_knight (1 0 0) (-16 -16 -24) (16 16 40) Ambush
*/
void() monster_knight =
{
    if (deathmatch)
    {
        remove(self);
        return;
    }

    precache_model("progs/knight.mdl");
    precache_model("progs/h_knight.mdl");

    precache_sound("knight/kdeath.wav");
    precache_sound("knight/khurt.wav");
    precache_sound("knight/ksight.wav");
    precache_sound("knight/sword1.wav");
    precache_sound("knight/sword2.wav");
    precache_sound("knight/idle.wav");

    self.solid = SOLID_SLIDEBOX;
    self.movetype = MOVETYPE_STEP;

    setmodel(self, "progs/knight.mdl");

    setsize(self, '-16 -16 -24', '16 16 40');
    self.health = 75;

    self.th_stand = knight_stand1;
    self.th_walk = knight_walk1;
    self.th_run = knight_run1;
    self.th_melee = knight_atk1;
    self.th_pain = knight_pain;
    self.th_die = knight_die;

    walkmonster_start();
};
