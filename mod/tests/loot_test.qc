/*
Headless checks for loot_core.qc.

Prints a report and ends with "RESULT: PASS" or "RESULT: FAIL".
*/

float failures;

void(string what) Fail =
{
    print("FAIL: ", what, "\n");
    failures = failures + 1;
};


// the test seeds: whole numbers spread over the full 0..2^24 range
float(float i) TestSeed =
{
    return Loot_Mod(i * 811 + 5, 16777216);
};


void() TestDeterminism =
{
    local float i, a, b, d, del, m;

    i = 0;
    while (i < 1000)
    {
        Loot_Roll(TestSeed(i), 1 + Loot_Mod(i, 50), 0, -1, -1);
        a = lr_a;
        b = lr_b;
        Loot_Stats(lr_a, lr_b, lr_s);
        d = ls_damage;
        del = ls_delay;
        m = ls_mag;

        Loot_Roll(TestSeed(i), 1 + Loot_Mod(i, 50), 0, -1, -1);
        Loot_Stats(lr_a, lr_b, lr_s);
        if (lr_a != a || lr_b != b || ls_damage != d || ls_delay != del || ls_mag != m)
        {
            Fail(strcat("same seed gave a different gun, i=", ftos(i)));
            return;
        }
        i = i + 1;
    }
    print("determinism: 1000 seeds rolled twice, identical\n");
};


void() TestPacking =
{
    local float i;

    i = 0;
    while (i < 1000)
    {
        Loot_Roll(TestSeed(i), 1 + Loot_Mod(i, 127), 3, -1, -1);
        Loot_Unpack(lr_a, lr_b);
        if (Loot_PackA() != lr_a || Loot_PackB() != lr_b)
        {
            Fail(strcat("pack/unpack mismatch, i=", ftos(i)));
            return;
        }
        if (lr_a >= 16777216 || lr_b >= 16777216)
        {
            Fail("packed value above 2^24");
            return;
        }
        if (lg_type >= LT_NUMTYPES || lg_mfr >= LM_NUMMFRS || lg_rarity > LR_LEGENDARY)
        {
            Fail("type/mfr/rarity out of range");
            return;
        }
        if (lg_body >= 6 || lg_barrel >= 6 || lg_grip >= 6 || lg_mag >= 6 || lg_sight >= 6 || lg_acc >= 6)
        {
            Fail("part out of range");
            return;
        }
        if (lg_level != 1 + Loot_Mod(i, 127))
        {
            Fail("level not kept");
            return;
        }
        i = i + 1;
    }
    print("packing: 1000 guns round-trip exactly\n");
};


void(float bias, float n) TestRarity =
{
    local float i, c0, c1, c2, c3, c4;

    i = 0;
    c0 = c1 = c2 = c3 = c4 = 0;
    while (i < n)
    {
        Loot_Roll(TestSeed(i), 1, bias, -1, -1);
        Loot_Unpack(lr_a, lr_b);
        if (lg_rarity == 0) c0 = c0 + 1;
        else if (lg_rarity == 1) c1 = c1 + 1;
        else if (lg_rarity == 2) c2 = c2 + 1;
        else if (lg_rarity == 3) c3 = c3 + 1;
        else c4 = c4 + 1;
        i = i + 1;
    }
    print("rarity bias ", ftos(bias), " (", ftos(n), " rolls): ");
    print("common ", ftos(c0), "  uncommon ", ftos(c1), "  rare ", ftos(c2));
    print("  epic ", ftos(c3), "  legendary ", ftos(c4), "\n");

    // bias 0 expected share: 100/159.75 = 62.6%, legendary 0.47%
    if (bias == 0)
    {
        if (c0 < n * 0.58 || c0 > n * 0.67)
        {
            Fail("common share far from 62.6%");
        }
        if (c4 < 1)
        {
            Fail("no legendary in a large sample");
        }
        if (c4 > n * 0.01)
        {
            Fail("legendary too common");
        }
    }
};


void() TestForced =
{
    local float i;

    i = 0;
    while (i < 500)
    {
        Loot_Roll(TestSeed(i), 1, 0, LT_PISTOL, LR_COMMON);
        Loot_Unpack(lr_a, lr_b);
        if (lg_type != LT_PISTOL || lg_rarity != LR_COMMON || lg_acc != 0)
        {
            Fail("forced common pistol was not a common pistol");
            return;
        }
        i = i + 1;
    }
    print("forced type/rarity: ok\n");
};


// stat ranges per type: catches broken numbers (zero mag, negative delay)
void(float t) TestType =
{
    local float i, lo, hi, distinct, last;

    i = 0;
    lo = 1000000;
    hi = 0;
    while (i < 1500)
    {
        Loot_Roll(TestSeed(i), 1, 1, t, -1);
        Loot_Stats(lr_a, lr_b, lr_s);
        if (ls_damage < 1 || ls_mag < 1 || ls_delay < 0.06 || ls_spread < 0 || ls_reload <= 0 || ls_pellets < 1)
        {
            Fail(strcat("bad stats for ", Loot_TypeName(t)));
            return;
        }
        if (ls_dps < lo)
        {
            lo = ls_dps;
        }
        if (ls_dps > hi)
        {
            hi = ls_dps;
        }
        i = i + 1;
    }
    print(Loot_TypeName(t), ": DPS at level 1 ranges ", ftos(lo), " .. ", ftos(hi), "\n");
};


/*
qcvm stops any run after 1,000,000 jumps, so the tests are split into
groups. The build script runs each group as its own qcvm run:
    qcvm loot_test.dat -float <group>
*/
void(float group) main =
{
    local float i;

    failures = 0;

    if (group == 0)
    {
        TestDeterminism();
    }
    else if (group == 1)
    {
        TestPacking();
        TestForced();
    }
    else if (group == 2)
    {
        TestRarity(0, 4000);
    }
    else if (group == 3)
    {
        TestRarity(1, 4000);
    }
    else if (group == 4)
    {
        TestRarity(3, 4000);
    }
    else if (group >= 5 && group < 5 + LT_NUMTYPES)
    {
        TestType(group - 5);
    }
    else
    {
        print("\n=== sample guns ===\n");
        i = 0;
        while (i < 6)
        {
            Loot_Roll(TestSeed(i * 97 + 3), 1 + i * 4, 2, -1, -1);
            print(Loot_Card(lr_a, lr_b, lr_s, 60), "\n");
            i = i + 1;
        }
    }

    if (failures)
    {
        print("RESULT: FAIL (", ftos(failures), ")\n");
    }
    else
    {
        print("RESULT: PASS\n");
    }
};
