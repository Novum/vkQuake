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

float modelindex_eyes, modelindex_player;

/*
=============================================================================

                LEVEL CHANGING / INTERMISSION

=============================================================================
*/

float intermission_running;
float intermission_exittime;

/*QUAKED info_intermission (1 0.5 0.5) (-16 -16 -16) (16 16 16)
This is the camera point for the intermission.
Use mangle instead of angle, so you can set pitch or roll as well as yaw.  'pitch roll yaw'
*/
void() info_intermission =
{
};


void() SetChangeParms =
{
    if (self.health <= 0)
    {
        SetNewParms();
        return;
    }

    // remove items
    self.items = self.items - (self.items & (IT_KEY1 | IT_KEY2 | IT_INVISIBILITY | IT_INVULNERABILITY | IT_SUIT | IT_QUAD));

    // cap super health
    if (self.health > 100)
    {
        self.health = 100;
    }
    else if (self.health < 50)
    {
        self.health = 50;
    }

    parm1 = self.items;
    parm2 = self.health;
    parm3 = self.armorvalue;
    if (self.ammo_shells < 25)
    {
        parm4 = 25;
    }
    else
    {
        parm4 = self.ammo_shells;
    }
    parm5 = self.ammo_nails;
    parm6 = self.ammo_rockets;
    parm7 = self.ammo_cells;
    parm8 = self.weapon;
    parm9 = self.armortype * 100;
};


void() SetNewParms =
{
    parm1 = IT_SHOTGUN | IT_AXE;
    parm2 = 100;
    parm3 = 0;
    parm4 = 25;
    parm5 = 0;
    parm6 = 0;
    parm7 = 0;
    parm8 = 1;
    parm9 = 0;
};


void() DecodeLevelParms =
{
    if (serverflags)
    {
        if (world.model == "maps/start.bsp")
        {
            SetNewParms(); // take away all stuff on starting new episode
        }
    }

    self.items = parm1;
    self.health = parm2;
    self.armorvalue = parm3;
    self.ammo_shells = parm4;
    self.ammo_nails = parm5;
    self.ammo_rockets = parm6;
    self.ammo_cells = parm7;
    self.weapon = parm8;
    self.armortype = parm9 * 0.01;
};


/*
============
FindIntermission

Returns the entity to view from
============
*/
entity() FindIntermission =
{
    local entity spot;
    local float cyc;

    // look for info_intermission first
    spot = find(world, classname, "info_intermission");
    if (spot)
    {
        // pick a random one
        cyc = random() * 4;
        while (cyc > 1)
        {
            spot = find(spot, classname, "info_intermission");
            if (!spot)
            {
                spot = find(spot, classname, "info_intermission");
            }
            cyc = cyc - 1;
        }
        return spot;
    }

    // then look for the start position
    spot = find(world, classname, "info_player_start");
    if (spot)
    {
        return spot;
    }

    // testinfo_player_start is only found in regioned levels
    spot = find(world, classname, "testplayerstart");
    if (spot)
    {
        return spot;
    }

    objerror("FindIntermission: no spot");
};


string nextmap;

void() GotoNextMap =
{
    if (cvar("samelevel")) // if samelevel is set, stay on same level
    {
        changelevel(mapname);
    }
    else
    {
        changelevel(nextmap);
    }
};


void() ExitIntermission =
{
    // skip any text in deathmatch
    if (deathmatch)
    {
        GotoNextMap();
        return;
    }

    intermission_exittime = time + 1;
    intermission_running = intermission_running + 1;

    //
    // run some text if at the end of an episode
    //
    if (intermission_running == 2)
    {
        if (world.model == "maps/e1m7.bsp")
        {
            WriteByte(MSG_ALL, SVC_CDTRACK);
            WriteByte(MSG_ALL, 2);
            WriteByte(MSG_ALL, 3);
            if (!cvar("registered"))
            {
                WriteByte(MSG_ALL, SVC_FINALE);
                WriteString(MSG_ALL, "As the corpse of the monstrous entity\nChthon sinks back into the lava whence\nit rose, you grip the Rune of Earth\nMagic tightly. Now that you have\nconquered the Dimension of the Doomed,\nrealm of Earth Magic, you are ready to\ncomplete your task in the other three\nhaunted lands of Quake. Or are you? If\nyou don't register Quake, you'll never\nknow what awaits you in the Realm of\nBlack Magic, the Netherworld, and the\nElder World!");
            }
            else
            {
                WriteByte(MSG_ALL, SVC_FINALE);
                WriteString(MSG_ALL, "As the corpse of the monstrous entity\nChthon sinks back into the lava whence\nit rose, you grip the Rune of Earth\nMagic tightly. Now that you have\nconquered the Dimension of the Doomed,\nrealm of Earth Magic, you are ready to\ncomplete your task. A Rune of magic\npower lies at the end of each haunted\nland of Quake. Go forth, seek the\ntotality of the four Runes!");
            }
            return;
        }
        else if (world.model == "maps/e2m6.bsp")
        {
            WriteByte(MSG_ALL, SVC_CDTRACK);
            WriteByte(MSG_ALL, 2);
            WriteByte(MSG_ALL, 3);

            WriteByte(MSG_ALL, SVC_FINALE);
            WriteString(MSG_ALL, "The Rune of Black Magic throbs evilly in\nyour hand and whispers dark thoughts\ninto your brain. You learn the inmost\nlore of the Hell-Mother; Shub-Niggurath!\nYou now know that she is behind all the\nterrible plotting which has led to so\nmuch death and horror. But she is not\ninviolate! Armed with this Rune, you\nrealize that once all four Runes are\ncombined, the gate to Shub-Niggurath's\nPit will open, and you can face the\nWitch-Goddess herself in her frightful\notherworld cathedral.");
            return;
        }
        else if (world.model == "maps/e3m6.bsp")
        {
            WriteByte(MSG_ALL, SVC_CDTRACK);
            WriteByte(MSG_ALL, 2);
            WriteByte(MSG_ALL, 3);

            WriteByte(MSG_ALL, SVC_FINALE);
            WriteString(MSG_ALL, "The charred viscera of diabolic horrors\nbubble viscously as you seize the Rune\nof Hell Magic. Its heat scorches your\nhand, and its terrible secrets blight\nyour mind. Gathering the shreds of your\ncourage, you shake the devil's shackles\nfrom your soul, and become ever more\nhard and determined to destroy the\nhideous creatures whose mere existence\nthreatens the souls and psyches of all\nthe population of Earth.");
            return;
        }
        else if (world.model == "maps/e4m7.bsp")
        {
            WriteByte(MSG_ALL, SVC_CDTRACK);
            WriteByte(MSG_ALL, 2);
            WriteByte(MSG_ALL, 3);

            WriteByte(MSG_ALL, SVC_FINALE);
            WriteString(MSG_ALL, "Despite the awful might of the Elder\nWorld, you have achieved the Rune of\nElder Magic, capstone of all types of\narcane wisdom. Beyond good and evil,\nbeyond life and death, the Rune\npulsates, heavy with import. Patient and\npotent, the Elder Being Shub-Niggurath\nweaves her dire plans to clear off all\nlife from the Earth, and bring her own\nfoul offspring to our world! For all the\ndwellers in these nightmare dimensions\nare her descendants! Once all Runes of\nmagic power are united, the energy\nbehind them will blast open the Gateway\nto Shub-Niggurath, and you can travel\nthere to foil the Hell-Mother's plots\nin person.");
            return;
        }

        GotoNextMap();
    }

    if (intermission_running == 3)
    {
        if (!cvar("registered"))
        {
            // shareware episode has been completed, go to sell screen
            WriteByte(MSG_ALL, SVC_SELLSCREEN);
            return;
        }

        if ((serverflags & 15) == 15)
        {
            WriteByte(MSG_ALL, SVC_FINALE);
            WriteString(MSG_ALL, "Now, you have all four Runes. You sense\ntremendous invisible forces moving to\nunseal ancient barriers. Shub-Niggurath\nhad hoped to use the Runes Herself to\nclear off the Earth, but now instead,\nyou will use them to enter her home and\nconfront her as an avatar of avenging\nEarth-life. If you defeat her, you will\nbe remembered forever as the savior of\nthe planet. If she conquers, it will be\nas if you had never been born.");
            return;
        }
    }

    GotoNextMap();
};


/*
============
IntermissionThink

When the player presses attack or jump, change to the next level
============
*/
void() IntermissionThink =
{
    if (time < intermission_exittime)
    {
        return;
    }

    if (!self.button0 && !self.button1 && !self.button2)
    {
        return;
    }

    ExitIntermission();
};


void() execute_changelevel =
{
    local entity pos;

    intermission_running = 1;

    // enforce a wait time before allowing changelevel
    if (deathmatch)
    {
        intermission_exittime = time + 5;
    }
    else
    {
        intermission_exittime = time + 2;
    }

    WriteByte(MSG_ALL, SVC_CDTRACK);
    WriteByte(MSG_ALL, 3);
    WriteByte(MSG_ALL, 3);

    pos = FindIntermission();

    other = find(world, classname, "player");
    while (other != world)
    {
        other.view_ofs = '0 0 0';
        other.angles = other.v_angle = pos.mangle;
        other.fixangle = TRUE; // turn this way immediately
        other.nextthink = time + 0.5;
        other.takedamage = DAMAGE_NO;
        other.solid = SOLID_NOT;
        other.movetype = MOVETYPE_NONE;
        other.modelindex = 0;
        setorigin(other, pos.origin);
        other = find(other, classname, "player");
    }

    WriteByte(MSG_ALL, SVC_INTERMISSION);
};


void() changelevel_touch =
{
    if (other.classname != "player")
    {
        return;
    }

    if (cvar("noexit") == 1 || (cvar("noexit") == 2 && mapname != "start"))
    {
        T_Damage(other, self, self, 50000);
        return;
    }

    if (coop || deathmatch)
    {
        bprint(other.netname);
        bprint(" exited the level\n");
    }

    nextmap = self.map;

    SUB_UseTargets();

    if ((self.spawnflags & 1) && deathmatch == 0)
    {
        // NO_INTERMISSION
        GotoNextMap();
        return;
    }

    self.touch = SUB_Null;

    // we can't move people right now, because touch functions are called
    // in the middle of C movement code, so set a think time to do it
    self.think = execute_changelevel;
    self.nextthink = time + 0.1;
};


/*QUAKED trigger_changelevel (0.5 0.5 0.5) ? NO_INTERMISSION
When the player touches this, he gets sent to the map listed in the "map" variable.
Unless the NO_INTERMISSION flag is set, the view will go to the info_intermission spot and display stats.
*/
void() trigger_changelevel =
{
    if (!self.map)
    {
        objerror("changelevel trigger doesn't have map");
    }

    InitTrigger();
    self.touch = changelevel_touch;
};


/*
=============================================================================

                PLAYER GAME EDGE FUNCTIONS

=============================================================================
*/

// called by ClientKill and DeadThink
void() respawn =
{
    if (coop)
    {
        // make a copy of the dead body for appearances sake
        CopyToBodyQueue(self);
        // get the spawn parms as they were at level start
        setspawnparms(self);
        // respawn
        PutClientInServer();
    }
    else if (deathmatch)
    {
        // make a copy of the dead body for appearances sake
        CopyToBodyQueue(self);
        // set default spawn parms
        SetNewParms();
        // respawn
        PutClientInServer();
    }
    else
    {
        // restart the entire server
        localcmd("restart\n");
    }
};


/*
============
ClientKill

Player entered the suicide command
============
*/
void() ClientKill =
{
    bprint(self.netname);
    bprint(" suicides\n");
    set_suicide_frame();
    self.modelindex = modelindex_player;
    self.frags = self.frags - 2; // extra penalty
    respawn();
};


float(vector v) CheckSpawnPoint =
{
    return FALSE;
};


/*
============
SelectSpawnPoint

Returns the entity to spawn at
============
*/
entity() SelectSpawnPoint =
{
    local entity spot;
    local entity thing;
    local float pcount;

    // testinfo_player_start is only found in regioned levels
    spot = find(world, classname, "testplayerstart");
    if (spot)
    {
        return spot;
    }

    // choose a info_player_deathmatch point
    if (coop)
    {
        lastspawn = find(lastspawn, classname, "info_player_coop");
        if (lastspawn == world)
        {
            lastspawn = find(lastspawn, classname, "info_player_start");
        }
        if (lastspawn != world)
        {
            return lastspawn;
        }
    }
    else if (deathmatch)
    {
        spot = lastspawn;
        while (TRUE)
        {
            spot = find(spot, classname, "info_player_deathmatch");
            if (spot != world)
            {
                if (spot == lastspawn)
                {
                    return lastspawn;
                }

                pcount = 0;
                thing = findradius(spot.origin, 32);
                while (thing)
                {
                    if (thing.classname == "player")
                    {
                        pcount = pcount + 1;
                    }
                    thing = thing.chain;
                }
                if (pcount == 0)
                {
                    lastspawn = spot;
                    return spot;
                }
            }
        }
    }

    if (serverflags)
    {
        // return with a rune to start
        spot = find(world, classname, "info_player_start2");
        if (spot)
        {
            return spot;
        }
    }

    spot = find(world, classname, "info_player_start");
    if (!spot)
    {
        error("PutClientInServer: no info_player_start on level");
    }

    return spot;
};


/*
===========
PutClientInServer

called each time a player is spawned
============
*/
void() PutClientInServer =
{
    local entity spot;

    spot = SelectSpawnPoint();

    self.classname = "player";
    self.health = 100;
    self.takedamage = DAMAGE_AIM;
    self.solid = SOLID_SLIDEBOX;
    self.movetype = MOVETYPE_WALK;
    self.show_hostile = 0;
    self.max_health = 100;
    self.flags = FL_CLIENT;
    self.air_finished = time + 12;
    self.dmg = 2; // initial water damage
    self.super_damage_finished = 0;
    self.radsuit_finished = 0;
    self.invisible_finished = 0;
    self.invincible_finished = 0;
    self.effects = 0;
    self.invincible_time = 0;

    DecodeLevelParms();

    W_SetCurrentAmmo();

    self.attack_finished = time;
    self.th_pain = player_pain;
    self.th_die = PlayerDie;

    self.deadflag = DEAD_NO;
    // pausetime is set by teleporters to keep the player from moving a while
    self.pausetime = 0;

    //spot = SelectSpawnPoint();

    self.origin = spot.origin + '0 0 1';
    self.angles = spot.angles;
    self.fixangle = TRUE; // turn this way immediately

    // oh, this is a hack!
    setmodel(self, "progs/eyes.mdl");
    modelindex_eyes = self.modelindex;

    setmodel(self, "progs/player.mdl");
    modelindex_player = self.modelindex;

    setsize(self, VEC_HULL_MIN, VEC_HULL_MAX);

    self.view_ofs = '0 0 22';

    player_stand1();

    if (deathmatch || coop)
    {
        makevectors(self.angles);
        spawn_tfog(self.origin + v_forward * 20);
    }

    spawn_tdeath(self.origin, self);
};


/*
=============================================================================

                QUAKED FUNCTIONS

=============================================================================
*/

/*QUAKED info_player_start (1 0 0) (-16 -16 -24) (16 16 24)
The normal starting point for a level.
*/
void() info_player_start =
{
};


/*QUAKED info_player_start2 (1 0 0) (-16 -16 -24) (16 16 24)
Only used on start map for the return point from an episode.
*/
void() info_player_start2 =
{
};


/*
saved out by quaked in region mode
*/
void() testplayerstart =
{
};


/*QUAKED info_player_deathmatch (1 0 1) (-16 -16 -24) (16 16 24)
potential spawning position for deathmatch games
*/
void() info_player_deathmatch =
{
};


/*QUAKED info_player_coop (1 0 1) (-16 -16 -24) (16 16 24)
potential spawning position for coop games
*/
void() info_player_coop =
{
};


/*
===============================================================================

RULES

===============================================================================
*/

/*
go to the next level for deathmatch
only called if a time or frag limit has expired
*/
void() NextLevel =
{
    local entity o;

    if (mapname == "start")
    {
        if (!cvar("registered"))
        {
            mapname = "e1m1";
        }
        else if (!(serverflags & 1))
        {
            mapname = "e1m1";
            serverflags = serverflags | 1;
        }
        else if (!(serverflags & 2))
        {
            mapname = "e2m1";
            serverflags = serverflags | 2;
        }
        else if (!(serverflags & 4))
        {
            mapname = "e3m1";
            serverflags = serverflags | 4;
        }
        else if (!(serverflags & 8))
        {
            mapname = "e4m1";
            serverflags = serverflags - 7;
        }

        o = spawn();
        o.map = mapname;
    }
    else
    {
        // find a trigger_changelevel
        o = find(world, classname, "trigger_changelevel");

        // go back to start if no trigger_changelevel
        if (!o)
        {
            mapname = "start";
            o = spawn();
            o.map = mapname;
        }
    }

    nextmap = o.map;
    gameover = TRUE;

    if (o.nextthink < time)
    {
        o.think = execute_changelevel;
        o.nextthink = time + 0.1;
    }
};


/*
============
CheckRules

Exit deathmatch games upon conditions
============
*/
void() CheckRules =
{
    local float timelimit;
    local float fraglimit;

    if (gameover) // someone else quit the game already
    {
        return;
    }

    timelimit = cvar("timelimit") * 60;
    fraglimit = cvar("fraglimit");

    if (timelimit && time >= timelimit)
    {
        NextLevel();
        return;
    }

    if (fraglimit && self.frags >= fraglimit)
    {
        NextLevel();
        return;
    }
};


//============================================================================

void() PlayerDeathThink =
{
    local float forward;

    if ((self.flags & FL_ONGROUND))
    {
        forward = vlen(self.velocity);
        forward = forward - 20;
        if (forward <= 0)
        {
            self.velocity = '0 0 0';
        }
        else
        {
            self.velocity = forward * normalize(self.velocity);
        }
    }

    // wait for all buttons released
    if (self.deadflag == DEAD_DEAD)
    {
        if (self.button2 || self.button1 || self.button0)
        {
            return;
        }

        self.deadflag = DEAD_RESPAWNABLE;
        return;
    }

    // wait for any button down
    if (!self.button2 && !self.button1 && !self.button0)
    {
        return;
    }

    self.button0 = 0;
    self.button1 = 0;
    self.button2 = 0;
    respawn();
};


void() PlayerJump =
{
    if ((self.flags & FL_WATERJUMP))
    {
        return;
    }

    if (self.waterlevel >= 2)
    {
        if (self.watertype == CONTENT_WATER)
        {
            self.velocity_z = 100;
        }
        else if (self.watertype == CONTENT_SLIME)
        {
            self.velocity_z = 80;
        }
        else
        {
            self.velocity_z = 50;
        }

        // play swimming sound
        if (self.swim_flag < time)
        {
            self.swim_flag = time + 1;
            if (random() < 0.5)
            {
                sound(self, CHAN_BODY, "misc/water1.wav", 1, ATTN_NORM);
            }
            else
            {
                sound(self, CHAN_BODY, "misc/water2.wav", 1, ATTN_NORM);
            }
        }

        return;
    }

    if (!(self.flags & FL_ONGROUND))
    {
        return;
    }

    if (!(self.flags & FL_JUMPRELEASED))
    {
        return; // don't pogo stick
    }

    self.flags = self.flags - (self.flags & FL_JUMPRELEASED);

    self.flags = self.flags - FL_ONGROUND; // don't stairwalk

    self.button2 = 0;
    // player jumping sound
    sound(self, CHAN_BODY, "player/plyrjmp8.wav", 1, ATTN_NORM);
    self.velocity_z = self.velocity_z + 270;
};


/*
===========
WaterMove
============
*/
.float dmgtime;

void() WaterMove =
{
    //dprint(ftos(self.waterlevel));
    if (self.movetype == MOVETYPE_NOCLIP)
    {
        return;
    }
    if (self.health < 0)
    {
        return;
    }

    if (self.waterlevel != 3)
    {
        if (self.air_finished < time)
        {
            sound(self, CHAN_VOICE, "player/gasp2.wav", 1, ATTN_NORM);
        }
        else if (self.air_finished < (time + 9))
        {
            sound(self, CHAN_VOICE, "player/gasp1.wav", 1, ATTN_NORM);
        }
        self.air_finished = time + 12;
        self.dmg = 2;
    }
    else if (self.air_finished < time)
    {
        // drown!
        if (self.pain_finished < time)
        {
            self.dmg = self.dmg + 2;
            if (self.dmg > 15)
            {
                self.dmg = 10;
            }
            T_Damage(self, world, world, self.dmg);
            self.pain_finished = time + 1;
        }
    }

    if (!self.waterlevel)
    {
        if ((self.flags & FL_INWATER))
        {
            // play leave water sound
            sound(self, CHAN_BODY, "misc/outwater.wav", 1, ATTN_NORM);
            self.flags = self.flags - FL_INWATER;
        }
        return;
    }

    if (self.watertype == CONTENT_LAVA)
    {
        // do damage
        if (self.dmgtime < time)
        {
            if (self.radsuit_finished > time)
            {
                self.dmgtime = time + 1;
            }
            else
            {
                self.dmgtime = time + 0.2;
            }

            T_Damage(self, world, world, 10 * self.waterlevel);
        }
    }
    else if (self.watertype == CONTENT_SLIME)
    {
        // do damage
        if (self.dmgtime < time && self.radsuit_finished < time)
        {
            self.dmgtime = time + 1;
            T_Damage(self, world, world, 4 * self.waterlevel);
        }
    }

    if (!(self.flags & FL_INWATER))
    {
        // player enter water sound
        if (self.watertype == CONTENT_LAVA)
        {
            sound(self, CHAN_BODY, "player/inlava.wav", 1, ATTN_NORM);
        }
        else if (self.watertype == CONTENT_WATER)
        {
            sound(self, CHAN_BODY, "player/inh2o.wav", 1, ATTN_NORM);
        }
        else if (self.watertype == CONTENT_SLIME)
        {
            sound(self, CHAN_BODY, "player/slimbrn2.wav", 1, ATTN_NORM);
        }

        self.flags = self.flags + FL_INWATER;
        self.dmgtime = 0;
    }

    if (!(self.flags & FL_WATERJUMP))
    {
        self.velocity = self.velocity - 0.8 * self.waterlevel * frametime * self.velocity;
    }
};


void() CheckWaterJump =
{
    local vector start, end;

    // check for a jump-out-of-water
    makevectors(self.angles);
    start = self.origin;
    start_z = start_z + 8;
    v_forward_z = 0;
    normalize(v_forward);
    end = start + v_forward * 24;
    traceline(start, end, TRUE, self);
    if (trace_fraction < 1)
    {
        // solid at waist
        start_z = start_z + self.maxs_z - 8;
        end = start + v_forward * 24;
        self.movedir = trace_plane_normal * -50;
        traceline(start, end, TRUE, self);
        if (trace_fraction == 1)
        {
            // open at eye level
            self.flags = self.flags | FL_WATERJUMP;
            self.velocity_z = 225;
            self.flags = self.flags - (self.flags & FL_JUMPRELEASED);
            self.teleport_time = time + 2; // safety net
            return;
        }
    }
};


/*
================
PlayerPreThink

Called every frame before physics are run
================
*/
void() PlayerPreThink =
{
    if (intermission_running)
    {
        IntermissionThink(); // otherwise a button could be missed between
        return; // the think tics
    }

    if (self.view_ofs == '0 0 0')
    {
        return; // intermission or finale
    }

    makevectors(self.v_angle); // is this still used

    CheckRules();
    WaterMove();

    if (self.waterlevel == 2)
    {
        CheckWaterJump();
    }

    if (self.deadflag >= DEAD_DEAD)
    {
        PlayerDeathThink();
        return;
    }

    if (self.deadflag == DEAD_DYING)
    {
        return; // dying, so do nothing
    }

    if (self.button2)
    {
        PlayerJump();
    }
    else
    {
        self.flags = self.flags | FL_JUMPRELEASED;
    }

    // teleporters can force a non-moving pause time
    if (time < self.pausetime)
    {
        self.velocity = '0 0 0';
    }

    if (time > self.attack_finished && self.currentammo == 0 && self.weapon != IT_AXE)
    {
        self.weapon = W_BestWeapon();
        W_SetCurrentAmmo();
    }
};


/*
================
CheckPowerups

Check for turning off powerups
================
*/
void() CheckPowerups =
{
    if (self.health <= 0)
    {
        return;
    }

    // invisibility
    if (self.invisible_finished)
    {
        // sound and screen flash when item starts to run out
        if (self.invisible_sound < time)
        {
            sound(self, CHAN_AUTO, "items/inv3.wav", 0.5, ATTN_IDLE);
            self.invisible_sound = time + ((random() * 3) + 1);
        }

        if (self.invisible_finished < (time + 3))
        {
            if (self.invisible_time == 1)
            {
                sprint(self, "Ring of Shadows magic is fading\n");
                stuffcmd(self, "bf\n");
                sound(self, CHAN_AUTO, "items/inv2.wav", 1, ATTN_NORM);
                self.invisible_time = time + 1;
            }
            else if (self.invisible_time < time)
            {
                self.invisible_time = time + 1;
                stuffcmd(self, "bf\n");
            }
        }

        if (self.invisible_finished < time)
        {
            // just stopped
            self.items = self.items - IT_INVISIBILITY;
            self.invisible_finished = 0;
            self.invisible_time = 0;
        }

        // use the eyes
        self.frame = 0;
        self.modelindex = modelindex_eyes;
    }
    else
    {
        self.modelindex = modelindex_player; // don't use eyes
    }

    // invincibility
    if (self.invincible_finished)
    {
        // sound and screen flash when item starts to run out
        if (self.invincible_finished < (time + 3))
        {
            if (self.invincible_time == 1)
            {
                sprint(self, "Protection is almost burned out\n");
                stuffcmd(self, "bf\n");
                sound(self, CHAN_AUTO, "items/protect2.wav", 1, ATTN_NORM);
                self.invincible_time = time + 1;
            }
            else if (self.invincible_time < time)
            {
                self.invincible_time = time + 1;
                stuffcmd(self, "bf\n");
            }
        }

        if (self.invincible_finished < time)
        {
            // just stopped
            self.items = self.items - IT_INVULNERABILITY;
            self.invincible_time = 0;
            self.invincible_finished = 0;
        }
        if (self.invincible_finished > time)
        {
            self.effects = self.effects | EF_DIMLIGHT;
        }
        else
        {
            self.effects = self.effects - (self.effects & EF_DIMLIGHT);
        }
    }

    // super damage
    if (self.super_damage_finished)
    {
        // sound and screen flash when item starts to run out
        if (self.super_damage_finished < (time + 3))
        {
            if (self.super_time == 1)
            {
                sprint(self, "Quad Damage is wearing off\n");
                stuffcmd(self, "bf\n");
                sound(self, CHAN_AUTO, "items/damage2.wav", 1, ATTN_NORM);
                self.super_time = time + 1;
            }
            else if (self.super_time < time)
            {
                self.super_time = time + 1;
                stuffcmd(self, "bf\n");
            }
        }

        if (self.super_damage_finished < time)
        {
            // just stopped
            self.items = self.items - IT_QUAD;
            self.super_damage_finished = 0;
            self.super_time = 0;
        }
        if (self.super_damage_finished > time)
        {
            self.effects = self.effects | EF_DIMLIGHT;
        }
        else
        {
            self.effects = self.effects - (self.effects & EF_DIMLIGHT);
        }
    }

    // suit
    if (self.radsuit_finished)
    {
        self.air_finished = time + 12; // don't drown

        // sound and screen flash when item starts to run out
        if (self.radsuit_finished < (time + 3))
        {
            if (self.rad_time == 1)
            {
                sprint(self, "Air supply in Biosuit expiring\n");
                stuffcmd(self, "bf\n");
                sound(self, CHAN_AUTO, "items/suit2.wav", 1, ATTN_NORM);
                self.rad_time = time + 1;
            }
            else if (self.rad_time < time)
            {
                self.rad_time = time + 1;
                stuffcmd(self, "bf\n");
            }
        }

        if (self.radsuit_finished < time)
        {
            // just stopped
            self.items = self.items - IT_SUIT;
            self.rad_time = 0;
            self.radsuit_finished = 0;
        }
    }
};


/*
================
PlayerPostThink

Called every frame after physics are run
================
*/
void() PlayerPostThink =
{
    if (self.view_ofs == '0 0 0')
    {
        return; // intermission or finale
    }
    if (self.deadflag)
    {
        return;
    }

    // do weapon stuff
    W_WeaponFrame();

    // check to see if player landed and play landing sound
    if (self.jump_flag < -300 && (self.flags & FL_ONGROUND) && self.health > 0)
    {
        if (self.watertype == CONTENT_WATER)
        {
            sound(self, CHAN_BODY, "player/h2ojump.wav", 1, ATTN_NORM);
        }
        else if (self.jump_flag < -650)
        {
            T_Damage(self, world, world, 5);
            sound(self, CHAN_VOICE, "player/land2.wav", 1, ATTN_NORM);
            self.deathtype = "falling";
        }
        else
        {
            sound(self, CHAN_VOICE, "player/land.wav", 1, ATTN_NORM);
        }

        self.jump_flag = 0;
    }

    if (!(self.flags & FL_ONGROUND))
    {
        self.jump_flag = self.velocity_z;
    }

    CheckPowerups();
};


/*
===========
ClientConnect

called when a player connects to a server
============
*/
void() ClientConnect =
{
    bprint(self.netname);
    bprint(" entered the game\n");

    // a client connecting during an intermission can cause problems
    if (intermission_running)
    {
        ExitIntermission();
    }
};


/*
===========
ClientDisconnect

called when a player disconnects from a server
============
*/
void() ClientDisconnect =
{
    if (gameover)
    {
        // if the level end trigger has been activated, just return
        // since they aren't *really* leaving
        return;
    }

    // let everyone else know
    bprint(self.netname);
    bprint(" left the game with ");
    bprint(ftos(self.frags));
    bprint(" frags\n");
    sound(self, CHAN_BODY, "player/tornoff2.wav", 1, ATTN_NONE);
    set_suicide_frame();
};


/*
===========
ClientObituary

called when a player dies
============
*/
void(entity targ, entity attacker) ClientObituary =
{
    local float rnum;
    local string deathstring, deathstring2;

    rnum = random();

    if (targ.classname == "player")
    {
        if (attacker.classname == "teledeath")
        {
            bprint(targ.netname);
            bprint(" was telefragged by ");
            bprint(attacker.owner.netname);
            bprint("\n");

            attacker.owner.frags = attacker.owner.frags + 1;
            return;
        }

        if (attacker.classname == "teledeath2")
        {
            bprint("Satan's power deflects ");
            bprint(targ.netname);
            bprint("'s telefrag\n");

            targ.frags = targ.frags - 1;
            return;
        }

        if (attacker.classname == "player")
        {
            // killed by a player (incl. itself)
            if (targ == attacker)
            {
                // killed self
                attacker.frags = attacker.frags - 1;

                bprint(targ.netname);
                if (targ.weapon == IT_LIGHTNING && targ.waterlevel > 1)
                {
                    bprint(" discharges into the water.\n");
                    return;
                }

                if (targ.weapon == IT_GRENADE_LAUNCHER)
                {
                    bprint(" tries to put the pin back in\n");
                }
                else
                {
                    bprint(" becomes bored with life\n");
                }
                return;
            }
            else if (teamplay == 2 && targ.team > 0 && targ.team == attacker.team)
            {
                if (rnum < 0.25)
                {
                    deathstring = " mows down a teammate\n";
                }
                else if (rnum < 0.50)
                {
                    deathstring = " checks his glasses\n";
                }
                else if (rnum < 0.75)
                {
                    deathstring = " gets a frag for the other team\n";
                }
                else
                {
                    deathstring = " loses another friend\n";
                }
                bprint(attacker.netname);
                bprint(deathstring);

                attacker.frags = attacker.frags - 1;
                return;
            }
            else
            {
                attacker.frags = attacker.frags + 1;

                rnum = attacker.weapon;
                if (rnum == IT_AXE)
                {
                    deathstring = " was axe-murdered by ";
                    deathstring2 = "\n";
                }
                else if (rnum == IT_SHOTGUN)
                {
                    deathstring = " chewed on ";
                    deathstring2 = "'s boomstick\n";
                }
                else if (rnum == IT_SUPER_SHOTGUN)
                {
                    deathstring = " ate 2 loads of ";
                    deathstring2 = "'s buckshot\n";
                }
                else if (rnum == IT_NAILGUN)
                {
                    deathstring = " was nailed by ";
                    deathstring2 = "\n";
                }
                else if (rnum == IT_SUPER_NAILGUN)
                {
                    deathstring = " was punctured by ";
                    deathstring2 = "\n";
                }
                else if (rnum == IT_GRENADE_LAUNCHER)
                {
                    deathstring = " eats ";
                    deathstring2 = "'s pineapple\n";
                    if (targ.health < -40)
                    {
                        deathstring = " was gibbed by ";
                        deathstring2 = "'s grenade\n";
                    }
                }
                else if (rnum == IT_ROCKET_LAUNCHER)
                {
                    deathstring = " rides ";
                    deathstring2 = "'s rocket\n";
                    if (targ.health < -40)
                    {
                        deathstring = " was gibbed by ";
                        deathstring2 = "'s rocket\n";
                    }
                }
                else if (rnum == IT_LIGHTNING)
                {
                    deathstring = " accepts ";
                    if (attacker.waterlevel > 1)
                    {
                        deathstring2 = "'s discharge\n";
                    }
                    else
                    {
                        deathstring2 = "'s shaft\n";
                    }
                }
                bprint(targ.netname);
                bprint(deathstring);
                bprint(attacker.netname);
                bprint(deathstring2);
            }
            return;
        }
        else
        {
            // killed by something else, which was not an attack by any player (incl. itself)
            targ.frags = targ.frags - 1;

            bprint(targ.netname);

            // killed by a monster?
            if ((attacker.flags & FL_MONSTER))
            {
                if (attacker.classname == "monster_army")
                {
                    bprint(" was shot by a Grunt\n");
                }
                else if (attacker.classname == "monster_demon1")
                {
                    bprint(" was eviscerated by a Fiend\n");
                }
                else if (attacker.classname == "monster_dog")
                {
                    bprint(" was mauled by a Rottweiler\n");
                }
                else if (attacker.classname == "monster_dragon")
                {
                    bprint(" was fried by a Dragon\n");
                }
                else if (attacker.classname == "monster_enforcer")
                {
                    bprint(" was blasted by an Enforcer\n");
                }
                else if (attacker.classname == "monster_fish")
                {
                    bprint(" was fed to the Rotfish\n");
                }
                else if (attacker.classname == "monster_hell_knight")
                {
                    bprint(" was slain by a Death Knight\n");
                }
                else if (attacker.classname == "monster_knight")
                {
                    bprint(" was slashed by a Knight\n");
                }
                else if (attacker.classname == "monster_ogre")
                {
                    bprint(" was destroyed by an Ogre\n");
                }
                else if (attacker.classname == "monster_oldone")
                {
                    bprint(" became one with Shub-Niggurath\n");
                }
                else if (attacker.classname == "monster_shalrath")
                {
                    bprint(" was exploded by a Vore\n");
                }
                else if (attacker.classname == "monster_shambler")
                {
                    bprint(" was smashed by a Shambler\n");
                }
                else if (attacker.classname == "monster_tarbaby")
                {
                    bprint(" was slimed by a Spawn\n");
                }
                else if (attacker.classname == "monster_vomit")
                {
                    bprint(" was vomited on by a Vomitus\n");
                }
                else if (attacker.classname == "monster_wizard")
                {
                    bprint(" was scragged by a Scrag\n");
                }
                else if (attacker.classname == "monster_zombie")
                {
                    bprint(" joins the Zombies\n");
                }

                return;
            }

            // tricks and traps
            if (attacker.classname == "explo_box")
            {
                bprint(" blew up\n");
                return;
            }
            if (attacker.solid == SOLID_BSP && attacker != world)
            {
                bprint(" was squished\n");
                return;
            }
            if (attacker.classname == "trap_shooter" || attacker.classname == "trap_spikeshooter")
            {
                bprint(" was spiked\n");
                return;
            }
            if (attacker.classname == "fireball")
            {
                bprint(" ate a lavaball\n");
                return;
            }
            if (attacker.classname == "trigger_changelevel")
            {
                bprint(" tried to leave\n");
                return;
            }

            // in-water deaths
            rnum = targ.watertype;
            if (rnum == CONTENT_WATER)
            {
                if (random() < 0.5)
                {
                    bprint(" sleeps with the fishes\n");
                }
                else
                {
                    bprint(" sucks it down\n");
                }
                return;
            }
            else if (rnum == CONTENT_SLIME)
            {
                if (random() < 0.5)
                {
                    bprint(" gulped a load of slime\n");
                }
                else
                {
                    bprint(" can't exist on slime alone\n");
                }
                return;
            }
            else if (rnum == CONTENT_LAVA)
            {
                if (targ.health < -15)
                {
                    bprint(" burst into flames\n");
                    return;
                }

                if (random() < 0.5)
                {
                    bprint(" turned into hot slag\n");
                }
                else
                {
                    bprint(" visits the Volcano God\n");
                }
                return;
            }

            // fell to their death?
            if (targ.deathtype == "falling")
            {
                targ.deathtype = "";
                bprint(" fell to his death\n");
                return;
            }

            // hell if I know; he's just dead!!!
            bprint(" died\n");
        }
    }
};
