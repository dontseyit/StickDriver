// -----------------------------------------------------------------------------
//  DIVERS, an arcade game.
//
//  Tilt to control the ship; the gun fires by itself, one bullet at a time.
//  The game rules are contained in the `mathx/Divers.h` and tested on the 
//  host. This file handles hardware interaction such as movements of the 
//  ship's position, the buttons, and the drawing.
//
//  The tilt is ABSOLUTE. The tilt angle controls the position of the ship. 
//  The gyro is fused in to smooth movements; see the filter below. 
//  Pressing A sets the current angle as straight ahead. The zero is also 
//  taken at the start of each round, so the game starts centered to position 
//  you are holding the device.
//
// -----------------------------------------------------------------------------
#include "core/App.h"
#include "core/AppManager.h"
#include "core/Log.h"
#include "mathx/Divers.h"
#include "services/ServiceHub.h"
#include "services/Settings.h"
#include "ui/Canvas.h"
#include "ui/Widgets.h"

#include <cmath>
#include <cstdio>

namespace sd {
namespace {

using G = Divers;

constexpr const char* kKeyBest = "best";
constexpr const char* kKeyWave = "wave";

// -- the palette --------------------------------------------------------------
//  Arcade colors based on scores: red aliens is worth more than blue ones.
constexpr uint16_t kBlue   = ui::rgb(0x5A, 0x9C, 0xFF);
constexpr uint16_t kPurple = ui::rgb(0xB0, 0x8B, 0xFF);
constexpr uint16_t kRed    = ui::theme::kErr;
constexpr uint16_t kBoss   = ui::theme::kAccent;

// -- the sprites --------------------------------------------------------------
//  One byte a row, high bit on the left. drawBitmap draws a whole sprite in one
//  call instead of about fifty pixel writes.
//
//  Each sprite size is asserted against its collision box in mathx/Divers.h.
constexpr int kAlienSpriteW   = 8, kAlienSpriteH   = 7;
constexpr int kShipSpriteW    = 8, kShipSpriteH    = 8;
constexpr int kCapsuleSpriteW = 7, kCapsuleSpriteH = 7;

static_assert(kAlienSpriteW == 2 * G::kAlienHalfW &&
                  kAlienSpriteH == 2 * G::kAlienHalfH,
              "the alien is not drawn the size it is shot at");
static_assert(kShipSpriteW == 2 * G::kShipHalfW &&
                  kShipSpriteH == 2 * G::kShipHalfH,
              "the ship is not drawn the size it is hit at");
static_assert(kCapsuleSpriteW == 2 * G::kCapsuleHalfW &&
                  kCapsuleSpriteH == 2 * G::kCapsuleHalfH,
              "the capsule is not drawn the size it is caught at");
constexpr uint8_t kDrone[7] = {
    0b00100100,
    0b00111100,
    0b01100110,
    0b11111111,
    0b10111101,
    0b10100101,
    0b00100100,
};

constexpr uint8_t kEscort[7] = {
    0b00011000,
    0b00111100,
    0b01111110,
    0b11011011,
    0b11111111,
    0b01100110,
    0b11000011,
};

constexpr uint8_t kFlag[7] = {
    0b00011000,
    0b00111100,
    0b01111110,
    0b11111111,
    0b11011011,
    0b10111101,
    0b00100100,
};

constexpr uint8_t kShip[8] = {
    0b00011000,
    0b00011000,
    0b00111100,
    0b00111100,
    0b01111110,
    0b11111111,
    0b11100111,
    0b10000001,
};

/// The ship again at three pixels, for the lives left.
constexpr uint8_t kPip[3] = {
    0b00100000,
    0b01110000,
    0b11111000,
};

/// A capsule: a ring round a solid core, so it reads as a thing to collect
/// rather than a thing to avoid. Different upgrades indicated by color. Seven
/// wide, which is the width it is caught at.
constexpr uint8_t kCapsule[7] = {
    0b01111100,
    0b10000010,
    0b10111010,
    0b10111010,
    0b10111010,
    0b10000010,
    0b01111100,
};

/// Cyan means width and amber means rate, on the capsule and on its clock. A
/// seven-pixel sprite has room for colour and nothing else.
constexpr uint16_t kWidthColour = ui::theme::kCyan;
constexpr uint16_t kRateColour  = ui::theme::kAccent;

// -- the starfield ------------------------------------------------------------
//  Twenty-four pixels at three speeds on the background for sense of flying.
constexpr int kStars = 24;

const uint8_t* spriteFor(G::AlienKind k) {
    switch (k) {
        case G::AlienKind::Flagship: return kFlag;
        case G::AlienKind::Red:      return kEscort;
        case G::AlienKind::Purple:   return kEscort;
        default:                     return kDrone;
    }
}

uint16_t colourFor(G::AlienKind k) {
    switch (k) {
        case G::AlienKind::Flagship: return kBoss;
        case G::AlienKind::Red:      return kRed;
        case G::AlienKind::Purple:   return kPurple;
        default:                     return kBlue;
    }
}

constexpr AppInfo kInfo{
    .id       = "divers",
    .title    = "DIVERS",
    .subtitle = "tilt to fly",
    .group    = "PLAY",
    .parent   = "games",
    .needs    = Cap::Display | Cap::Imu,
    // Portrait gives 240 rows of empty panel above the ship, which is where the
    // game happens.
    .rotation = 0,
    // A full frame is 65 kB down a 40 MHz bus and takes 13 ms to draw, so
    // drawing faster than 30 Hz buys nothing. The physics still runs at 60.
    .tickHz   = 60,
    .drawHz   = 30,
    .accent   = ui::theme::kCyan,
    .fullscreen = false,
    .what     = "A formation of aliens, in the style of Galaxian (1979). The ones that dive out of it can kill you.",
    .how      = "Tilt to move; the gun fires on its own. A re-centres, hold A pauses.",
    .care     = "One bullet at a time, as in the arcade. Miss and you wait for it to clear.",
};

}  // namespace

// -----------------------------------------------------------------------------
class DiversApp : public App {
public:
    void onStart() override {
        Settings s(kInfo.id, /*readOnly*/ true);
        _best     = static_cast<uint32_t>(s.getInt(kKeyBest, 0));
        _bestWave = static_cast<uint32_t>(s.getInt(kKeyWave, 0));

        for (int i = 0; i < kStars; ++i) {
            // A fixed pattern. The field repeats every wrap whatever the seed,
            // so a random one would differ only on the first pass.
            _star[i].x = static_cast<uint8_t>((i * 47 + 13) % G::kFieldW);
            _star[i].y = static_cast<float>((i * 91 + 7) % G::kFieldH);
            _star[i].v = 10.0f + static_cast<float>(i % 3) * 14.0f;
        }
        _screen = kTitle;
        apps().setChrome(true);
    }

    void onStop() override { saveBest(); }

    // -- input ---------------------------------------------------------------
    bool onEvent(const InputEvent& ev) override {
        if (_screen == kTitle) {
            if (ev.click(Btn::A)) { startGame(); return true; }
            return false;   // Back leaves for GAMES
        }

        if (ev.type == EvType::Back) {
            _screen = kTitle;
            saveBest();
            apps().setChrome(true);
            return true;
        }
        if (ev.longPress(Btn::A)) {
            _paused = !_paused;
            return true;
        }
        if (ev.click(Btn::A)) {
            if (_game.over())   { startGame(); return true; }
            if (_paused)        { _paused = false; return true; }
            _zeroDeg = _rollDeg;   // this is straight ahead now
            return true;
        }
        // Swallow the rest of a hold on A and nothing more. MENU arrives as
        // an A event, so an app that eats every A event cannot be left by the
        // chord.
        if (ev.is(Btn::A, EvType::Repeat) || ev.is(Btn::A, EvType::Release))
            return true;
        return false;
    }

    void onTick(uint32_t dtUs) override {
        sampleTilt();

        const float dt = static_cast<float>(dtUs) * 1e-6f;
        if (_screen != kPlay || _paused) return;

        // The gun fires itself and a steady hand barely stirs the IMU, so
        // without this the panel dims mid-wave. Only while the game runs: on
        // the title, paused or over, the framework's own idle clock applies.
        // An abandoned game reaches GAME OVER by itself, so a device put down
        // still goes dark.
        if (!_game.over()) services().mood().noteUse();

        stepStars(dt);
        _game.update(dt, G::shipXForTilt(_rollDeg, _zeroDeg));
        if (_game.over()) saveBest();
    }

    void onDraw(ui::Canvas& c) override {
        if (_screen == kTitle) drawTitle(c);
        else                   drawPlay(c);
    }

    const char* hintA() const override { return _screen == kTitle ? "play" : nullptr; }
    const char* hintB() const override { return nullptr; }

private:
    enum Screen : uint8_t { kTitle, kPlay };

    struct Star {
        uint8_t x = 0;
        float   y = 0.0f;
        float   v = 0.0f;
    };

    // -- sensing -------------------------------------------------------------
    /// Fuses the gyro and the accelerometer; the filter is in Divers.h. It runs
    /// at the sensor's rate rather than the tick's, because the gyro half is an
    /// integral and a late sample still describes the interval it covers.
    void sampleTilt() {
        auto& imu = services().imu();
        if (!imu.poll()) return;

        const float    raw = G::lateralDeg(imu.accel());
        const uint32_t us  = imu.lastSampleUs();

        if (!_tiltReady) {
            // Start where the hand is. Starting at level would slide the ship
            // across the panel over the first tenth of a second.
            _rollDeg   = raw;
            _zeroDeg   = raw;
            _tiltReady = true;
            _lastUs    = us;
            return;
        }

        // An unsigned difference survives the micros() wrap at 71 minutes.
        const float dt = static_cast<float>(us - _lastUs) * 1e-6f;
        _lastUs = us;

        _rollDeg = G::fuseTilt(_rollDeg, raw, imu.gyro().y, dt);
        logTilt(raw);
    }

    /// Prints what each half of the filter believes, for tuning G::kGyroSign
    /// and G::kFuseTauSec against a real hand. Compiled out at the default log
    /// level.
    void logTilt(float raw) const {
#if SD_LOG_LEVEL >= 4
        static uint32_t nextMs = 0;
        const uint32_t  now    = millis();
        if (static_cast<int32_t>(now - nextMs) < 0) return;
        nextMs = now + 1000;
        SD_LOGD("tilt", "accel %+6.1f  fused %+6.1f  gyro %+7.1f dps  zero %+6.1f",
                static_cast<double>(raw), static_cast<double>(_rollDeg),
                static_cast<double>(services().imu().gyro().y),
                static_cast<double>(_zeroDeg));
#else
        (void)raw;
#endif
    }

    void stepStars(float dt) {
        for (Star& s : _star) {
            s.y += s.v * dt;
            if (s.y >= G::kFieldH) s.y -= G::kFieldH - G::kHudH;
        }
    }

    // -- game ----------------------------------------------------------------
    void startGame() {
        // micros() at a press is unpredictable, and the counter separates two
        // games started in the same microsecond.
        _game.reset(micros() ^ (++_games * 2654435761u));
        _zeroDeg = _rollDeg;   // however it is being held right now
        _paused  = false;
        _saved   = false;
        _screen  = kPlay;
        // The field uses all 240 rows and draws its own HUD.
        apps().setChrome(false);
    }

    /// Writes only on a new best, and only once a game. NVS is flash and this
    /// runs from a tick.
    void saveBest() {
        const uint32_t score = static_cast<uint32_t>(_game.score());
        if (_saved || score == 0 || score <= _best) return;
        _best     = score;
        _bestWave = static_cast<uint32_t>(_game.wave());
        _saved    = true;

        Settings s(kInfo.id, /*readOnly*/ false);
        s.putInt(kKeyBest, static_cast<int32_t>(_best));
        s.putInt(kKeyWave, static_cast<int32_t>(_bestWave));
    }

    // -- drawing -------------------------------------------------------------
    void drawTitle(ui::Canvas& c) {
        const int top = ui::theme::kBarH + 1;

        for (int i = 0; i < 5; ++i)
            c.drawBitmap(20 + i * 20, top + 14, i == 2 ? kFlag : kDrone,
                         kAlienSpriteW, kAlienSpriteH, i == 2 ? kBoss : kBlue);

        c.textAt(c.w() / 2, top + 44, "TILT TO FLY", ui::theme::kText,
                 &fonts::Font2, middle_center);
        c.textAt(c.w() / 2, top + 62, "the gun fires itself", ui::theme::kDim,
                 &fonts::Font0, middle_center);

        c.rule(top + 76, ui::theme::kLine, 12);

        if (_best != 0) {
            c.textf(c.w() / 2, top + 92, ui::theme::kCyan, &fonts::Font2,
                    middle_center, "%lu", static_cast<unsigned long>(_best));
            c.textf(c.w() / 2, top + 108, ui::theme::kFaint, &fonts::Font0,
                    middle_center, "best, wave %lu",
                    static_cast<unsigned long>(_bestWave));
        } else {
            c.textAt(c.w() / 2, top + 96, "no score yet", ui::theme::kFaint,
                     &fonts::Font0, middle_center);
        }

        c.textAt(c.w() / 2, top + 134, "A re-centres", ui::theme::kDim,
                 &fonts::Font0, middle_center);
        c.textAt(c.w() / 2, top + 146, "hold A pauses", ui::theme::kDim,
                 &fonts::Font0, middle_center);
    }

    void drawPlay(ui::Canvas& c) {
        drawStars(c);
        drawAliens(c);
        drawCapsules(c);
        drawShots(c);
        drawShip(c);
        drawBlasts(c);
        drawHud(c);
        drawPowerClocks(c);
        drawOverlay(c);
    }

    void drawCapsules(ui::Canvas& c) {
        for (int i = 0; i < G::kMaxCapsules; ++i) {
            const G::Capsule& cap = _game.capsule(i);
            if (!cap.alive) continue;
            c.drawBitmap(static_cast<int>(cap.x - G::kCapsuleHalfW),
                         static_cast<int>(cap.y - G::kCapsuleHalfH), kCapsule,
                         kCapsuleSpriteW, kCapsuleSpriteH,
                         cap.kind == G::PowerKind::Width ? kWidthColour : kRateColour);
        }
    }

    /// The two clocks run along the bottom rows, under the ship. No alien
    /// reaches those rows, so they cover nothing, and they can be read without
    /// looking away from the fight.
    ///
    /// The tier shows as a number of segments rather than a figure. The HUD has
    /// no room for "W2" beside a six-digit score.
    void drawPowerClocks(ui::Canvas& c) {
        clockBar(c, c.h() - 4, _game.widthLeft01(), _game.widthTier(), kWidthColour);
        clockBar(c, c.h() - 2, _game.rateLeft01(), _game.rateTier(), kRateColour);
    }

    static void clockBar(ui::Canvas& c, int y, float left01, int tier,
                         uint16_t colour) {
        if (left01 <= 0.0f || tier < 1) return;
        constexpr int kGap = 3;
        const int span = static_cast<int>(c.w() * left01) - (tier - 1) * kGap;
        if (span <= 0) return;
        const int seg = span / tier;
        if (seg <= 0) return;
        for (int i = 0; i < tier; ++i) c.fillRect(i * (seg + kGap), y, seg, 2, colour);
    }

    void drawStars(ui::Canvas& c) {
        for (const Star& s : _star) {
            const uint16_t col = (s.v > 30.0f)   ? ui::theme::kText
                                 : (s.v > 18.0f) ? ui::theme::kDim
                                                 : ui::theme::kFaint;
            c.drawPixel(s.x, static_cast<int>(s.y), col);
        }
    }

    void drawAliens(ui::Canvas& c) {
        for (int i = 0; i < G::kMaxAliens; ++i) {
            const G::Alien& a = _game.alien(i);
            if (a.state == G::AlienState::Dead) continue;
            const int x = static_cast<int>(a.x - G::kAlienHalfW);
            const int y = static_cast<int>(a.y - G::kAlienHalfH);
            if (y < -8 || y > G::kFieldH) continue;   // re-entering, not here yet
            c.drawBitmap(x, y, spriteFor(a.kind), kAlienSpriteW, kAlienSpriteH,
                         colourFor(a.kind));
        }
    }

    void drawShip(ui::Canvas& c) {
        if (_game.phase() == G::Phase::Dying || _game.over()) return;
        c.drawBitmap(static_cast<int>(_game.shipX() - G::kShipHalfW),
                     static_cast<int>(G::kShipY - G::kShipHalfH), kShip,
                     kShipSpriteW, kShipSpriteH, ui::theme::kCyan);
    }

    void drawShots(ui::Canvas& c) {
        for (int i = 0; i < G::kMaxPlayerShots; ++i) {
            const G::Shot& p = _game.playerShot(i);
            if (!p.alive) continue;
            c.fillRect(static_cast<int>(p.x), static_cast<int>(p.y) - 3, 1, 6,
                       ui::theme::kText);
        }

        for (int i = 0; i < G::kMaxAlienShots; ++i) {
            const G::Shot& s = _game.alienShot(i);
            if (!s.alive) continue;
            c.fillRect(static_cast<int>(s.x), static_cast<int>(s.y) - 2, 2, 5,
                       ui::theme::kWarn);
        }
    }

    void drawBlasts(ui::Canvas& c) {
        for (int i = 0; i < G::kMaxBlasts; ++i) {
            const G::Blast& b = _game.blast(i);
            if (!b.alive) continue;
            // A ring that opens and fades. Two circles read as an explosion at
            // this size and cost less than a sprite sequence.
            const float t = b.age / G::kBlastSec;
            const int   r = 2 + static_cast<int>(t * 6.0f);
            c.drawCircle(static_cast<int>(b.x), static_cast<int>(b.y), r,
                         t < 0.5f ? ui::theme::kAccent : ui::theme::kWarn);
            if (t < 0.6f)
                c.drawCircle(static_cast<int>(b.x), static_cast<int>(b.y), r / 2,
                             ui::theme::kText);
        }
    }

    void drawHud(ui::Canvas& c) {
        c.fillRect(0, 0, c.w(), G::kHudH, ui::theme::kBg);
        c.drawFastHLine(0, G::kHudH, c.w(), ui::theme::kLine);

        c.textf(2, G::kHudH / 2, ui::theme::kText, &fonts::Font0, middle_left,
                "%d", _game.score());
        c.textf(c.w() / 2, G::kHudH / 2, ui::theme::kCyan, &fonts::Font0,
                middle_center, "W%d", _game.wave());

        // Lives show as ships rather than a number, read at a glance mid-dive.
        for (int i = 0; i < _game.lives(); ++i)
            c.drawBitmap(c.w() - 6 - i * 7, 2, kPip, 5, 3, ui::theme::kCyan);

    }

    /// The one line that says what the game is doing when it is not playing.
    void drawOverlay(ui::Canvas& c) {
        const int mid = G::kFieldH / 2;

        if (_paused) {
            banner(c, mid, "PAUSED", ui::theme::kWarn);
            c.textAt(c.w() / 2, mid + 18, "A resumes", ui::theme::kDim,
                     &fonts::Font0, middle_center);
            return;
        }

        switch (_game.phase()) {
            case G::Phase::Ready:
                banner(c, mid, "READY", ui::theme::kCyan);
                // The shape is named because it is what changed, and a player
                // who cannot name it cannot learn it.
                c.textf(c.w() / 2, mid + 18, ui::theme::kDim, &fonts::Font0,
                        middle_center, "WAVE %d   %s", _game.wave(),
                        _game.shape().name);
                break;

            case G::Phase::WaveClear:
                banner(c, mid, "WAVE CLEAR", ui::theme::kOk);
                break;

            case G::Phase::GameOver: {
                banner(c, mid - 20, "GAME OVER", ui::theme::kErr);
                c.textf(c.w() / 2, mid + 4, ui::theme::kText, &fonts::Font2,
                        middle_center, "%d", _game.score());
                c.textf(c.w() / 2, mid + 22, ui::theme::kFaint, &fonts::Font0,
                        middle_center, "wave %d   best %lu", _game.wave(),
                        static_cast<unsigned long>(_best));
                c.textAt(c.w() / 2, mid + 44, "A plays again", ui::theme::kDim,
                         &fonts::Font0, middle_center);
                c.textAt(c.w() / 2, mid + 56, "hold B to leave", ui::theme::kFaint,
                         &fonts::Font0, middle_center);
                break;
            }

            default:
                break;
        }
    }

    static void banner(ui::Canvas& c, int y, const char* text, uint16_t colour) {
        c.fillRect(0, y - 9, c.w(), 18, ui::theme::kBg);
        c.textAt(c.w() / 2, y, text, colour, &fonts::Font2, middle_center);
    }

    // -- state ---------------------------------------------------------------
    Divers _game;
    Star     _star[kStars];

    Screen   _screen = kTitle;
    bool     _paused = false;
    bool     _saved  = false;

    float    _rollDeg   = 0.0f;
    float    _zeroDeg   = 0.0f;
    uint32_t _lastUs    = 0;
    bool     _tiltReady = false;

    uint32_t _best     = 0;
    uint32_t _bestWave = 0;
    uint32_t _games    = 0;
};

SD_REGISTER_APP(DiversApp, kInfo)

}  // namespace sd
