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

BOSS-ONE

==============================================================================
*/

$cd id1/models/boss1
$origin 0 0 -15
$base base
$skin skin
$scale 5

$frame rise1 rise2 rise3 rise4 rise5 rise6 rise7 rise8 rise9 rise10
$frame rise11 rise12 rise13 rise14 rise15 rise16 rise17

$frame walk1 walk2 walk3 walk4 walk5 walk6 walk7 walk8
$frame walk9 walk10 walk11 walk12 walk13 walk14 walk15
$frame walk16 walk17 walk18 walk19 walk20 walk21 walk22
$frame walk23 walk24 walk25 walk26 walk27 walk28 walk29 walk30 walk31

$frame death1 death2 death3 death4 death5 death6 death7 death8 death9

$frame attack1 attack2 attack3 attack4 attack5 attack6 attack7 attack8
$frame attack9 attack10 attack11 attack12 attack13 attack14 attack15
$frame attack16 attack17 attack18 attack19 attack20 attack21 attack22
$frame attack23

$frame shocka1 shocka2 shocka3 shocka4 shocka5 shocka6 shocka7 shocka8
$frame shocka9 shocka10

$frame shockb1 shockb2 shockb3 shockb4 shockb5 shockb6

$frame shockc1 shockc2 shockc3 shockc4 shockc5 shockc6 shockc7 shockc8
$frame shockc9 shockc10


void(vector p) boss_missile;

void() boss_face =
{
    // go for another player if multi player
    if (self.enemy.health <= 0 || random() < 0.02)
    {
        self.enemy = find(self.enemy, classname, "player");
        if (!self.enemy)
        {
            self.enemy = find(self.enemy, classname, "player");
        }
    }
    ai_face();
};


void() boss_rise1  = [ $rise1,  boss_rise2    ] { sound(self, CHAN_WEAPON, "boss1/out1.wav", 1, ATTN_NORM); };
void() boss_rise2  = [ $rise2,  boss_rise3    ] { sound(self, CHAN_VOICE, "boss1/sight1.wav", 1, ATTN_NORM); };
void() boss_rise3  = [ $rise3,  boss_rise4    ] {};
void() boss_rise4  = [ $rise4,  boss_rise5    ] {};
void() boss_rise5  = [ $rise5,  boss_rise6    ] {};
void() boss_rise6  = [ $rise6,  boss_rise7    ] {};
void() boss_rise7  = [ $rise7,  boss_rise8    ] {};
void() boss_rise8  = [ $rise8,  boss_rise9    ] {};
void() boss_rise9  = [ $rise9,  boss_rise10   ] {};
void() boss_rise10 = [ $rise10, boss_rise11   ] {};
void() boss_rise11 = [ $rise11, boss_rise12   ] {};
void() boss_rise12 = [ $rise12, boss_rise13   ] {};
void() boss_rise13 = [ $rise13, boss_rise14   ] {};
void() boss_rise14 = [ $rise14, boss_rise15   ] {};
void() boss_rise15 = [ $rise15, boss_rise16   ] {};
void() boss_rise16 = [ $rise16, boss_rise17   ] {};
void() boss_rise17 = [ $rise17, boss_missile1 ] {};

void() boss_idle1  = [ $walk1,  boss_idle2  ] {
    // look for other players
};
void() boss_idle2  = [ $walk2,  boss_idle3  ] { boss_face(); };
void() boss_idle3  = [ $walk3,  boss_idle4  ] { boss_face(); };
void() boss_idle4  = [ $walk4,  boss_idle5  ] { boss_face(); };
void() boss_idle5  = [ $walk5,  boss_idle6  ] { boss_face(); };
void() boss_idle6  = [ $walk6,  boss_idle7  ] { boss_face(); };
void() boss_idle7  = [ $walk7,  boss_idle8  ] { boss_face(); };
void() boss_idle8  = [ $walk8,  boss_idle9  ] { boss_face(); };
void() boss_idle9  = [ $walk9,  boss_idle10 ] { boss_face(); };
void() boss_idle10 = [ $walk10, boss_idle11 ] { boss_face(); };
void() boss_idle11 = [ $walk11, boss_idle12 ] { boss_face(); };
void() boss_idle12 = [ $walk12, boss_idle13 ] { boss_face(); };
void() boss_idle13 = [ $walk13, boss_idle14 ] { boss_face(); };
void() boss_idle14 = [ $walk14, boss_idle15 ] { boss_face(); };
void() boss_idle15 = [ $walk15, boss_idle16 ] { boss_face(); };
void() boss_idle16 = [ $walk16, boss_idle17 ] { boss_face(); };
void() boss_idle17 = [ $walk17, boss_idle18 ] { boss_face(); };
void() boss_idle18 = [ $walk18, boss_idle19 ] { boss_face(); };
void() boss_idle19 = [ $walk19, boss_idle20 ] { boss_face(); };
void() boss_idle20 = [ $walk20, boss_idle21 ] { boss_face(); };
void() boss_idle21 = [ $walk21, boss_idle22 ] { boss_face(); };
void() boss_idle22 = [ $walk22, boss_idle23 ] { boss_face(); };
void() boss_idle23 = [ $walk23, boss_idle24 ] { boss_face(); };
void() boss_idle24 = [ $walk24, boss_idle25 ] { boss_face(); };
void() boss_idle25 = [ $walk25, boss_idle26 ] { boss_face(); };
void() boss_idle26 = [ $walk26, boss_idle27 ] { boss_face(); };
void() boss_idle27 = [ $walk27, boss_idle28 ] { boss_face(); };
void() boss_idle28 = [ $walk28, boss_idle29 ] { boss_face(); };
void() boss_idle29 = [ $walk29, boss_idle30 ] { boss_face(); };
void() boss_idle30 = [ $walk30, boss_idle31 ] { boss_face(); };
void() boss_idle31 = [ $walk31, boss_idle1  ] { boss_face(); };

void() boss_missile1  = [ $attack1,  boss_missile2  ] { boss_face(); };
void() boss_missile2  = [ $attack2,  boss_missile3  ] { boss_face(); };
void() boss_missile3  = [ $attack3,  boss_missile4  ] { boss_face(); };
void() boss_missile4  = [ $attack4,  boss_missile5  ] { boss_face(); };
void() boss_missile5  = [ $attack5,  boss_missile6  ] { boss_face(); };
void() boss_missile6  = [ $attack6,  boss_missile7  ] { boss_face(); };
void() boss_missile7  = [ $attack7,  boss_missile8  ] { boss_face(); };
void() boss_missile8  = [ $attack8,  boss_missile9  ] { boss_face(); };
void() boss_missile9  = [ $attack9,  boss_missile10 ] { boss_missile('100 100 200'); };
void() boss_missile10 = [ $attack10, boss_missile11 ] { boss_face(); };
void() boss_missile11 = [ $attack11, boss_missile12 ] { boss_face(); };
void() boss_missile12 = [ $attack12, boss_missile13 ] { boss_face(); };
void() boss_missile13 = [ $attack13, boss_missile14 ] { boss_face(); };
void() boss_missile14 = [ $attack14, boss_missile15 ] { boss_face(); };
void() boss_missile15 = [ $attack15, boss_missile16 ] { boss_face(); };
void() boss_missile16 = [ $attack16, boss_missile17 ] { boss_face(); };
void() boss_missile17 = [ $attack17, boss_missile18 ] { boss_face(); };
void() boss_missile18 = [ $attack18, boss_missile19 ] { boss_face(); };
void() boss_missile19 = [ $attack19, boss_missile20 ] { boss_face(); };
void() boss_missile20 = [ $attack20, boss_missile21 ] { boss_missile('100 -100 200'); };
void() boss_missile21 = [ $attack21, boss_missile22 ] { boss_face(); };
void() boss_missile22 = [ $attack22, boss_missile23 ] { boss_face(); };
void() boss_missile23 = [ $attack23, boss_missile1  ] { boss_face(); };

void() boss_shocka1  = [ $shocka1,  boss_shocka2  ] {};
void() boss_shocka2  = [ $shocka2,  boss_shocka3  ] {};
void() boss_shocka3  = [ $shocka3,  boss_shocka4  ] {};
void() boss_shocka4  = [ $shocka4,  boss_shocka5  ] {};
void() boss_shocka5  = [ $shocka5,  boss_shocka6  ] {};
void() boss_shocka6  = [ $shocka6,  boss_shocka7  ] {};
void() boss_shocka7  = [ $shocka7,  boss_shocka8  ] {};
void() boss_shocka8  = [ $shocka8,  boss_shocka9  ] {};
void() boss_shocka9  = [ $shocka9,  boss_shocka10 ] {};
void() boss_shocka10 = [ $shocka10, boss_missile1 ] {};

void() boss_shockb1  = [ $shockb1, boss_shockb2  ] {};
void() boss_shockb2  = [ $shockb2, boss_shockb3  ] {};
void() boss_shockb3  = [ $shockb3, boss_shockb4  ] {};
void() boss_shockb4  = [ $shockb4, boss_shockb5  ] {};
void() boss_shockb5  = [ $shockb5, boss_shockb6  ] {};
void() boss_shockb6  = [ $shockb6, boss_shockb7  ] {};
void() boss_shockb7  = [ $shockb1, boss_shockb8  ] {};
void() boss_shockb8  = [ $shockb2, boss_shockb9  ] {};
void() boss_shockb9  = [ $shockb3, boss_shockb10 ] {};
void() boss_shockb10 = [ $shockb4, boss_missile1 ] {};

void() boss_shockc1  = [ $shockc1,  boss_shockc2  ] {};
void() boss_shockc2  = [ $shockc2,  boss_shockc3  ] {};
void() boss_shockc3  = [ $shockc3,  boss_shockc4  ] {};
void() boss_shockc4  = [ $shockc4,  boss_shockc5  ] {};
void() boss_shockc5  = [ $shockc5,  boss_shockc6  ] {};
void() boss_shockc6  = [ $shockc6,  boss_shockc7  ] {};
void() boss_shockc7  = [ $shockc7,  boss_shockc8  ] {};
void() boss_shockc8  = [ $shockc8,  boss_shockc9  ] {};
void() boss_shockc9  = [ $shockc9,  boss_shockc10 ] {};
void() boss_shockc10 = [ $shockc10, boss_death1   ] {};

void() boss_death1  = [ $death1, boss_death2  ] { sound(self, CHAN_VOICE, "boss1/death.wav", 1, ATTN_NORM); };
void() boss_death2  = [ $death2, boss_death3  ] {};
void() boss_death3  = [ $death3, boss_death4  ] {};
void() boss_death4  = [ $death4, boss_death5  ] {};
void() boss_death5  = [ $death5, boss_death6  ] {};
void() boss_death6  = [ $death6, boss_death7  ] {};
void() boss_death7  = [ $death7, boss_death8  ] {};
void() boss_death8  = [ $death8, boss_death9  ] {};
void() boss_death9  = [ $death9, boss_death10 ] {
    sound(self, CHAN_BODY, "boss1/out1.wav", 1, ATTN_NORM);
    WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
    WriteByte(MSG_BROADCAST, TE_LAVASPLASH);
    WriteCoord(MSG_BROADCAST, self.origin_x);
    WriteCoord(MSG_BROADCAST, self.origin_y);
    WriteCoord(MSG_BROADCAST, self.origin_z);
};
void() boss_death10 = [ $death9, boss_death10 ] {
    killed_monsters = killed_monsters + 1;
    WriteByte(MSG_ALL, SVC_KILLEDMONSTER); // FIXME: reliable broadcast
    SUB_UseTargets();
    remove(self);
};

void(vector p) boss_missile =
{
    local vector offang;
    local vector org, vec, d;
    local float t;

    offang = vectoangles(self.enemy.origin - self.origin);
    makevectors(offang);

    org = self.origin + p_x * v_forward + p_y * v_right + p_z * '0 0 1';

    // lead the player on hard mode
    if (skill > 1)
    {
        t = vlen(self.enemy.origin - org) / 300;
        vec = self.enemy.velocity;
        vec_z = 0;
        d = self.enemy.origin + t * vec;
    }
    else
    {
        d = self.enemy.origin;
    }

    vec = normalize(d - org);

    launch_spike(org, vec);
    setmodel(newmis, "progs/lavaball.mdl");
    newmis.avelocity = '200 100 300';
    setsize(newmis, VEC_ORIGIN, VEC_ORIGIN);
    newmis.velocity = vec * 300;
    newmis.touch = T_MissileTouch; // rocket explosion
    sound(self, CHAN_WEAPON, "boss1/throw.wav", 1, ATTN_NORM);

    // check for dead enemy
    if (self.enemy.health <= 0)
    {
        boss_idle1();
    }
};


void() boss_awake =
{
    self.solid = SOLID_SLIDEBOX;
    self.movetype = MOVETYPE_STEP;
    self.takedamage = DAMAGE_NO;

    setmodel(self, "progs/boss.mdl");
    setsize(self, '-128 -128 -24', '128 128 256');

    if (skill == 0)
    {
        self.health = 1;
    }
    else
    {
        self.health = 3;
    }

    self.enemy = activator;

    WriteByte(MSG_BROADCAST, SVC_TEMPENTITY);
    WriteByte(MSG_BROADCAST, TE_LAVASPLASH);
    WriteCoord(MSG_BROADCAST, self.origin_x);
    WriteCoord(MSG_BROADCAST, self.origin_y);
    WriteCoord(MSG_BROADCAST, self.origin_z);

    self.yaw_speed = 20;
    boss_rise1();
};


/*QUAKED monster_boss (1 0 0) (-128 -128 -24) (128 128 256)
*/
void() monster_boss =
{
    if (deathmatch)
    {
        remove(self);
        return;
    }

    precache_model("progs/boss.mdl");
    precache_model("progs/lavaball.mdl");

    precache_sound("weapons/rocket1i.wav");
    precache_sound("boss1/out1.wav");
    precache_sound("boss1/sight1.wav");
    precache_sound("misc/power.wav");
    precache_sound("boss1/throw.wav");
    precache_sound("boss1/pain.wav");
    precache_sound("boss1/death.wav");

    total_monsters = total_monsters + 1;

    self.use = boss_awake;
};


//===========================================================================

entity le1, le2;
float lightning_end;

void() lightning_fire =
{
    local vector p1, p2;

    if (time >= lightning_end)
    {
        // done here, put the terminals back up
        self = le1;
        door_go_down();
        self = le2;
        door_go_down();
        return;
    }

    p1 = (le1.mins + le1.maxs) * 0.5;
    p1_z = le1.absmin_z - 16;

    p2 = (le2.mins + le2.maxs) * 0.5;
    p2_z = le2.absmin_z - 16;

    // compensate for length of bolt
    p2 = p2 - normalize(p2 - p1) * 100;

    self.nextthink = time + 0.1;
    self.think = lightning_fire;

    WriteByte(MSG_ALL, SVC_TEMPENTITY);
    WriteByte(MSG_ALL, TE_LIGHTNING3);
    WriteEntity(MSG_ALL, world);
    WriteCoord(MSG_ALL, p1_x);
    WriteCoord(MSG_ALL, p1_y);
    WriteCoord(MSG_ALL, p1_z);
    WriteCoord(MSG_ALL, p2_x);
    WriteCoord(MSG_ALL, p2_y);
    WriteCoord(MSG_ALL, p2_z);
};


void() lightning_use =
{
    if (lightning_end >= (time + 1))
    {
        return;
    }

    le1 = find(world, target, "lightning");
    le2 = find(le1, target, "lightning");
    if (!le1 || !le2)
    {
        dprint("missing lightning targets\n");
        return;
    }

    if ((le1.state != STATE_TOP && le1.state != STATE_BOTTOM)
        || (le2.state != STATE_TOP && le2.state != STATE_BOTTOM)
        || (le1.state != le2.state))
    {
        //dprint("not aligned\n");
        return;
    }

    // don't let the electrodes go back up until the bolt is done
    le1.nextthink = -1;
    le2.nextthink = -1;
    lightning_end = time + 1;

    sound(self, CHAN_VOICE, "misc/power.wav", 1, ATTN_NORM);
    lightning_fire();

    // advance the boss pain if down
    self = find(world, classname, "monster_boss");
    if (!self)
    {
        return;
    }

    self.enemy = activator;

    if (le1.state == STATE_TOP && self.health > 0)
    {
        sound(self, CHAN_VOICE, "boss1/pain.wav", 1, ATTN_NORM);
        self.health = self.health - 1;
        if (self.health >= 2)
        {
            boss_shocka1();
        }
        else if (self.health == 1)
        {
            boss_shockb1();
        }
        else if (self.health == 0)
        {
            boss_shockc1();
        }
    }
};


/*QUAKED event_lightning (0 1 1) (-16 -16 -16) (16 16 16)
Just for boss level.
*/
void() event_lightning =
{
    self.use = lightning_use;
};
