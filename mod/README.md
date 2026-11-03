# Loot mod (work in progress)

A Quake 1 gameplay mod for vkQuake: guns built from parts, with rarity
tiers. The engine is unchanged. All the game logic is QuakeC in `qcsrc/`.

## Play it

1. Build `progs.dat` (see below), or use the one in this folder.
2. Copy this whole `mod` folder next to your `id1` folder (it needs Quake's
   `id1/pak0.pak` for maps, models and sounds).
3. Start the game: `vkquake -game mod +map e1m1`

`autoexec.cfg` sets these keys:

| Key | Action |
| --- | --- |
| 1-4 | select gun slot |
| 5 | axe |
| R | reload |
| E | swap your gun with the one on the floor |
| G | drop your gun |
| mouse wheel | next / previous gun |

The big ammo number on the HUD is your **magazine**. The small numbers at
the top are your reserve ammo.

## How the loot works

A gun is three numbers (`loot_core.qc`):

- `gun_a`: type, manufacturer, rarity, element, level
- `gun_b`: six part IDs: body, barrel, grip, magazine, sight, accessory
- `gun_s`: a seed for small random stat differences

All stats are calculated from these numbers each time. The same numbers
always give the same gun.

- **6 types**: Pistol, SMG, Shotgun, Rifle, Sniper, Launcher. They use
  Quake's existing models and ammo (shells, nails, cells, rockets).
- **6 manufacturers**. Each one has a rule:
  - Gravemark: hits hard, fires slow
  - Blackvein: huge magazine, wild spray
  - Hexwright: pinpoint accuracy
  - Ruinhaus: every hit explodes
  - Scrapjaw: very fast reloads
  - Vexcorp: 3-round burst
- **6 variants of each part**. Parts change how a gun feels, not only its
  numbers. For example, a Splitting barrel doubles the pellets, and a
  Stubby barrel on a launcher makes it lob bouncing grenades.
- **5 rarities**: Common (white), Uncommon (green), Rare (blue),
  Epic (purple), Legendary (orange). Floor guns sparkle in their rarity
  color, and legendaries show a pillar of light. Higher rarity means
  better part odds, more damage, and an accessory slot.

Where guns come from:

- Monsters drop them. Tougher monsters drop more often and roll better
  rarity (`Loot_MonsterDrop`).
- Each weapon placed in a map becomes a random gun of a matching type.
- You start with a common pistol.

Walk over a gun to pick it up into a free slot. When all four slots are
full, a stat card shows the DPS difference from your current gun, and
E swaps them.

Guns carry over between maps through vkQuake's extra spawn parms
(`parm17`-`parm34`).

## Developer tools (single player)

| Command | Effect |
| --- | --- |
| `impulse 200` | spawn a random gun |
| `impulse 201` | spawn a gun with high rarity odds |
| `impulse 202` | print the selected gun's card and raw numbers |
| `impulse 9` | the normal cheat, and also fills empty slots with guns |

## Build

You need [gmqcc](https://github.com/graphitemaster/gmqcc):

```sh
git clone https://github.com/graphitemaster/gmqcc && make -C gmqcc
GMQCC=gmqcc/gmqcc ./build.sh                          # writes progs.dat
GMQCC=gmqcc/gmqcc QCVM=gmqcc/qcvm ./build.sh test     # loot tests
```

The tests in `tests/` run `loot_core.qc` without the game. They check
that the same seed gives the same gun, that packing is exact, that the
rarity odds match the table, and that every gun type has sane stats.

## Files

- `qcsrc/loot_decl.qc`: shared fields and declarations
- `qcsrc/loot_core.qc`: the generator (tables, rolls, stats, names)
- `qcsrc/loot_game.qc`: slots, firing, reloads, floor guns, drops
- Stock id 1.06 files with small changes, each marked `// loot`:
  `weapons.qc`, `client.qc`, `combat.qc`, `items.qc`, `player.qc`

The base is id Software's QuakeC v1.06 (GPLv2, see `qcsrc/COPYING`).

## Next phases

1. Elements: fire, shock, corrosive, explosive. The bits are already
   reserved in `gun_a`.
2. XP and levels. `Loot_AreaLevel()` returns 1 for now.
3. Backpack storage beyond the four slots.
4. Later: skills, shields, unique legendary effects, new models.
