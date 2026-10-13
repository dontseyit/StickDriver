#pragma once
// -----------------------------------------------------------------------------
//  Divers - the game, with no hardware in it. A formation shooter in the style
//  of Galaxian (1979), which is where the three rules below come from.
//
//  Everything the game decides lives here: where the formation sits, when an
//  alien breaks out of it, how a dive curves, what a kill is worth, when a wave
//  is over. The app above turns gravity into a ship position, calls update()
//  and draws what it finds; it holds no rules. So every rule is checked on a
//  laptop with `pio test -e native -f test_divers`.
//
//  Coordinates are the panel's at rotation 0: 135 across, 240 down, y growing
//  downwards, and an alien or a ship is placed by its CENTRE. A heading is in
//  degrees with 0 towards +x and 90 straight down, so the sign of a turn reads
//  the same as the sign of the angle.
//
//  Three things make this Galaxian rather than Space Invaders, and none of them
//  is negotiable in the tuning:
//
//    * Aliens in formation never shoot. Only the ones diving at you do.
//    * A dive is STEERED, not scripted: the diver turns towards the ship at a
//      limited rate and weaves. One path generator covers every dive, and what
//      it looks like is three constants rather than a table of waypoints.
//    * One volley is in the air at a time. With automatic fire that is the
//      whole of the difficulty, which is why the reload is short and the ramp
//      is spent on the aliens instead. A width power-up buys more pellets in
//      the volley, never a second volley.
// -----------------------------------------------------------------------------
#include "mathx/Vec3.h"

#include <cstdint>

namespace sd {

class Divers {
public:
    // -- the field, shared with whatever draws it ----------------------------
    static constexpr int kFieldW = 135;
    static constexpr int kFieldH = 240;
    /// Score, wave and lives. The game draws its own, so the app runs
    /// fullscreen and nothing below this line is covered by the framework.
    static constexpr int kHudH = 10;

    // -- the formation -------------------------------------------------------
    static constexpr int kCols = 10;
    static constexpr int kRows = 6;
    /// The array every shape is built into. The fullest shape is the wedge at
    /// 36; the slack is room for a shape somebody adds later.
    static constexpr int kMaxAliens = 40;

    /// One layout, written as a picture so the shape is visible in the file
    /// that defines it. F flagship, R red, P purple, B blue, '.' empty.
    struct Shape {
        const char* name;
        const char* row[kRows];
    };

    static constexpr int kShapeCount = 6;
    static constexpr Shape kShapes[kShapeCount] = {
        {"WEDGE", {"....FF....",
                   "..RRRRRR..",
                   ".PPPPPPPP.",
                   "BBBBBBBBBB",
                   "BBBBBBBBBB",
                   ".........."}},
        {"BOW",   {"...FFFF...",
                   "..RRRRRR..",
                   ".PPPPPPPP.",
                   "PP......PP",
                   "BB......BB",
                   "BB......BB"}},
        {"TWIN",  {"FF......FF",
                   "RRR....RRR",
                   "PPPP..PPPP",
                   "BBBB..BBBB",
                   "BBBB..BBBB",
                   ".........."}},
        {"VEE",   {"BBBBBBBBBB",
                   ".PPPPPPPP.",
                   "..PPPPPP..",
                   "...RRRR...",
                   "....FF....",
                   ".........."}},
        {"PINCER",{"PPP....PPP",
                   "BBB....BBB",
                   "BBB....BBB",
                   "RRR....RRR",
                   "...FFFF...",
                   ".........."}},
        {"DIAMOND",{"....FF....",
                    "...RRRR...",
                    "..PPPPPP..",
                    ".BBBBBBBB.",
                    "..BBBBBB..",
                    "...BBBB..."}},
    };

    /// The shape a wave is flown in. Six waves to a cycle, with the ramp
    /// climbing underneath it, so the same picture is never the same fight.
    static const Shape& shapeFor(int wave);

    /// How many aliens a shape holds. Used by the assertions below and by a
    /// test that the array can hold the fullest one.
    static constexpr int shapeSize(const Shape& s) {
        int n = 0;
        for (int r = 0; r < kRows; ++r)
            for (int c = 0; c < kCols; ++c)
                if (s.row[r][c] != '.') ++n;
        return n;
    }

    static constexpr float kColPitch = 12.0f;
    static constexpr float kRowPitch = 13.0f;
    /// Ten columns of 12 leave 15 px over, so the block is inset 7.5 each side.
    static constexpr float kFormLeft = 7.5f;
    /// Centre of the top row, clear of the HUD by half an alien and a margin.
    static constexpr float kFormTop = 30.0f;
    /// How far the whole block slides each way. Only its RATE moves with the
    /// wave: a wider sway would take the outer columns off the panel.
    static constexpr float kSwayPx = 6.0f;

    static constexpr float kAlienHalfW = 4.0f;
    static constexpr float kAlienHalfH = 3.5f;

    // -- the ship ------------------------------------------------------------
    static constexpr float kShipY      = 228.0f;
    /// Half of the 8 x 8 sprite the app draws for it.
    static constexpr float kShipHalfW  = 4.0f;
    static constexpr float kShipHalfH  = 4.0f;
    /// Keeps the whole sprite on the panel at either end of the travel.
    static constexpr float kShipMargin = 6.0f;

    // -- shooting ------------------------------------------------------------
    static constexpr int   kMaxAlienShots = 8;
    static constexpr float kShotHalfW     = 1.0f;
    static constexpr float kShotHalfH     = 3.0f;

    /// Pellets in the widest volley, and so the size of the player's array.
    static constexpr int kMaxPlayerShots = 3;

    /// ONE VOLLEY AT A TIME, whatever its width: the next one waits for every
    /// pellet of the last to leave the panel or hit something. Width buys
    /// coverage and rate buys rate, and neither quietly buys the other. Take
    /// this rule out and the game stops being about choosing a moment.
    static constexpr int kMaxTier = 3;

    /// Tier 1 is the ship as it starts. Tier 3 is where a faster gun would
    /// become a stream and aiming would stop mattering.
    static constexpr float kRateCooldownSec[kMaxTier] = {0.10f, 0.05f, 0.02f};
    static constexpr float kRateShotSpeed[kMaxTier]   = {260.0f, 310.0f, 360.0f};
    /// Half the gap between the outer pellets of a volley, per tier.
    static constexpr float kWidthSpreadPx[kMaxTier]   = {0.0f, 3.0f, 6.0f};

    // -- power-ups -------------------------------------------------------------
    static constexpr int   kMaxCapsules    = 3;
    /// Slower than any alien bullet, so a capsule can be chased down.
    static constexpr float kCapsuleSpeed   = 45.0f;
    static constexpr float kCapsuleHalfW   = 3.5f;
    static constexpr float kCapsuleHalfH   = 3.5f;
    /// How long a catch lasts. The clock restarts on each catch, and when it
    /// runs out the kind drops straight back to tier 1: a weapon that fades
    /// through a middle state is one nobody can time.
    ///
    /// It runs only while the game is being played. Dying costs a life, and
    /// charging the player for the two seconds of explosion as well would be
    /// taking the same mistake twice.
    static constexpr float kPowerSec = 20.0f;
    /// Chance that a kill drops one, by alien kind, in the order of AlienKind
    /// below. A flagship is worth going after for more than the points.
    ///
    /// Measured, not chosen: over 24 simulated games these leave the ship
    /// upgraded 47 % of the time, with both kinds at once only 9 % and a top
    /// tier 15 %, and they do not change the wave a run reaches. Chasing a
    /// capsule costs safety, which is what pays for the gun.
    static constexpr float kDropChance[4] = {0.04f, 0.05f, 0.06f, 0.15f};

    // -- a dive --------------------------------------------------------------
    /// How fast a diver may turn. Sets the size of the loop it makes coming out
    /// of the formation: slower is a wider arc.
    static constexpr float kTurnRateDps = 200.0f;
    /// The weave on top of that steering, which is what stops a dive being a
    /// straight line to the ship and the game being a test of holding still.
    static constexpr float kWeaveAmpDeg = 22.0f;
    static constexpr float kWeaveHz     = 1.1f;
    /// A diver aims at the deck rather than at the ship, so it arrives where
    /// the ship is going rather than where it was.
    static constexpr float kDiveAimY = kShipY + 24.0f;
    /// Where it stops steering and commits to the line it is on. A diver that
    /// hunts all the way down cannot be dodged, only outrun, and the game
    /// becomes a race rather than a reading of the path.
    static constexpr float kCommitY = 150.0f;
    /// How far below the panel a diver must get before it comes round again.
    static constexpr float kExitY = kFieldH + 12.0f;
    /// Where it re-enters, and how close to its slot counts as back in.
    static constexpr float kEntryY   = -10.0f;
    static constexpr float kRejoinPx = 2.5f;

    /// A flagship takes two escorts, one either side, held in its frame.
    static constexpr float kEscortOffX = 11.0f;
    static constexpr float kEscortOffY = 7.0f;
    /// One dive in four is the flagship's, when one is there to send.
    static constexpr int kFlagshipEveryNthDive = 4;

    // -- the round -----------------------------------------------------------
    static constexpr int   kLives        = 3;
    static constexpr float kReadySec     = 1.5f;
    static constexpr float kDyingSec     = 1.2f;
    static constexpr float kWaveClearSec = 1.0f;

    /// A frame that took longer than this is a hiccup, not slow motion.
    /// Clamped so nothing can move through the ship between two steps.
    static constexpr float kMaxStepSec = 0.05f;

    // -- tilt ----------------------------------------------------------------
    /// How far the ship travels either side of centre.
    static constexpr float kTravelPx = kFieldW * 0.5f - kShipMargin;

    /// The wrist angle that reaches the end of that travel. Twelve rather
    /// than the eighteen a straight line needed: the curve below spends the
    /// outer part of the range fast, so the full width arrives sooner without
    /// costing the fine control at the centre.
    static constexpr float kFullTiltDeg = 12.0f;

    /// Dead zone about the captured zero, so a still hand gives a still ship.
    /// Subtracted rather than clipped, so the ship eases out of it instead of
    /// jumping a dead zone's worth the moment it is crossed.
    static constexpr float kDeadbandDeg = 0.6f;

    /// How much of the response is a straight line; the rest is cubic.
    ///
    /// The expo curve of an RC transmitter, and for the same reason: a stick
    /// -- or a wrist -- is steadiest near its middle and least steady at the
    /// ends, so the middle should be where the fine control is. At 0.4 the
    /// centre answers at about 2.2 px per degree against the 5.4 a straight
    /// line to the same 12 degrees would need, and the last few degrees make
    /// up the difference. Aiming happens at the centre, dodging at the ends.
    ///
    /// 1.0 is a straight line, 0.0 is pure cubic and too vague to aim with.
    static constexpr float kExpoLinear = 0.4f;

    // -- explosions ------------------------------------------------------------
    static constexpr int   kMaxBlasts = 6;
    static constexpr float kBlastSec  = 0.35f;

    // -- the layout is arithmetic, so it is checked where it is written -------
    static_assert(kFormLeft + kCols * kColPitch <= kFieldW,
                  "the formation is wider than the panel");
    static_assert(kFormLeft - kSwayPx >= 0.0f,
                  "the sway takes the outer column off the panel");
    static_assert(kFormTop - kAlienHalfH > kHudH,
                  "the top row is under the HUD");
    static_assert(kFormTop + (kRows - 1) * kRowPitch + kAlienHalfH < kShipY,
                  "the bottom row is on top of the ship");
    static_assert(kShipMargin >= kShipHalfW,
                  "the ship hangs off the edge of the panel");
    static_assert(kCommitY > kFormTop + (kRows - 1) * kRowPitch,
                  "a diver commits before it has left the formation");
    static_assert(kDiveAimY >= kExitY,
                  "a diver would turn back up before it leaves the panel");

    // -------------------------------------------------------------------------
    enum class Phase : uint8_t {
        Ready,      ///< formation is up, the ship is not yet shot at
        Playing,
        Dying,      ///< the ship has been hit; divers are cleared
        WaveClear,
        GameOver,
    };

    enum class AlienKind : uint8_t { Blue, Purple, Red, Flagship };

    enum class AlienState : uint8_t {
        Dead,
        Formation,
        Diving,
        Returning,  ///< off the bottom, coming back in at the top
    };

    struct Alien {
        float      x = 0.0f, y = 0.0f;
        float      headingDeg = 90.0f;
        float      weavePhase = 0.0f;
        AlienKind  kind   = AlienKind::Blue;
        AlienState state  = AlienState::Dead;
        uint8_t    row = 0, col = 0;
        /// Index of the flagship this one is escorting, or -1. An escort holds
        /// station in the leader's frame and is cut loose when it dies, which
        /// is how the group scatters.
        int8_t     leader = -1;
        float      offX   = 0.0f;
    };

    struct Shot {
        float x = 0.0f, y = 0.0f;
        float vy = 0.0f;
        bool  alive = false;
    };

    struct Blast {
        float x = 0.0f, y = 0.0f;
        float age = 0.0f;
        bool  alive = false;
    };

    enum class PowerKind : uint8_t { Width, Rate };

    struct Capsule {
        float     x = 0.0f, y = 0.0f;
        PowerKind kind = PowerKind::Width;
        bool      alive = false;
    };

    /// Everything that gets harder. Linear in the wave and capped, so the game
    /// stops climbing rather than becoming impossible.
    struct WaveSpec {
        float diveSpeed;         ///< px/s
        int   maxDivers;         ///< flying at once, escorts included
        float diveGapSec;        ///< between launches
        float diverFirePerSec;   ///< per diver
        float alienShotSpeed;    ///< px/s
        float swayHz;
    };

    static WaveSpec waveSpec(int wave);

    /// What an alien is worth. A flagship is worth more the more of its escorts
    /// are still flying when it dies, which is the arcade's one bonus.
    static int scoreFor(AlienKind kind, bool diving, int escortsAlive);

    /// The lateral inclination of the device, in degrees, positive when the
    /// right-hand edge drops.
    ///
    /// At rotation 0 the StickS3 reads (0, 0, +1) on its back and the wrist
    /// turns about Y, so the whole of the answer is in the normalised X
    /// component, negated: body +X points screen LEFT at this rotation.
    /// Taking it from that one component means the reading does not care how
    /// far the panel is pitched towards the player: the game plays the same
    /// flat on a desk and held up.
    static float lateralDeg(const Vec3& accel);

    // -- fusing the two sensors ----------------------------------------------
    //  An accelerometer cannot tell gravity from the hand's own acceleration,
    //  so a wrist snapped sideways to dodge reads partly as TILT, briefly the
    //  wrong way, since the push leads the turn. Filtering that out with a
    //  slow enough low-pass costs the lag that same dodge needed. The gyro has
    //  the opposite faults: it sees rotation cleanly and at once and is blind
    //  to linear acceleration, but its small zero error integrates into drift.
    //
    //  So integrate the gyro for the short term and let the accelerometer pull
    //  the answer back slowly. Each covers the other's fault.

    /// How hard the accelerometer pulls. Long, because it is only there to
    /// stop the drift: at half a second a gyro zero error of a degree a second
    /// settles at half a degree of offset, which is inside the dead zone.
    static constexpr float kFuseTauSec = 0.5f;

    /// Which way a positive gyro reading turns the angle.
    ///
    /// Derived, not measured: lateralDeg() is -asin(a.x), and a body turned by
    /// +t about +Y sees gravity move to a.x = -sin t, so the angle is +t and
    /// its rate is +gyro.y. That holds for any right-handed body frame, which
    /// is what the two sensors share.
    ///
    /// If a quick flick feels like it kicks back before it moves, this is the
    /// wrong way round: flip it.
    static constexpr float kGyroSign = 1.0f;

    /// One step of the filter. `gyroDps` is the rate about body Y.
    ///
    /// A `dt` of zero or longer than half a second is a gap rather than an
    /// interval - the sensor was off, or another app had it - and integrating
    /// it would swing the angle by however long it was away, so the
    /// accelerometer's word is taken whole.
    static float fuseTilt(float prevDeg, float accelDeg, float gyroDps, float dt);

    /// Where the ship sits for a wrist angle, given the zero last captured.
    static float shipXForTilt(float rollDeg, float zeroDeg);

    // -------------------------------------------------------------------------
    /// Starts a new game at wave 1. The seed picks the order of the dives.
    void reset(uint32_t seed);

    /// Advances by `dt` seconds with the ship centred on `shipX`. Pausing is
    /// not calling this.
    void update(float dt, float shipX);

    Phase phase() const { return _phase; }
    bool  over() const { return _phase == Phase::GameOver; }
    int   score() const { return _score; }
    int   wave() const { return _wave; }
    int   lives() const { return _lives; }
    /// Aliens still on the board, in formation or in the air.
    int   alive() const;
    int   diving() const;
    float shipX() const { return _shipX; }

    const Alien&   alien(int i) const { return _alien[i]; }
    const Shot&    playerShot(int i) const { return _shot[i]; }
    const Shot&    alienShot(int i) const { return _alienShot[i]; }
    const Blast&   blast(int i) const { return _blast[i]; }
    const Capsule& capsule(int i) const { return _capsule[i]; }

    /// True while any pellet of the last volley is still in the air.
    bool volleyAlive() const;

    /// The shape this wave is flown in, for a screen that announces it.
    const Shape& shape() const { return shapeFor(_wave); }

    int widthTier() const { return _widthTier; }
    int rateTier() const { return _rateTier; }
    /// How much of each power-up's clock is left, 0..1, for the bars.
    float widthLeft01() const { return _widthSec / kPowerSec; }
    float rateLeft01() const { return _rateSec / kPowerSec; }

private:
    void  buildFormation();
    void  stepPlay(float dt);
    void  stepFormation(float dt);
    void  stepDivers(float dt, const WaveSpec& spec);
    void  stepShots(float dt);
    void  stepBlasts(float dt);
    void  launchDive(const WaveSpec& spec);
    void  fire();
    void  stepPower(float dt);
    void  stepCapsules(float dt);
    void  dropCapsule(const Alien& a);
    void  take(PowerKind k);
    float cooldownSec() const { return kRateCooldownSec[_rateTier - 1]; }
    float shotSpeed() const { return kRateShotSpeed[_rateTier - 1]; }
    void  hit(int index);
    void  loseLife();
    void  clearDivers();
    void  addBlast(float x, float y);
    void  collide();

    /// Where alien `i`'s slot is right now, sway included.
    float slotX(const Alien& a) const;
    float slotY(const Alien& a) const;

    float rand01();

    Alien   _alien[kMaxAliens];
    Shot    _shot[kMaxPlayerShots];     ///< one volley, one to three pellets
    Shot    _alienShot[kMaxAlienShots];
    Blast   _blast[kMaxBlasts];
    Capsule _capsule[kMaxCapsules];

    Phase _phase = Phase::Ready;
    int   _score = 0;
    int   _wave  = 1;
    int   _lives = kLives;

    float _shipX     = kFieldW * 0.5f;
    float _cooldown  = 0.0f;
    float _swayPhase = 0.0f;
    float _diveTimer = 0.0f;
    float _phaseTimer = 0.0f;
    int   _dives     = 0;   ///< launched this wave, for the flagship's turn

    int   _widthTier = 1;
    int   _rateTier  = 1;
    float _widthSec  = 0.0f;   ///< left on the clock; 0 means tier 1
    float _rateSec   = 0.0f;

    uint32_t _rng = 1u;
};

// -- and the shapes, which cannot be checked inside the class that holds them -
static_assert(Divers::shapeSize(Divers::kShapes[0]) <= Divers::kMaxAliens &&
                  Divers::shapeSize(Divers::kShapes[1]) <= Divers::kMaxAliens &&
                  Divers::shapeSize(Divers::kShapes[2]) <= Divers::kMaxAliens &&
                  Divers::shapeSize(Divers::kShapes[3]) <= Divers::kMaxAliens &&
                  Divers::shapeSize(Divers::kShapes[4]) <= Divers::kMaxAliens &&
                  Divers::shapeSize(Divers::kShapes[5]) <= Divers::kMaxAliens,
              "a shape holds more aliens than the array does");
static_assert(Divers::shapeSize(Divers::kShapes[0]) > 0 &&
                  Divers::shapeSize(Divers::kShapes[3]) > 0 &&
                  Divers::shapeSize(Divers::kShapes[5]) > 0,
              "a shape with nothing in it is a wave that clears itself");

}  // namespace sd
