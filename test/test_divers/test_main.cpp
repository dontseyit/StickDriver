// -----------------------------------------------------------------------------
//  Host tests for the game.  pio test -e native -f test_divers
//
//  Two kinds of test here. The pure functions - the ramp, the scoring table,
//  the tilt mapping - are checked directly, because they are the numbers the
//  game is tuned with and a typo in one is invisible on a 135 px panel.
//
//  The rest are INVARIANTS watched over a long simulated game: only one player
//  bullet, never more divers than the wave allows, nothing in formation ever
//  shoots, no alien dying twice. A frame-by-frame invariant catches what an
//  end-state assertion cannot, since almost any bug still ends with a dead
//  player eventually.
// -----------------------------------------------------------------------------
#include <unity.h>

#include <cmath>
#include <cstring>

#include "mathx/Divers.h"

using namespace sd;

namespace {

constexpr float kStep = 1.0f / 60.0f;

/// The panel, so a test can say "on the panel" without repeating the numbers.
constexpr float kLeft = 0.0f;
constexpr float kRight = static_cast<float>(Divers::kFieldW);

int countState(const Divers& g, Divers::AlienState s) {
    int n = 0;
    for (int i = 0; i < Divers::kMaxAliens; ++i)
        if (g.alien(i).state == s) ++n;
    return n;
}

int liveAlienShots(const Divers& g) {
    int n = 0;
    for (int i = 0; i < Divers::kMaxAlienShots; ++i)
        if (g.alienShot(i).alive) ++n;
    return n;
}

/// Runs `seconds` of game with the ship parked, and shouts if any invariant
/// breaks on any frame. Returns the frames run.
struct Watcher {
    int  maxDivers      = 0;
    int  maxAlienShots  = 0;
    int  worstDrop      = 0;   ///< most aliens lost in one frame
    bool shotWhileEmpty = false;   ///< a shot appeared with nothing diving
    bool leftField      = false;   ///< a formation alien off the panel
};

Watcher run(Divers& g, float seconds, float shipX) {
    Watcher w;
    int prevAlive = g.alive();
    int prevShots = liveAlienShots(g);

    const int frames = static_cast<int>(seconds / kStep);
    for (int f = 0; f < frames; ++f) {
        const int divingBefore = g.diving();
        g.update(kStep, shipX);

        const int nowAlive = g.alive();
        if (prevAlive - nowAlive > w.worstDrop) w.worstDrop = prevAlive - nowAlive;
        prevAlive = nowAlive;

        const int shots = liveAlienShots(g);
        if (shots > prevShots && divingBefore == 0 && g.diving() == 0)
            w.shotWhileEmpty = true;
        prevShots = shots;

        if (g.diving() > w.maxDivers) w.maxDivers = g.diving();
        if (shots > w.maxAlienShots) w.maxAlienShots = shots;

        for (int i = 0; i < Divers::kMaxAliens; ++i) {
            const Divers::Alien& a = g.alien(i);
            if (a.state != Divers::AlienState::Formation) continue;
            if (a.x - Divers::kAlienHalfW < kLeft ||
                a.x + Divers::kAlienHalfW > kRight)
                w.leftField = true;
        }
    }
    return w;
}

/// A player good enough to clear a wave: steps away from the nearest thing
/// on a collision course and otherwise walks under whatever is left. Crude,
/// but it shoots and it dodges, which is what the wave rule needs to be shown.
float botX(const Divers& g, float x) {
    float nearest = 1e9f, threatX = 0.0f;
    for (int i = 0; i < Divers::kMaxAlienShots; ++i) {
        const Divers::Shot& s = g.alienShot(i);
        if (!s.alive) continue;
        const float d = Divers::kShipY - s.y;
        if (d > 0.0f && d < nearest && std::fabs(s.x - x) < 26.0f) {
            nearest = d; threatX = s.x;
        }
    }
    for (int i = 0; i < Divers::kMaxAliens; ++i) {
        const Divers::Alien& a = g.alien(i);
        if (a.state != Divers::AlienState::Diving) continue;
        const float d = Divers::kShipY - a.y;
        if (d > 0.0f && d < nearest && std::fabs(a.x - x) < 26.0f) {
            nearest = d; threatX = a.x;
        }
    }
    if (nearest < 1e8f) return x + (x < threatX ? -210.0f : 210.0f) * kStep;

    // Nothing coming: line up on the fullest column.
    float target = x;
    int   most   = -1;
    for (int c = 0; c < Divers::kCols; ++c) {
        int n = 0; float cx = x;
        for (int i = 0; i < Divers::kMaxAliens; ++i) {
            const Divers::Alien& a = g.alien(i);
            if (a.state == Divers::AlienState::Dead || a.col != c) continue;
            ++n; cx = a.x;
        }
        if (n > most) { most = n; target = cx; }
    }
    if (std::fabs(target - x) < 2.0f) return x;
    return x + (target > x ? 60.0f : -60.0f) * kStep;
}

int liveCapsules(const Divers& g) {
    int n = 0;
    for (int i = 0; i < Divers::kMaxCapsules; ++i)
        if (g.capsule(i).alive) ++n;
    return n;
}

/// Plays until a capsule is falling, then flies the ship under it and catches
/// it. Returns false if no capsule turned up in the time allowed.
bool catchOne(Divers& g, Divers::PowerKind want, int maxFrames = 60 * 600) {
    float x = Divers::kFieldW * 0.5f;
    for (int f = 0; f < maxFrames && !g.over(); ++f) {
        int target = -1;
        for (int i = 0; i < Divers::kMaxCapsules; ++i)
            if (g.capsule(i).alive && g.capsule(i).kind == want) target = i;

        x = (target < 0) ? botX(g, x) : g.capsule(target).x;
        g.update(kStep, x);
        x = g.shipX();

        const bool tiered = (want == Divers::PowerKind::Width)
                                ? g.widthTier() > 1
                                : g.rateTier() > 1;
        if (tiered) return true;
    }
    return false;
}

/// Plays until a flagship is diving with at least `wantEscorts` escorts on
/// it, and returns its index, or -1. Tries a few seeds, because whether a
/// flagship gets its turn before the gun reaches it is down to the draw.
///
/// This helper exists because the escort group is the hardest thing in the
/// game to reach from outside: a ship parked at the centre shoots the
/// flagship column first and the path never runs at all.
int flyToFlagshipDive(Divers& g, float& x, int wantEscorts) {
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        g.reset(seed * 7);
        x = Divers::kFieldW * 0.5f;
        for (int f = 0; f < 60 * 400 && !g.over(); ++f) {
            x = botX(g, x);
            g.update(kStep, x);
            x = g.shipX();

            for (int i = 0; i < Divers::kMaxAliens; ++i) {
                const Divers::Alien& a = g.alien(i);
                if (a.kind != Divers::AlienKind::Flagship) continue;
                if (a.state != Divers::AlienState::Diving) continue;
                int esc = 0;
                for (int j = 0; j < Divers::kMaxAliens; ++j)
                    if (g.alien(j).leader == i &&
                        g.alien(j).state == Divers::AlienState::Diving) ++esc;
                if (esc >= wantEscorts) return i;
            }
        }
    }
    return -1;
}

}  // namespace

// -- the formation ------------------------------------------------------------

void test_a_new_formation_is_the_shape_the_table_draws() {
    Divers g;
    g.reset(1);

    const Divers::Shape& sh = g.shape();
    TEST_ASSERT_EQUAL_INT(Divers::shapeSize(sh), g.alive());

    // Every alien sits on a cell the picture marked, and every marked cell
    // has an alien on it. A shape with a typo in it fails here.
    bool seen[Divers::kRows][Divers::kCols] = {};
    for (int i = 0; i < Divers::kMaxAliens; ++i) {
        const Divers::Alien& a = g.alien(i);
        if (a.state == Divers::AlienState::Dead) continue;
        TEST_ASSERT_TRUE(a.row < Divers::kRows);
        TEST_ASSERT_TRUE(a.col < Divers::kCols);
        TEST_ASSERT_TRUE(sh.row[a.row][a.col] != '.');
        TEST_ASSERT_FALSE(seen[a.row][a.col]);
        seen[a.row][a.col] = true;
    }
    for (int r = 0; r < Divers::kRows; ++r)
        for (int c = 0; c < Divers::kCols; ++c)
            if (sh.row[r][c] != '.') TEST_ASSERT_TRUE(seen[r][c]);
}

void test_every_shape_is_well_formed() {
    for (int i = 0; i < Divers::kShapeCount; ++i) {
        const Divers::Shape& sh = Divers::kShapes[i];
        TEST_ASSERT_NOT_NULL(sh.name);
        for (int r = 0; r < Divers::kRows; ++r) {
            // Exactly ten columns. A row one character short would silently
            // read the next row's memory through the terminator.
            TEST_ASSERT_EQUAL_INT(Divers::kCols,
                                  static_cast<int>(std::strlen(sh.row[r])));
            for (int c = 0; c < Divers::kCols; ++c) {
                const char ch = sh.row[r][c];
                TEST_ASSERT_TRUE(ch == '.' || ch == 'F' || ch == 'R' ||
                                 ch == 'P' || ch == 'B');
            }
        }
        // Something to shoot, and room in the array for it.
        TEST_ASSERT_GREATER_THAN_INT(15, Divers::shapeSize(sh));
        TEST_ASSERT_LESS_OR_EQUAL_INT(Divers::kMaxAliens,
                                      Divers::shapeSize(sh));
    }
}

void test_the_shape_cycles_with_the_wave() {
    TEST_ASSERT_EQUAL_STRING(Divers::kShapes[0].name, Divers::shapeFor(1).name);
    TEST_ASSERT_EQUAL_STRING(Divers::kShapes[2].name, Divers::shapeFor(3).name);
    // Six waves to a cycle, and the ramp underneath is what keeps wave 7 from
    // being wave 1 again.
    TEST_ASSERT_EQUAL_STRING(Divers::shapeFor(1).name, Divers::shapeFor(7).name);
    TEST_ASSERT_TRUE(Divers::waveSpec(7).diveSpeed > Divers::waveSpec(1).diveSpeed);
}

void test_the_formation_starts_below_the_hud_and_above_the_ship() {
    Divers g;
    g.reset(1);
    for (int i = 0; i < Divers::kMaxAliens; ++i) {
        const Divers::Alien& a = g.alien(i);
        if (a.state == Divers::AlienState::Dead) continue;   // an unused slot
        TEST_ASSERT_TRUE(a.y - Divers::kAlienHalfH > Divers::kHudH);
        TEST_ASSERT_TRUE(a.y + Divers::kAlienHalfH < Divers::kShipY);
    }
}

void test_no_shape_reaches_the_hud_or_the_ship() {
    // Wave 1 only proves the wedge. The bow and the diamond use the sixth
    // row, which is the one that could reach down into the ship.
    for (int w = 1; w <= Divers::kShapeCount; ++w) {
        Divers g;
        g.reset(1);
        for (int i = 0; i < Divers::kRows; ++i) {
            const char* r = Divers::shapeFor(w).row[i];
            for (int c = 0; c < Divers::kCols; ++c) {
                if (r[c] == '.') continue;
                const float y = Divers::kFormTop + i * Divers::kRowPitch;
                TEST_ASSERT_TRUE(y - Divers::kAlienHalfH > Divers::kHudH);
                TEST_ASSERT_TRUE(y + Divers::kAlienHalfH < Divers::kCommitY);
            }
        }
    }
}

void test_the_sway_never_takes_the_formation_off_the_panel() {
    Divers g;
    g.reset(7);
    const Watcher w = run(g, 40.0f, Divers::kFieldW * 0.5f);
    TEST_ASSERT_FALSE(w.leftField);
}

// -- diving -------------------------------------------------------------------

void test_a_diver_leaves_the_formation_and_comes_back_round() {
    Divers g;
    g.reset(3);

    bool sawDiving = false, sawReturning = false, sawRejoin = false;
    // The ship sits in a corner and is never hit there for long enough to see
    // a full lap; a death would put the diver back by hand and prove nothing.
    for (int f = 0; f < 60 * 40; ++f) {
        g.update(kStep, Divers::kShipMargin);
        if (countState(g, Divers::AlienState::Diving) > 0) sawDiving = true;
        if (countState(g, Divers::AlienState::Returning) > 0) {
            sawReturning = true;
        } else if (sawReturning) {
            sawRejoin = true;   // it was coming back, now it is not: it arrived
            break;
        }
    }
    TEST_ASSERT_TRUE(sawDiving);
    TEST_ASSERT_TRUE(sawReturning);
    TEST_ASSERT_TRUE(sawRejoin);
}

void test_never_more_divers_than_the_wave_allows() {
    Divers g;
    g.reset(11);
    const Watcher w = run(g, 60.0f, Divers::kFieldW * 0.5f);
    // A flagship launch sends two escorts with it, so the budget is per
    // launch, not per body in the air.
    const int cap = Divers::waveSpec(g.wave()).maxDivers + 2;
    TEST_ASSERT_LESS_OR_EQUAL_INT(cap, w.maxDivers);
}

void test_only_divers_shoot() {
    Divers g;
    g.reset(5);
    const Watcher w = run(g, 60.0f, Divers::kFieldW * 0.5f);
    TEST_ASSERT_FALSE(w.shotWhileEmpty);
}

void test_alien_fire_stays_inside_its_buffer() {
    Divers g;
    g.reset(9);
    const Watcher w = run(g, 90.0f, Divers::kFieldW * 0.5f);
    TEST_ASSERT_LESS_OR_EQUAL_INT(Divers::kMaxAlienShots, w.maxAlienShots);
}

// -- shooting -----------------------------------------------------------------

void test_the_gun_keeps_firing() {
    // One volley at a time is a rule the class enforces by construction, so
    // the test that matters is that the volley is REUSED: a gun that never
    // fires again also has one volley in the air.
    Divers g;
    g.reset(2);

    int fired = 0;
    bool wasAlive = false;
    for (int f = 0; f < 60 * 20; ++f) {
        g.update(kStep, Divers::kFieldW * 0.5f);
        const bool now = g.volleyAlive();
        if (now && !wasAlive) ++fired;
        wasAlive = now;
    }
    TEST_ASSERT_GREATER_THAN_INT(10, fired);
}

void test_the_gun_waits_the_reload_however_the_volley_was_spent() {
    // The reload IS the fire rate, and the fire rate is the difficulty. Both
    // ways a volley can end have to pay it: a pellet that leaves the top of
    // the panel, and a pellet that hits something. Those used to be two
    // separate lines of code, and deleting either one changed the fire rate
    // without failing a single test.
    //
    // The ship is flown rather than parked, because a parked one hits with
    // every shot and the miss path never runs.
    Divers g;
    g.reset(19);

    float x = Divers::kFieldW * 0.5f;
    bool  was = false, wasHit = false;
    int   died = -1, gapHit = -1, gapMiss = -1;

    for (int f = 0; f < 60 * 120 && (gapHit < 0 || gapMiss < 0); ++f) {
        const int before = g.score();
        x = botX(g, x);
        g.update(kStep, x);
        x = g.shipX();

        if (g.phase() != Divers::Phase::Playing) {
            was = g.volleyAlive();
            continue;
        }
        const bool now = g.volleyAlive();
        if (was && !now) {
            died   = f;
            wasHit = g.score() > before;
        } else if (!was && now && died >= 0) {
            (wasHit ? gapHit : gapMiss) = f - died;
        }
        was = now;
    }

    TEST_ASSERT_GREATER_THAN_INT(0, gapHit);
    TEST_ASSERT_GREATER_THAN_INT(0, gapMiss);

    // Six frames of cooldown at 60 Hz, plus the frame it is set on.
    const int want = static_cast<int>(Divers::kRateCooldownSec[0] / kStep) + 1;
    TEST_ASSERT_INT_WITHIN(2, want, gapHit);
    TEST_ASSERT_INT_WITHIN(2, want, gapMiss);
}

void test_the_bullet_travels_up_from_the_ship() {
    Divers g;
    g.reset(2);
    // Past the ready pause, so the gun is live.
    for (int f = 0; f < 60 * 2; ++f) g.update(kStep, 40.0f);

    while (!g.volleyAlive()) g.update(kStep, 40.0f);
    const float y0 = g.playerShot(0).y;
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 40.0f, g.playerShot(0).x);
    g.update(kStep, 40.0f);
    TEST_ASSERT_TRUE(g.playerShot(0).y < y0);
}

void test_aliens_only_ever_die_one_at_a_time() {
    Divers g;
    g.reset(13);
    const Watcher w = run(g, 120.0f, Divers::kFieldW * 0.5f);
    TEST_ASSERT_LESS_OR_EQUAL_INT(1, w.worstDrop);
}

void test_shooting_the_formation_scores() {
    Divers g;
    g.reset(4);
    run(g, 30.0f, Divers::kFieldW * 0.5f);
    TEST_ASSERT_GREATER_THAN_INT(0, g.score());
    TEST_ASSERT_LESS_THAN_INT(Divers::kMaxAliens, g.alive());
}

// -- scoring ------------------------------------------------------------------

void test_the_scoring_table_is_the_arcades() {
    using K = Divers::AlienKind;
    TEST_ASSERT_EQUAL_INT(30, Divers::scoreFor(K::Blue, false, 0));
    TEST_ASSERT_EQUAL_INT(60, Divers::scoreFor(K::Blue, true, 0));
    TEST_ASSERT_EQUAL_INT(40, Divers::scoreFor(K::Purple, false, 0));
    TEST_ASSERT_EQUAL_INT(80, Divers::scoreFor(K::Purple, true, 0));
    TEST_ASSERT_EQUAL_INT(50, Divers::scoreFor(K::Red, false, 0));
    TEST_ASSERT_EQUAL_INT(100, Divers::scoreFor(K::Red, true, 0));
}

void test_a_flagship_is_worth_more_with_its_escorts_alive() {
    using K = Divers::AlienKind;
    TEST_ASSERT_EQUAL_INT(60, Divers::scoreFor(K::Flagship, false, 2));
    TEST_ASSERT_EQUAL_INT(150, Divers::scoreFor(K::Flagship, true, 0));
    TEST_ASSERT_EQUAL_INT(200, Divers::scoreFor(K::Flagship, true, 1));
    TEST_ASSERT_EQUAL_INT(300, Divers::scoreFor(K::Flagship, true, 2));
}

// -- the round ----------------------------------------------------------------

void test_three_lives_and_then_the_game_is_over() {
    Divers g;
    g.reset(17);
    TEST_ASSERT_EQUAL_INT(Divers::kLives, g.lives());

    // Long enough that a diver gets there whatever the seed does.
    for (int f = 0; f < 60 * 600 && !g.over(); ++f)
        g.update(kStep, Divers::kFieldW * 0.5f);

    TEST_ASSERT_TRUE(g.over());
    TEST_ASSERT_EQUAL_INT(0, g.lives());
}

void test_a_death_puts_every_diver_back_in_the_formation() {
    Divers g;
    g.reset(23);
    bool sawDying = false;
    for (int f = 0; f < 60 * 600; ++f) {
        g.update(kStep, Divers::kFieldW * 0.5f);
        if (g.phase() != Divers::Phase::Dying) continue;
        sawDying = true;
        TEST_ASSERT_EQUAL_INT(0, g.diving());
        TEST_ASSERT_FALSE(g.volleyAlive());
        TEST_ASSERT_EQUAL_INT(0, liveAlienShots(g));
        break;
    }
    TEST_ASSERT_TRUE(sawDying);
}

void test_clearing_the_board_starts_the_next_wave() {
    Divers g;
    g.reset(29);
    // A parked ship can only shoot the column above it, and a blind sweep
    // walks into everything, so this plays with the bot above. Clearing 36
    // aliens one bullet at a time is a couple of minutes of game.
    float x = Divers::kFieldW * 0.5f;
    for (int f = 0; f < 60 * 600 && g.wave() == 1 && !g.over(); ++f) {
        x = botX(g, x);
        g.update(kStep, x);
        x = g.shipX();   // as clamped, so the bot cannot walk off the panel
    }

    TEST_ASSERT_FALSE(g.over());
    TEST_ASSERT_EQUAL_INT(2, g.wave());
    // Wave 2 is a different shape, so the new board is its size, not the
    // array's and not the one just cleared.
    TEST_ASSERT_EQUAL_INT(Divers::shapeSize(g.shape()), g.alive());
}


// -- power-ups ----------------------------------------------------------------

void test_the_ship_starts_at_tier_one_with_no_clock() {
    Divers g;
    g.reset(1);
    TEST_ASSERT_EQUAL_INT(1, g.widthTier());
    TEST_ASSERT_EQUAL_INT(1, g.rateTier());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g.widthLeft01());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g.rateLeft01());
}

void test_capsules_fall_and_never_outnumber_their_slots() {
    Divers g;
    g.reset(41);
    float prevY[Divers::kMaxCapsules] = {};
    bool  sawFall = false;
    for (int f = 0; f < 60 * 300 && !g.over(); ++f) {
        g.update(kStep, Divers::kFieldW * 0.5f);
        TEST_ASSERT_LESS_OR_EQUAL_INT(Divers::kMaxCapsules, liveCapsules(g));
        for (int i = 0; i < Divers::kMaxCapsules; ++i) {
            const Divers::Capsule& c = g.capsule(i);
            if (!c.alive) { prevY[i] = 0.0f; continue; }
            if (prevY[i] > 0.0f && c.y > prevY[i]) sawFall = true;
            // Nothing falls off the top, and nothing lingers below the panel.
            TEST_ASSERT_TRUE(c.y <= Divers::kFieldH);
            prevY[i] = c.y;
        }
    }
    TEST_ASSERT_TRUE(sawFall);
}

void test_catching_a_width_capsule_widens_the_volley() {
    Divers g;
    g.reset(7);
    if (!catchOne(g, Divers::PowerKind::Width)) {
        TEST_FAIL_MESSAGE("no width capsule inside ten minutes of play");
    }
    TEST_ASSERT_EQUAL_INT(2, g.widthTier());
    TEST_ASSERT_TRUE(g.widthLeft01() > 0.9f);

    // Two pellets in the NEXT volley. The one in the air when the capsule
    // was caught was fired at tier 1 and is still a single pellet, so wait
    // for it to clear before looking.
    while (g.volleyAlive() && !g.over()) g.update(kStep, g.shipX());
    while (!g.volleyAlive() && !g.over()) g.update(kStep, g.shipX());
    int pellets = 0;
    for (int i = 0; i < Divers::kMaxPlayerShots; ++i)
        if (g.playerShot(i).alive) ++pellets;
    TEST_ASSERT_EQUAL_INT(2, pellets);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, g.playerShot(0).y, g.playerShot(1).y);
    TEST_ASSERT_TRUE(g.playerShot(0).x < g.playerShot(1).x);
}

void test_catching_a_rate_capsule_speeds_the_gun_up() {
    Divers g;
    g.reset(7);
    if (!catchOne(g, Divers::PowerKind::Rate)) {
        TEST_FAIL_MESSAGE("no rate capsule inside ten minutes of play");
    }
    TEST_ASSERT_EQUAL_INT(2, g.rateTier());

    while (g.volleyAlive() && !g.over()) g.update(kStep, g.shipX());
    while (!g.volleyAlive() && !g.over()) g.update(kStep, g.shipX());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -Divers::kRateShotSpeed[1],
                             g.playerShot(0).vy);
}

void test_a_power_up_runs_out_and_drops_straight_to_tier_one() {
    // Not "wait twenty seconds and look": a bot that keeps playing keeps
    // catching capsules, and each catch restarts the clock. The rule is that
    // the clock ALWAYS runs out if it is not refreshed, so that is what is
    // measured - the gap since the last refresh, counted only over the frames
    // that are actually being played. The clock is stopped while the ship is
    // exploding and between waves, which is why those frames are not counted
    // here either.
    Divers g;
    g.reset(7);

    float prevLeft = 0.0f;
    int   sinceRefresh = 0;
    bool  sawTiered = false, sawExpiry = false;
    const int limit = static_cast<int>((Divers::kPowerSec + 0.5f) * 60.0f);

    float x = Divers::kFieldW * 0.5f;
    for (int f = 0; f < 60 * 600 && !g.over(); ++f) {
        x = botX(g, x);
        g.update(kStep, x);
        x = g.shipX();

        const float left = g.widthLeft01();
        if (left > prevLeft + 0.001f) sinceRefresh = 0;   // caught another
        else if (g.phase() == Divers::Phase::Playing) ++sinceRefresh;
        prevLeft = left;

        if (g.widthTier() > 1) {
            sawTiered = true;
            TEST_ASSERT_LESS_OR_EQUAL_INT(limit, sinceRefresh);
        } else if (sawTiered) {
            // And it dropped all the way, not to an intermediate tier.
            TEST_ASSERT_EQUAL_FLOAT(0.0f, g.widthLeft01());
            sawExpiry = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(sawTiered);
    TEST_ASSERT_TRUE(sawExpiry);
}

void test_the_volley_rule_survives_the_widest_gun() {
    // Three pellets is still ONE volley: the gun cannot fire again until
    // every pellet of the last has gone. This is the rule the upgrades were
    // designed around, so it is the one worth a test of its own.
    Divers g;
    g.reset(7);
    if (!catchOne(g, Divers::PowerKind::Width)) {
        TEST_FAIL_MESSAGE("no width capsule inside ten minutes of play");
    }

    float x = g.shipX();
    bool  wasAlive = false;
    int   prev     = 0;
    for (int f = 0; f < 60 * 20 && !g.over(); ++f) {
        x = botX(g, x);
        g.update(kStep, x);
        x = g.shipX();

        // A volley may only appear out of nothing, never on top of itself:
        // if pellets are in the air, the count can fall but never rise.
        int now = 0;
        for (int i = 0; i < Divers::kMaxPlayerShots; ++i)
            if (g.playerShot(i).alive) ++now;
        if (wasAlive && now > prev) TEST_FAIL_MESSAGE("a volley fired on top of one");
        prev     = now;
        wasAlive = now > 0;
    }
}


// -- the flagship and its escorts ---------------------------------------------
//  The most intricate thing in the file and the least reachable: an escort is
//  placed in its leader's turning frame rather than steered, and the group is
//  what the 200 and 300 point bonuses are for.

void test_a_flagship_dives_with_an_escort_either_side() {
    Divers g;
    float    x = 0.0f;
    const int lead = flyToFlagshipDive(g, x, 2);
    if (lead < 0) TEST_FAIL_MESSAGE("no escorted flagship dive in twelve games");

    const Divers::Alien& f = g.alien(lead);
    // The leader's own frame: forward along its heading, right at 90 degrees
    // to it on the screen, where y grows downwards.
    const float h  = f.headingDeg * 3.14159265f / 180.0f;
    const float fx = std::cos(h), fy = std::sin(h);

    int   found = 0;
    float side[2] = {0.0f, 0.0f};
    for (int j = 0; j < Divers::kMaxAliens; ++j) {
        const Divers::Alien& e = g.alien(j);
        if (e.leader != lead || e.state != Divers::AlienState::Diving) continue;
        TEST_ASSERT_TRUE(found < 2);

        // Station keeping: the offset resolved in the leader's frame is the
        // offset it was given, sideways and behind.
        const float dx = e.x - f.x, dy = e.y - f.y;
        const float right   = -fy * dx + fx * dy;   // dot with the right vector
        const float forward = fx * dx + fy * dy;
        TEST_ASSERT_FLOAT_WITHIN(0.5f, e.offX, right);
        TEST_ASSERT_FLOAT_WITHIN(0.5f, -Divers::kEscortOffY, forward);
        // And it flies the leader's heading, so the group banks as one.
        TEST_ASSERT_FLOAT_WITHIN(0.01f, f.headingDeg, e.headingDeg);

        side[found++] = right;
    }
    TEST_ASSERT_EQUAL_INT(2, found);
    // One each way, not both on the same wing.
    TEST_ASSERT_TRUE(side[0] * side[1] < 0.0f);
}

void test_the_group_holds_its_shape_while_it_dives() {
    Divers g;
    float    x = 0.0f;
    const int lead = flyToFlagshipDive(g, x, 1);
    if (lead < 0) TEST_FAIL_MESSAGE("no escorted flagship dive in twelve games");

    const float want = std::sqrt(Divers::kEscortOffX * Divers::kEscortOffX +
                                 Divers::kEscortOffY * Divers::kEscortOffY);
    int checked = 0;
    for (int f = 0; f < 60 * 3; ++f) {
        x = botX(g, x);
        g.update(kStep, x);
        x = g.shipX();

        const Divers::Alien& lead_a = g.alien(lead);
        if (lead_a.state != Divers::AlienState::Diving) break;
        for (int j = 0; j < Divers::kMaxAliens; ++j) {
            const Divers::Alien& e = g.alien(j);
            if (e.leader != lead || e.state != Divers::AlienState::Diving) continue;
            const float dx = e.x - lead_a.x, dy = e.y - lead_a.y;
            // The distance never changes: that is what "in formation" means,
            // and a steered escort would breathe in and out instead.
            TEST_ASSERT_FLOAT_WITHIN(0.5f, want, std::sqrt(dx * dx + dy * dy));
            ++checked;
        }
    }
    TEST_ASSERT_GREATER_THAN_INT(10, checked);
}

void test_killing_the_flagship_pays_for_the_escorts_still_flying() {
    Divers g;
    float    x = 0.0f;
    const int lead = flyToFlagshipDive(g, x, 1);
    if (lead < 0) TEST_FAIL_MESSAGE("no escorted flagship dive in twelve games");

    // Park under it. The flagship leads its escorts, so the volley reaches it
    // first, which is the shot the bonus exists to reward.
    for (int f = 0; f < 60 * 8; ++f) {
        const Divers::Alien& before = g.alien(lead);
        if (before.state == Divers::AlienState::Dead) break;

        int escorts = 0;
        for (int j = 0; j < Divers::kMaxAliens; ++j)
            if (g.alien(j).leader == lead &&
                g.alien(j).state == Divers::AlienState::Diving) ++escorts;
        const bool diving = before.state == Divers::AlienState::Diving;
        const int  scoreBefore = g.score();

        g.update(kStep, before.x);

        if (g.alien(lead).state != Divers::AlienState::Dead) continue;

        // Whatever it was worth, it was worth the table's answer for the
        // escorts that were still up the instant before.
        const int expected = Divers::scoreFor(Divers::AlienKind::Flagship,
                                                diving, escorts);
        TEST_ASSERT_EQUAL_INT(expected, g.score() - scoreBefore);

        // And the survivors are on their own now, still flying rather than
        // frozen on a leader that no longer exists.
        for (int j = 0; j < Divers::kMaxAliens; ++j) {
            const Divers::Alien& e = g.alien(j);
            if (e.state != Divers::AlienState::Diving) continue;
            TEST_ASSERT_TRUE(e.leader != lead);
        }
        return;
    }
    TEST_FAIL_MESSAGE("the flagship was never shot down");
}

// -- the ramp -----------------------------------------------------------------

void test_every_knob_gets_harder_and_then_stops() {
    Divers::WaveSpec prev = Divers::waveSpec(1);
    for (int w = 2; w <= 30; ++w) {
        const Divers::WaveSpec s = Divers::waveSpec(w);
        TEST_ASSERT_TRUE(s.diveSpeed >= prev.diveSpeed);
        TEST_ASSERT_TRUE(s.maxDivers >= prev.maxDivers);
        TEST_ASSERT_TRUE(s.diveGapSec <= prev.diveGapSec);
        TEST_ASSERT_TRUE(s.diverFirePerSec >= prev.diverFirePerSec);
        TEST_ASSERT_TRUE(s.alienShotSpeed >= prev.alienShotSpeed);
        TEST_ASSERT_TRUE(s.swayHz >= prev.swayHz);
        prev = s;
    }

    const Divers::WaveSpec end = Divers::waveSpec(1000);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, end.diveSpeed);
    TEST_ASSERT_EQUAL_INT(4, end.maxDivers);
    TEST_ASSERT_EQUAL_FLOAT(0.7f, end.diveGapSec);
    TEST_ASSERT_EQUAL_FLOAT(1.6f, end.diverFirePerSec);
    TEST_ASSERT_EQUAL_FLOAT(110.0f, end.alienShotSpeed);
    TEST_ASSERT_EQUAL_FLOAT(0.35f, end.swayHz);
}

void test_wave_one_is_gentle_enough_to_learn_on() {
    const Divers::WaveSpec s = Divers::waveSpec(1);
    TEST_ASSERT_EQUAL_INT(1, s.maxDivers);
    // A diver crossing the field slower than the panel is tall per two
    // seconds is one a beginner can read.
    TEST_ASSERT_TRUE(s.diveSpeed < Divers::kFieldH * 0.5f);
}

// -- tilt ---------------------------------------------------------------------

void test_flat_on_its_back_is_straight_ahead() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, Divers::lateralDeg(Vec3{0.0f, 0.0f, 1.0f}));
}

void test_the_right_edge_dropping_reads_positive() {
    // 30 degrees about Y. Body +X points screen left here, so the right edge
    // going down is gravity into -X and the answer is +30: the ship follows
    // the low edge, which is the only direction that feels right in the hand.
    const float d = Divers::lateralDeg(Vec3{-0.5f, 0.0f, 0.866f});
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 30.0f, d);
    TEST_ASSERT_TRUE(Divers::shipXForTilt(d, 0.0f) > Divers::kFieldW * 0.5f);
}

void test_pitch_does_not_change_the_answer() {
    // Same 30 degrees of roll, but stood up rather than lying down: the
    // remaining gravity moved from Z to Y and the answer must not.
    const float lying = Divers::lateralDeg(Vec3{-0.5f, 0.0f, 0.866f});
    const float stood = Divers::lateralDeg(Vec3{-0.5f, 0.866f, 0.0f});
    TEST_ASSERT_FLOAT_WITHIN(0.01f, lying, stood);
}

void test_turning_about_the_other_axis_does_nothing() {
    // Pitching the panel towards the player is not a steering input, and
    // this is the test that fails if the axes are swapped back.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f,
                             Divers::lateralDeg(Vec3{0.0f, 0.5f, 0.866f}));
}

void test_freefall_reads_level_rather_than_nonsense() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Divers::lateralDeg(Vec3{0.0f, 0.0f, 0.0f}));
}

// -- fusing the two sensors ---------------------------------------------------

void test_a_still_hand_settles_on_what_gravity_says() {
    // Gyro silent, accelerometer steady: the answer has to walk to the
    // accelerometer and stop there, or the ship sits at an angle nobody is
    // holding.
    float a = 0.0f;
    for (int i = 0; i < 600; ++i) a = Divers::fuseTilt(a, 8.0f, 0.0f, 1.0f / 100.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 8.0f, a);
}

void test_the_gyro_answers_before_the_accelerometer_has_caught_up() {
    // Turning at 60 deg/s for a tenth of a second is six degrees of wrist.
    // The accelerometer has not moved yet - this is exactly the interval it
    // is worst over - so all of the answer has to come from the gyro.
    float a = 0.0f;
    for (int i = 0; i < 10; ++i) a = Divers::fuseTilt(a, 0.0f, 60.0f, 1.0f / 100.0f);
    TEST_ASSERT_TRUE(a > 5.0f);
    TEST_ASSERT_TRUE(a <= 6.0f);
}

void test_a_shake_that_is_not_a_turn_barely_moves_the_ship() {
    // THE reason the gyro is here. The hand jerks sideways: the accelerometer
    // reads a large false tilt for a tenth of a second while the gyro, which
    // cannot see linear acceleration, says the device has not turned.
    float fused = 0.0f, plain = 0.0f;
    const float dt = 1.0f / 100.0f;
    for (int i = 0; i < 10; ++i) {
        fused = Divers::fuseTilt(fused, 20.0f, 0.0f, dt);
        // What the old single-pole filter would have made of it.
        plain += (20.0f - plain) * (1.0f - std::exp(-dt / 0.06f));
    }
    TEST_ASSERT_TRUE(fused < 4.0f);
    TEST_ASSERT_TRUE(plain > 15.0f);

    // And in ship travel, which is what the player feels.
    const float drift = Divers::shipXForTilt(fused, 0.0f) - Divers::kFieldW * 0.5f;
    TEST_ASSERT_TRUE(drift < 8.0f);
}

void test_the_gyro_cannot_run_away_on_its_own() {
    // A zero error of a degree a second, which is a bad part, held for a
    // minute. The accelerometer has to peg it inside the dead zone or the
    // ship creeps off on its own.
    float a = 0.0f;
    for (int i = 0; i < 6000; ++i) a = Divers::fuseTilt(a, 0.0f, 1.0f, 1.0f / 100.0f);
    TEST_ASSERT_TRUE(std::fabs(a) <= Divers::kDeadbandDeg);
}

void test_a_gap_in_the_samples_is_not_an_interval() {
    // The sensor was off, or another app had it. Integrating the gap would
    // swing the angle by however long it was away.
    TEST_ASSERT_EQUAL_FLOAT(5.0f, Divers::fuseTilt(0.0f, 5.0f, 900.0f, 30.0f));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, Divers::fuseTilt(0.0f, 5.0f, 900.0f, 0.0f));
}

void test_the_zero_is_wherever_the_player_centred() {
    const float centre = Divers::kFieldW * 0.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, centre, Divers::shipXForTilt(0.0f, 0.0f));
    // Centred at 20 degrees, 20 degrees is still the middle.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, centre, Divers::shipXForTilt(20.0f, 20.0f));
}

void test_the_dead_zone_holds_a_still_hand_still() {
    const float centre = Divers::kFieldW * 0.5f;
    const float inside = Divers::kDeadbandDeg * 0.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, centre, Divers::shipXForTilt(inside, 0.0f));
    // And gives it back continuously: just past the edge moves a little, not
    // a jump of a whole dead zone.
    const float justOut = Divers::shipXForTilt(Divers::kDeadbandDeg + 0.1f, 0.0f);
    TEST_ASSERT_TRUE(justOut > centre);
    TEST_ASSERT_TRUE(justOut - centre < 1.0f);
}

void test_the_ship_stops_at_the_edges() {
    TEST_ASSERT_EQUAL_FLOAT(Divers::kShipMargin,
                            Divers::shipXForTilt(-90.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(Divers::kFieldW - Divers::kShipMargin,
                            Divers::shipXForTilt(90.0f, 0.0f));
}

void test_the_curve_is_fine_at_the_centre_and_quick_at_the_ends() {
    const float centre = Divers::kFieldW * 0.5f;
    // Gain per degree, just off the dead zone and just short of full tilt.
    const float step = 0.5f;
    const float near = (Divers::shipXForTilt(Divers::kDeadbandDeg + step, 0.0f) -
                        centre) / step;
    const float far  = (Divers::shipXForTilt(Divers::kFullTiltDeg, 0.0f) -
                        Divers::shipXForTilt(Divers::kFullTiltDeg - step, 0.0f)) /
                       step;
    TEST_ASSERT_TRUE(near < far);
    // And finer than the straight line to the same full travel would be,
    // which is the whole point of spending the outer range faster.
    const float linear = Divers::kTravelPx /
                         (Divers::kFullTiltDeg - Divers::kDeadbandDeg);
    TEST_ASSERT_TRUE(near < linear);
}

void test_the_curve_is_monotonic_and_symmetric() {
    const float centre = Divers::kFieldW * 0.5f;
    float prev = Divers::shipXForTilt(-20.0f, 0.0f);
    for (float d = -20.0f; d <= 20.0f; d += 0.25f) {
        const float x = Divers::shipXForTilt(d, 0.0f);
        // Never goes backwards: a curve that did would make the ship jitter
        // at the angle where it turned round.
        TEST_ASSERT_TRUE(x >= prev - 0.001f);
        prev = x;
        // The same wrist either way gives the same travel either way.
        TEST_ASSERT_FLOAT_WITHIN(0.01f, centre - x,
                                 Divers::shipXForTilt(-d, 0.0f) - centre);
    }
}

void test_the_full_width_is_reachable_within_a_wrist() {
    // 18 degrees each way is the design figure; more than that and the device
    // is pointing away from the player's eyes.
    TEST_ASSERT_EQUAL_FLOAT(Divers::kFieldW - Divers::kShipMargin,
                            Divers::shipXForTilt(18.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(Divers::kShipMargin,
                            Divers::shipXForTilt(-18.0f, 0.0f));
}

// -- determinism --------------------------------------------------------------

void test_the_same_seed_plays_the_same_game() {
    Divers a, b;
    a.reset(99);
    b.reset(99);
    run(a, 45.0f, 60.0f);
    run(b, 45.0f, 60.0f);
    TEST_ASSERT_EQUAL_INT(a.score(), b.score());
    TEST_ASSERT_EQUAL_INT(a.alive(), b.alive());
    TEST_ASSERT_EQUAL_INT(a.lives(), b.lives());
}

void test_a_long_frame_cannot_teleport_a_diver_through_the_ship() {
    Divers a, b;
    a.reset(31);
    b.reset(31);
    // One absurd step against the clamp: both must land in the same place.
    a.update(10.0f, 60.0f);
    b.update(Divers::kMaxStepSec, 60.0f);
    TEST_ASSERT_EQUAL_FLOAT(b.alien(0).y, a.alien(0).y);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_a_new_formation_is_the_shape_the_table_draws);
    RUN_TEST(test_every_shape_is_well_formed);
    RUN_TEST(test_the_shape_cycles_with_the_wave);
    RUN_TEST(test_the_formation_starts_below_the_hud_and_above_the_ship);
    RUN_TEST(test_no_shape_reaches_the_hud_or_the_ship);
    RUN_TEST(test_the_sway_never_takes_the_formation_off_the_panel);

    RUN_TEST(test_a_diver_leaves_the_formation_and_comes_back_round);
    RUN_TEST(test_never_more_divers_than_the_wave_allows);
    RUN_TEST(test_only_divers_shoot);
    RUN_TEST(test_alien_fire_stays_inside_its_buffer);

    RUN_TEST(test_the_gun_keeps_firing);
    RUN_TEST(test_the_gun_waits_the_reload_however_the_volley_was_spent);
    RUN_TEST(test_the_bullet_travels_up_from_the_ship);
    RUN_TEST(test_aliens_only_ever_die_one_at_a_time);
    RUN_TEST(test_shooting_the_formation_scores);

    RUN_TEST(test_the_scoring_table_is_the_arcades);
    RUN_TEST(test_a_flagship_is_worth_more_with_its_escorts_alive);

    RUN_TEST(test_three_lives_and_then_the_game_is_over);
    RUN_TEST(test_a_death_puts_every_diver_back_in_the_formation);
    RUN_TEST(test_clearing_the_board_starts_the_next_wave);

    RUN_TEST(test_a_flagship_dives_with_an_escort_either_side);
    RUN_TEST(test_the_group_holds_its_shape_while_it_dives);
    RUN_TEST(test_killing_the_flagship_pays_for_the_escorts_still_flying);

    RUN_TEST(test_the_ship_starts_at_tier_one_with_no_clock);
    RUN_TEST(test_capsules_fall_and_never_outnumber_their_slots);
    RUN_TEST(test_catching_a_width_capsule_widens_the_volley);
    RUN_TEST(test_catching_a_rate_capsule_speeds_the_gun_up);
    RUN_TEST(test_a_power_up_runs_out_and_drops_straight_to_tier_one);
    RUN_TEST(test_the_volley_rule_survives_the_widest_gun);

    RUN_TEST(test_every_knob_gets_harder_and_then_stops);
    RUN_TEST(test_wave_one_is_gentle_enough_to_learn_on);

    RUN_TEST(test_flat_on_its_back_is_straight_ahead);
    RUN_TEST(test_the_right_edge_dropping_reads_positive);
    RUN_TEST(test_pitch_does_not_change_the_answer);
    RUN_TEST(test_turning_about_the_other_axis_does_nothing);
    RUN_TEST(test_freefall_reads_level_rather_than_nonsense);
    RUN_TEST(test_a_still_hand_settles_on_what_gravity_says);
    RUN_TEST(test_the_gyro_answers_before_the_accelerometer_has_caught_up);
    RUN_TEST(test_a_shake_that_is_not_a_turn_barely_moves_the_ship);
    RUN_TEST(test_the_gyro_cannot_run_away_on_its_own);
    RUN_TEST(test_a_gap_in_the_samples_is_not_an_interval);

    RUN_TEST(test_the_zero_is_wherever_the_player_centred);
    RUN_TEST(test_the_dead_zone_holds_a_still_hand_still);
    RUN_TEST(test_the_ship_stops_at_the_edges);
    RUN_TEST(test_the_curve_is_fine_at_the_centre_and_quick_at_the_ends);
    RUN_TEST(test_the_curve_is_monotonic_and_symmetric);
    RUN_TEST(test_the_full_width_is_reachable_within_a_wrist);

    RUN_TEST(test_the_same_seed_plays_the_same_game);
    RUN_TEST(test_a_long_frame_cannot_teleport_a_diver_through_the_ship);

    return UNITY_END();
}
