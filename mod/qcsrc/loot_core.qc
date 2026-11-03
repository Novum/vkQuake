/*
==============================================================================

LOOT CORE

The "brain" of the gun system. A gun is NOT a hand-made weapon. A gun is
three numbers:

    gun_a   type, manufacturer, rarity, element, level  (packed bits)
    gun_b   six part IDs: body, barrel, grip, mag, sight, accessory
    gun_s   the seed: a random number that fixes the small stat wobble

Every stat (damage, fire rate, spread, magazine...) is CALCULATED from these
three numbers whenever it is needed. Nothing else is stored. That is how
a few small tables multiply into a huge number of different guns.

This file only uses floor(), ftos() and strcat(). It never touches the
game world, so the headless test (mod/tests) can run it outside Quake.

Why packed numbers? QuakeC only has floats. A float holds whole numbers
exactly up to 2^24 (16777216). So we squeeze small IDs into one float with
multiply/divide by powers of two, which stay exact.

==============================================================================
*/

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// weapon types
float LT_PISTOL   = 0;
float LT_SMG      = 1;
float LT_SHOTGUN  = 2;
float LT_RIFLE    = 3;
float LT_SNIPER   = 4;
float LT_LAUNCHER = 5;
float LT_NUMTYPES = 6;

// manufacturers (each has one rule the player can learn)
float LM_GRAVEMARK = 0; // heavy: hits hard, fires slow, small mag
float LM_BLACKVEIN = 1; // spray: huge mag, fast, inaccurate
float LM_HEXWRIGHT = 2; // precise: tight spread, slow reload
float LM_RUINHAUS  = 3; // explosive: every hit also explodes
float LM_SCRAPJAW  = 4; // cheap: very fast reload, weaker
float LM_VEXCORP   = 5; // burst: every trigger pull fires 3 shots
float LM_NUMMFRS   = 6;

// rarity
float LR_COMMON    = 0;
float LR_UNCOMMON  = 1;
float LR_RARE      = 2;
float LR_EPIC      = 3;
float LR_LEGENDARY = 4;

// ammo pools (Quake's four)
float LA_SHELLS  = 0;
float LA_NAILS   = 1;
float LA_ROCKETS = 2;
float LA_CELLS   = 3;

// how a shot travels
float LP_HITSCAN = 0; // instant trace, like the shotgun
float LP_SPIKE   = 1; // nail projectile
float LP_ROCKET  = 2;
float LP_GRENADE = 3;

// part variants per part slot (fits in 4 bits)
float LOOT_VARIANTS = 6;

// ---------------------------------------------------------------------------
// Random numbers you can repeat
//
// Quake's random() gives a different answer every time. For loot we want
// "same seed = same gun", so we use our own generator (Wichmann-Hill).
// Three small counters, each multiplied by a small number and wrapped.
// All products stay below 2^24, so float math is exact.
// ---------------------------------------------------------------------------

float rng1, rng2, rng3;

// x mod m that never goes out of range, even with float rounding
float(float x, float m) Loot_Mod =
{
    local float r;

    r = x - floor(x / m) * m;
    if (r < 0)
    {
        r = r + m;
    }
    if (r >= m)
    {
        r = r - m;
    }
    return r;
};


float() Rng_Next =
{
    local float u;

    rng1 = Loot_Mod(171 * rng1, 30269);
    rng2 = Loot_Mod(172 * rng2, 30307);
    rng3 = Loot_Mod(170 * rng3, 30323);

    u = rng1 / 30269 + rng2 / 30307 + rng3 / 30323;
    u = u - floor(u);
    return u; // 0 <= u < 1
};


// whole number from 0 to n-1
float(float n) Rng_Int =
{
    local float v;

    v = floor(Rng_Next() * n);
    if (v >= n)
    {
        v = n - 1;
    }
    return v;
};


// seed must be a whole number from 0 to 16777215
void(float seed) Rng_Seed =
{
    local float lo, hi;

    seed = floor(seed);
    lo = Loot_Mod(seed, 4096);
    hi = Loot_Mod(floor(seed / 4096), 4096);

    rng1 = 1 + Loot_Mod(lo * 7 + hi, 30268);
    rng2 = 1 + Loot_Mod(hi * 7 + lo + 11, 30306);
    rng3 = 1 + Loot_Mod(lo * 5 + hi * 3 + 23, 30322);

    // stir a few times so close seeds give different guns
    Rng_Next();
    Rng_Next();
    Rng_Next();
};

// ---------------------------------------------------------------------------
// Packing and unpacking
// ---------------------------------------------------------------------------

// unpacked gun, filled by Loot_Unpack
float lg_type, lg_mfr, lg_rarity, lg_elem, lg_level;
float lg_body, lg_barrel, lg_grip, lg_mag, lg_sight, lg_acc;

// read "m" values starting at "div" (div is a power of two)
float(float v, float div, float m) Loot_Bits =
{
    return Loot_Mod(floor(v / div), m);
};


// gun_a layout:  type 3 bits | mfr 3 | rarity 3 | element 3 | level 7
// gun_b layout:  body 4 | barrel 4 | grip 4 | mag 4 | sight 4 | accessory 4
void(float a, float b) Loot_Unpack =
{
    lg_type   = Loot_Bits(a, 1, 8);
    lg_mfr    = Loot_Bits(a, 8, 8);
    lg_rarity = Loot_Bits(a, 64, 8);
    lg_elem   = Loot_Bits(a, 512, 8);
    lg_level  = Loot_Bits(a, 4096, 128);

    lg_body   = Loot_Bits(b, 1, 16);
    lg_barrel = Loot_Bits(b, 16, 16);
    lg_grip   = Loot_Bits(b, 256, 16);
    lg_mag    = Loot_Bits(b, 4096, 16);
    lg_sight  = Loot_Bits(b, 65536, 16);
    lg_acc    = Loot_Bits(b, 1048576, 16);
};


float() Loot_PackA =
{
    return lg_type + lg_mfr * 8 + lg_rarity * 64 + lg_elem * 512 + lg_level * 4096;
};


float() Loot_PackB =
{
    return lg_body + lg_barrel * 16 + lg_grip * 256 + lg_mag * 4096 + lg_sight * 65536 + lg_acc * 1048576;
};

// ---------------------------------------------------------------------------
// Rolling a new gun
// ---------------------------------------------------------------------------

// result of Loot_Roll
float lr_a, lr_b, lr_s;

// relative chance of each rarity before bias
float(float r) Loot_RarityWeight =
{
    if (r == LR_COMMON)
    {
        return 100;
    }
    if (r == LR_UNCOMMON)
    {
        return 40;
    }
    if (r == LR_RARE)
    {
        return 15;
    }
    if (r == LR_EPIC)
    {
        return 4;
    }
    return 0.75; // legendary
};


// bias 0 = normal enemy. Each +1 bias doubles the odds of every step up.
float(float bias) Loot_RollRarity =
{
    local float r, total, mult, u;

    total = 0;
    mult = 1;
    r = 0;
    while (r <= LR_LEGENDARY)
    {
        total = total + Loot_RarityWeight(r) * mult;
        mult = mult * (1 + bias);
        r = r + 1;
    }

    u = Rng_Next() * total;

    mult = 1;
    r = 0;
    while (r < LR_LEGENDARY)
    {
        u = u - Loot_RarityWeight(r) * mult;
        if (u < 0)
        {
            return r;
        }
        mult = mult * (1 + bias);
        r = r + 1;
    }
    return LR_LEGENDARY;
};


// Variants 0-3 are normal, 4-5 are premium.
// Common guns only see 0-3. Better rarity rolls several times and keeps
// the highest, so premium parts show up more often.
float(float rarity) Loot_RollPart =
{
    local float best, v, n;

    if (rarity == LR_COMMON)
    {
        return Rng_Int(4);
    }
    if (rarity == LR_UNCOMMON)
    {
        return Rng_Int(5);
    }

    best = Rng_Int(LOOT_VARIANTS);
    n = rarity - LR_RARE;
    while (n > 0)
    {
        v = Rng_Int(LOOT_VARIANTS);
        if (v > best)
        {
            best = v;
        }
        n = n - 1;
    }
    return best;
};


// Accessories only appear on rare and better guns.
float(float rarity) Loot_RollAccessory =
{
    if (rarity < LR_RARE)
    {
        return 0;
    }
    if (rarity == LR_RARE)
    {
        if (Rng_Next() < 0.5)
        {
            return 0;
        }
    }
    return 1 + Rng_Int(LOOT_VARIANTS - 1);
};


/*
Loot_Roll

seed         0 .. 16777215, picks everything below
level        1 .. 127
bias         0 = normal, higher = better rarity odds
force_type   -1 = random, or LT_*
force_rarity -1 = roll it, or LR_*

Result goes into lr_a, lr_b, lr_s.
*/
void(float seed, float level, float bias, float force_type, float force_rarity) Loot_Roll =
{
    Rng_Seed(seed);

    lg_type = Rng_Int(LT_NUMTYPES);
    if (force_type >= 0)
    {
        lg_type = force_type;
    }

    lg_mfr = Rng_Int(LM_NUMMFRS);

    lg_rarity = Loot_RollRarity(bias);
    if (force_rarity >= 0)
    {
        lg_rarity = force_rarity;
    }

    lg_body   = Loot_RollPart(lg_rarity);
    lg_barrel = Loot_RollPart(lg_rarity);
    lg_grip   = Loot_RollPart(lg_rarity);
    lg_mag    = Loot_RollPart(lg_rarity);
    lg_sight  = Loot_RollPart(lg_rarity);
    lg_acc    = Loot_RollAccessory(lg_rarity);

    lg_elem = 0; // elements come in a later phase

    if (level < 1)
    {
        level = 1;
    }
    if (level > 127)
    {
        level = 127;
    }
    lg_level = floor(level);

    lr_a = Loot_PackA();
    lr_b = Loot_PackB();
    lr_s = floor(seed);
};

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

// result of Loot_Stats
float ls_damage;  // per pellet / per projectile
float ls_pellets; // pellets (hitscan) or projectiles per shot
float ls_delay;   // seconds between trigger pulls
float ls_spread;  // 0 = laser accurate
float ls_mag;     // magazine size
float ls_reload;  // seconds
float ls_ammo;    // LA_*
float ls_proj;    // LP_*
float ls_speed;   // projectile speed
float ls_splash;  // extra explosion, as a fraction of the shot's damage
float ls_burst;   // shots per trigger pull
float ls_beam;    // 1 = draw a beam tracer (sniper)
float ls_dps;     // damage per second, for comparing guns

void(float a, float b, float s) Loot_Stats =
{
    local float dmg, pel, del, spr, mag, rel, spd, u;

    Loot_Unpack(a, b);

    // ---- base stats by type ----
    ls_proj = LP_HITSCAN;
    spd = 0;
    ls_splash = 0;
    ls_burst = 1;
    ls_beam = 0;

    if (lg_type == LT_PISTOL)
    {
        dmg = 14; pel = 1; del = 0.35; spr = 0.02; mag = 12; rel = 1.2;
        ls_ammo = LA_SHELLS;
    }
    else if (lg_type == LT_SMG)
    {
        dmg = 7; pel = 1; del = 0.1; spr = 0.05; mag = 30; rel = 1.5;
        ls_ammo = LA_NAILS;
        ls_proj = LP_SPIKE;
        spd = 1000;
    }
    else if (lg_type == LT_SHOTGUN)
    {
        dmg = 5; pel = 8; del = 0.75; spr = 0.12; mag = 6; rel = 2;
        ls_ammo = LA_SHELLS;
    }
    else if (lg_type == LT_RIFLE)
    {
        dmg = 10; pel = 1; del = 0.13; spr = 0.03; mag = 24; rel = 1.8;
        ls_ammo = LA_NAILS;
    }
    else if (lg_type == LT_SNIPER)
    {
        dmg = 60; pel = 1; del = 1.1; spr = 0.004; mag = 5; rel = 2.2;
        ls_ammo = LA_CELLS;
        ls_beam = 1;
    }
    else // launcher
    {
        dmg = 90; pel = 1; del = 0.9; spr = 0.01; mag = 3; rel = 2.5;
        ls_ammo = LA_ROCKETS;
        ls_proj = LP_ROCKET;
        spd = 1000;
    }

    // ---- manufacturer rule ----
    if (lg_mfr == LM_GRAVEMARK)
    {
        dmg = dmg * 1.35; del = del * 1.3; mag = mag * 0.75;
    }
    else if (lg_mfr == LM_BLACKVEIN)
    {
        dmg = dmg * 0.85; del = del * 0.8; spr = spr * 1.6; mag = mag * 1.8;
    }
    else if (lg_mfr == LM_HEXWRIGHT)
    {
        dmg = dmg * 1.1; spr = spr * 0.4; rel = rel * 1.2;
    }
    else if (lg_mfr == LM_RUINHAUS)
    {
        dmg = dmg * 0.9;
        ls_splash = 0.5;
    }
    else if (lg_mfr == LM_SCRAPJAW)
    {
        dmg = dmg * 0.9; del = del * 0.9; rel = rel * 0.55;
    }
    else // vexcorp
    {
        dmg = dmg * 0.85; del = del * 1.6;
        ls_burst = 3;
    }

    // ---- body ----
    if (lg_body == 1)      { dmg = dmg * 1.25; del = del * 1.2; }  // heavy
    else if (lg_body == 2) { dmg = dmg * 0.85; del = del * 0.75; } // light
    else if (lg_body == 3) { dmg = dmg * 1.1; del = del * 0.9; mag = mag * 0.8; } // overclocked
    else if (lg_body == 4) { dmg = dmg * 1.15; rel = rel * 1.15; } // reinforced
    else if (lg_body == 5) { dmg = dmg * 1.2; }                    // masterwork

    // ---- barrel: these change how the gun FEELS ----
    if (lg_barrel == 1) // long
    {
        spr = spr * 0.6; spd = spd * 1.3;
    }
    else if (lg_barrel == 2) // stubby
    {
        spr = spr * 1.4; dmg = dmg * 1.1;
        if (lg_type == LT_LAUNCHER)
        {
            ls_proj = LP_GRENADE; // stubby launchers lob grenades
            spd = 600;
        }
    }
    else if (lg_barrel == 3) // splitting: twice the pellets, each weaker
    {
        pel = pel * 2; dmg = dmg * 0.6; spr = spr * 1.2;
    }
    else if (lg_barrel == 4) // hulking
    {
        dmg = dmg * 1.25; del = del * 1.15;
    }
    else if (lg_barrel == 5) // rifled
    {
        spr = spr * 0.75; dmg = dmg * 1.1;
    }

    // ---- grip ----
    if (lg_grip == 1)      { spr = spr * 0.8; }                    // steady
    else if (lg_grip == 2) { rel = rel * 0.75; }                   // quick
    else if (lg_grip == 3) { dmg = dmg * 1.1; spr = spr * 1.15; }  // brutal
    else if (lg_grip == 4) { del = del * 0.9; }                    // feather
    else if (lg_grip == 5) { dmg = dmg * 1.15; }                   // savage

    // ---- magazine ----
    if (lg_mag == 1)      { mag = mag * 2; rel = rel * 1.4; }      // drum
    else if (lg_mag == 2) { mag = mag * 0.6; rel = rel * 0.6; }    // compact
    else if (lg_mag == 3) { mag = mag * 1.4; }                     // extended
    else if (lg_mag == 4) { rel = rel * 0.8; }                     // rapid
    else if (lg_mag == 5) { mag = mag * 1.7; rel = rel * 0.9; }    // bottomless

    // ---- sight ----
    if (lg_sight == 1)      { spr = spr * 0.9; }                   // dot
    else if (lg_sight == 2) { spr = spr * 0.65; del = del * 1.1; } // scope
    else if (lg_sight == 3) { spr = spr * 0.85; dmg = dmg * 1.05; }// holo
    else if (lg_sight == 4) { spr = spr * 0.75; }                  // laser
    else if (lg_sight == 5) { spr = spr * 0.7; dmg = dmg * 1.1; }  // seer

    // ---- accessory (rare and better) ----
    if (lg_acc == 1)      { dmg = dmg * 1.2; }                     // serrated
    else if (lg_acc == 2) { del = del * 0.8; }                     // hairtrigger
    else if (lg_acc == 3) { spr = spr * 0.6; }                     // stabilizer
    else if (lg_acc == 4) { pel = pel + 1; }                       // overcharged
    else if (lg_acc == 5) { rel = rel * 0.65; }                    // swift

    // ---- rarity and level ----
    dmg = dmg * (1 + 0.1 * lg_rarity);
    mag = mag * (1 + 0.05 * lg_rarity);
    dmg = dmg * (1 + 0.12 * (lg_level - 1));

    // ---- small wobble from the seed, so two "same" guns still differ ----
    // skip past the numbers Loot_Roll used for this seed
    Rng_Seed(s);
    u = 0;
    while (u < 24)
    {
        Rng_Next();
        u = u + 1;
    }
    dmg = dmg * (0.92 + 0.16 * Rng_Next());
    del = del * (0.95 + 0.1 * Rng_Next());
    spr = spr * (0.9 + 0.2 * Rng_Next());

    // ---- round and clamp ----
    ls_damage = floor(dmg + 0.5);
    if (ls_damage < 1)
    {
        ls_damage = 1;
    }
    ls_pellets = pel;
    ls_delay = del;
    if (ls_delay < 0.06)
    {
        ls_delay = 0.06;
    }
    ls_spread = spr;
    ls_mag = floor(mag + 0.5);
    if (ls_mag < 1)
    {
        ls_mag = 1;
    }
    ls_reload = rel;
    ls_speed = spd;

    // splash adds damage too, so count it in DPS
    ls_dps = ls_damage * ls_pellets * ls_burst * (1 + ls_splash) / ls_delay;
    ls_dps = floor(ls_dps + 0.5);

    // leave the lg_* globals describing this gun for the caller
    Loot_Unpack(a, b);
};

// ---------------------------------------------------------------------------
// Names and text
// ---------------------------------------------------------------------------

// Join strings. strcat() is only called with 2 arguments so the same
// code also runs in the headless test (its strcat takes exactly 2).
string(string a, string b, string c) Loot_S3 =
{
    return strcat(strcat(a, b), c);
};


string(string a, string b, string c, string d) Loot_S4 =
{
    return strcat(strcat(a, b), strcat(c, d));
};


string(string a, string b, string c, string d, string e) Loot_S5 =
{
    return strcat(Loot_S4(a, b, c, d), e);
};


string(string a, string b, string c, string d, string e, string f) Loot_S6 =
{
    return strcat(Loot_S4(a, b, c, d), strcat(e, f));
};


string(float t) Loot_TypeName =
{
    if (t == LT_PISTOL)
    {
        return "Pistol";
    }
    if (t == LT_SMG)
    {
        return "SMG";
    }
    if (t == LT_SHOTGUN)
    {
        return "Shotgun";
    }
    if (t == LT_RIFLE)
    {
        return "Rifle";
    }
    if (t == LT_SNIPER)
    {
        return "Sniper";
    }
    return "Launcher";
};


string(float m) Loot_MfrName =
{
    if (m == LM_GRAVEMARK)
    {
        return "Gravemark";
    }
    if (m == LM_BLACKVEIN)
    {
        return "Blackvein";
    }
    if (m == LM_HEXWRIGHT)
    {
        return "Hexwright";
    }
    if (m == LM_RUINHAUS)
    {
        return "Ruinhaus";
    }
    if (m == LM_SCRAPJAW)
    {
        return "Scrapjaw";
    }
    return "Vexcorp";
};


string(float m) Loot_MfrRule =
{
    if (m == LM_GRAVEMARK)
    {
        return "Gravemark: hits hard, fires slow";
    }
    if (m == LM_BLACKVEIN)
    {
        return "Blackvein: huge mag, wild spray";
    }
    if (m == LM_HEXWRIGHT)
    {
        return "Hexwright: pinpoint accuracy";
    }
    if (m == LM_RUINHAUS)
    {
        return "Ruinhaus: every hit explodes";
    }
    if (m == LM_SCRAPJAW)
    {
        return "Scrapjaw: lightning reloads";
    }
    return "Vexcorp: 3-round burst";
};


string(float r) Loot_RarityName =
{
    if (r == LR_COMMON)
    {
        return "Common";
    }
    if (r == LR_UNCOMMON)
    {
        return "Uncommon";
    }
    if (r == LR_RARE)
    {
        return "Rare";
    }
    if (r == LR_EPIC)
    {
        return "Epic";
    }
    return "Legendary";
};


// one prefix word per part variant (variant 0 = plain, no word)
string(float part, float v) Loot_PartWord =
{
    if (v == 0)
    {
        return "";
    }
    if (part == 0) // body
    {
        if (v == 1) return "Heavy";
        if (v == 2) return "Light";
        if (v == 3) return "Overclocked";
        if (v == 4) return "Reinforced";
        return "Masterwork";
    }
    if (part == 1) // barrel
    {
        if (v == 1) return "Long";
        if (v == 2) return "Stubby";
        if (v == 3) return "Splitting";
        if (v == 4) return "Hulking";
        return "Rifled";
    }
    if (part == 2) // grip
    {
        if (v == 1) return "Steady";
        if (v == 2) return "Quick";
        if (v == 3) return "Brutal";
        if (v == 4) return "Feather";
        return "Savage";
    }
    if (part == 3) // magazine
    {
        if (v == 1) return "Drum";
        if (v == 2) return "Compact";
        if (v == 3) return "Extended";
        if (v == 4) return "Rapid";
        return "Bottomless";
    }
    if (part == 4) // sight
    {
        if (v == 1) return "Dotted";
        if (v == 2) return "Scoped";
        if (v == 3) return "Holo";
        if (v == 4) return "Laser";
        return "Seer's";
    }
    // accessory
    if (v == 1) return "Serrated";
    if (v == 2) return "Hairtrigger";
    if (v == 3) return "Stable";
    if (v == 4) return "Overcharged";
    return "Swift";
};


// The prefix comes from the most interesting part:
// accessory first, then the barrel (it changes feel the most),
// then whichever other part has the highest variant.
string() Loot_Prefix =
{
    local float bestpart, bestv;

    if (lg_acc)
    {
        return Loot_PartWord(5, lg_acc);
    }
    if (lg_barrel)
    {
        return Loot_PartWord(1, lg_barrel);
    }

    bestpart = 0;
    bestv = lg_body;
    if (lg_grip > bestv)
    {
        bestpart = 2;
        bestv = lg_grip;
    }
    if (lg_sight > bestv)
    {
        bestpart = 4;
        bestv = lg_sight;
    }
    if (lg_mag > bestv)
    {
        bestpart = 3;
        bestv = lg_mag;
    }
    return Loot_PartWord(bestpart, bestv);
};


// call after Loot_Unpack or Loot_Stats
string() Loot_Name =
{
    local string p;

    p = Loot_Prefix();
    if (p == "")
    {
        return Loot_S3(Loot_MfrName(lg_mfr), " ", Loot_TypeName(lg_type));
    }
    return Loot_S5(p, " ", Loot_MfrName(lg_mfr), " ", Loot_TypeName(lg_type));
};


// number with one decimal, e.g. 2.5
string(float x) Loot_Fmt1 =
{
    local float n, whole;

    n = floor(x * 10 + 0.5);
    whole = floor(n / 10);
    return Loot_S3(ftos(whole), ".", ftos(n - whole * 10));
};


// 0..100, higher is better. Only for display.
float(float spread) Loot_Accuracy =
{
    local float acc;

    acc = floor(100 - spread * 500 + 0.5);
    if (acc < 0)
    {
        acc = 0;
    }
    if (acc > 100)
    {
        acc = 100;
    }
    return acc;
};


/*
Loot_Card

The text box shown when you look at a gun on the floor.
compare_dps < 0 means "nothing to compare with".
*/
string(float a, float b, float s, float compare_dps) Loot_Card =
{
    local string t, line;
    local float diff;

    Loot_Stats(a, b, s);

    t = strcat(Loot_Name(), "\n");
    t = Loot_S5(t, Loot_RarityName(lg_rarity), "  Lv ", ftos(lg_level), "\n\n");

    line = strcat("Damage ", ftos(ls_damage));
    if (ls_pellets > 1)
    {
        line = Loot_S3(line, " x", ftos(ls_pellets));
    }
    if (ls_burst > 1)
    {
        line = Loot_S4(line, " (burst ", ftos(ls_burst), ")");
    }
    t = Loot_S3(t, line, "\n");

    t = Loot_S4(t, "Fire rate ", Loot_Fmt1(1 / ls_delay), "/s\n");
    t = Loot_S4(t, "Accuracy ", ftos(Loot_Accuracy(ls_spread)), "%\n");
    t = Loot_S6(t, "Mag ", ftos(ls_mag), "   Reload ", Loot_Fmt1(ls_reload), "s\n");

    line = strcat("DPS ", ftos(ls_dps));
    if (compare_dps >= 0)
    {
        diff = ls_dps - compare_dps;
        if (diff >= 0)
        {
            line = Loot_S4(line, "  (+", ftos(diff), ")");
        }
        else
        {
            line = Loot_S4(line, "  (", ftos(diff), ")");
        }
    }
    t = Loot_S3(t, line, "\n\n");

    t = Loot_S3(t, Loot_MfrRule(lg_mfr), "\n");
    if (lg_type == LT_LAUNCHER && lg_barrel == 2)
    {
        t = strcat(t, "Lobs bouncing grenades\n");
    }
    return t;
};
