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

/* ALL MONSTERS SHOULD BE 1 0 0 IN COLOR */

// name = [ framenum, think ] { code }
// expands to: (see qcc's function PR_ParseState)
// name()
// {
//     self.frame = framenum;
//     self.nextthink = time + 0.1;
//     self.think = think;
//     <code>
// };

/*
================
monster_use

Using a monster makes it angry at the current activator
================
*/
void() monster_use =
{
    if (self.enemy)
    {
        return;
    }
    if (self.health <= 0)
    {
        return;
    }

    if ((activator.items & IT_INVISIBILITY))
    {
        return;
    }
    if ((activator.flags & FL_NOTARGET))
    {
        return;
    }
    if (activator.classname != "player")
    {
        return;
    }

    // delay reaction so if the monster is teleported, its sound is still heard
    self.enemy = activator;
    self.nextthink = time + 0.1;
    self.think = FoundTarget;
};


/*
================
monster_death_use

When a monster dies, it fires all of its targets with the current
enemy as activator.
================
*/
void() monster_death_use =
{
    // fall to ground
    if ((self.flags & FL_FLY))
    {
        self.flags = self.flags - FL_FLY;
    }
    if ((self.flags & FL_SWIM))
    {
        self.flags = self.flags - FL_SWIM;
    }

    if (!self.target)
    {
        return;
    }

    activator = self.enemy;
    SUB_UseTargets();
};


//============================================================================

void() walkmonster_start_go =
{
    self.origin_z = self.origin_z + 1; // raise off floor a bit
    droptofloor();

    if (!walkmove(0, 0))
    {
        dprint("walkmonster in wall at: ");
        dprint(vtos(self.origin));
        dprint("\n");
    }

    self.takedamage = DAMAGE_AIM;

    self.ideal_yaw = self.angles * '0 1 0';
    if (!self.yaw_speed)
    {
        self.yaw_speed = 20;
    }
    self.view_ofs = '0 0 25';
    self.use = monster_use;

    self.flags = self.flags | FL_MONSTER;

    if (self.target)
    {
        self.goalentity = self.movetarget = find(world, targetname, self.target);
        self.ideal_yaw = vectoyaw(self.goalentity.origin - self.origin);
        if (!self.movetarget)
        {
            // this used to be an objerror
            dprint("Monster can't find target at ");
            dprint(vtos(self.origin));
            dprint("\n");
        }
        if (self.movetarget.classname == "path_corner")
        {
            self.th_walk();
        }
        else
            // FIXME/Maddes: missing curly brackets, WARNING!!! logic change
            self.pausetime = 99999999;
            self.th_stand();
    }
    else
    {
        self.pausetime = 99999999;
        self.th_stand();
    }

    // spread think times so they don't all happen at same time
    self.nextthink = self.nextthink + random() * 0.5;
};


void() walkmonster_start =
{
    // delay drop to floor to make sure all doors have been spawned
    // spread think times so they don't all happen at same time
    self.nextthink = self.nextthink + random() * 0.5;
    self.think = walkmonster_start_go;
    total_monsters = total_monsters + 1;
};


void() flymonster_start_go =
{
    self.takedamage = DAMAGE_AIM;

    self.ideal_yaw = self.angles * '0 1 0';
    if (!self.yaw_speed)
    {
        self.yaw_speed = 10;
    }
    self.view_ofs = '0 0 25';
    self.use = monster_use;

    self.flags = self.flags | FL_FLY;
    self.flags = self.flags | FL_MONSTER;

    if (!walkmove(0, 0))
    {
        dprint("flymonster in wall at: ");
        dprint(vtos(self.origin));
        dprint("\n");
    }

    if (self.target)
    {
        self.goalentity = self.movetarget = find(world, targetname, self.target);
        if (!self.movetarget)
        {
            // this used to be an objerror
            dprint("Monster can't find target at ");
            dprint(vtos(self.origin));
            dprint("\n");
        }
        if (self.movetarget.classname == "path_corner")
        {
            self.th_walk();
        }
        else
            // FIXME/Maddes: missing curly brackets, WARNING!!! logic change
            self.pausetime = 99999999;
            self.th_stand();
    }
    else
    {
        self.pausetime = 99999999;
        self.th_stand();
    }
};


void() flymonster_start =
{
    // spread think times so they don't all happen at same time
    self.nextthink = self.nextthink + random() * 0.5;
    self.think = flymonster_start_go;
    total_monsters = total_monsters + 1;
};


void() swimmonster_start_go =
{
    if (deathmatch)
    {
        remove(self);
        return;
    }

    self.takedamage = DAMAGE_AIM;
    total_monsters = total_monsters + 1; // FIXME/Maddes: remove duplicate increase, do only in swimmonster_start() like for the other monster types

    self.ideal_yaw = self.angles * '0 1 0';
    if (!self.yaw_speed)
    {
        self.yaw_speed = 10;
    }
    self.view_ofs = '0 0 10';
    self.use = monster_use;

    self.flags = self.flags | FL_SWIM;
    self.flags = self.flags | FL_MONSTER;

    if (self.target)
    {
        self.goalentity = self.movetarget = find(world, targetname, self.target);
        if (!self.movetarget)
        {
            // this used to be an objerror
            dprint("Monster can't find target at ");
            dprint(vtos(self.origin));
            dprint("\n");
        }
        self.ideal_yaw = vectoyaw(self.goalentity.origin - self.origin);
        self.th_walk();
    }
    else
    {
        self.pausetime = 99999999;
        self.th_stand();
    }

    // spread think times so they don't all happen at same time
    self.nextthink = self.nextthink + random() * 0.5;
};


void() swimmonster_start =
{
    // spread think times so they don't all happen at same time
    self.nextthink = self.nextthink + random() * 0.5;
    self.think = swimmonster_start_go;
    total_monsters = total_monsters + 1;
};
