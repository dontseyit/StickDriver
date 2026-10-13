#include "mathx/Divers.h"

#include <cmath>

namespace sd {
namespace {

constexpr float kPi    = 3.14159265f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kDeg   = 180.0f / kPi;
constexpr float kRad   = kPi / 180.0f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float minf(float a, float b) { return a < b ? a : b; }
float maxf(float a, float b) { return a > b ? a : b; }

/// An angle folded into -180..180, which is the form a turn wants: the sign
/// says which way round is shorter.
float wrap180(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

float wrap360(float deg) {
    while (deg >= 360.0f) deg -= 360.0f;
    while (deg < 0.0f) deg += 360.0f;
    return deg;
}

bool overlaps(float ax, float ay, float ahw, float ahh,
              float bx, float by, float bhw, float bhh) {
    return std::fabs(ax - bx) <= (ahw + bhw) && std::fabs(ay - by) <= (ahh + bhh);
}

}  // namespace

// -----------------------------------------------------------------------------
Divers::WaveSpec Divers::waveSpec(int wave) {
    if (wave < 1) wave = 1;
    const float w = static_cast<float>(wave - 1);

    WaveSpec s{};
    s.diveSpeed       = minf(52.0f + 6.0f * w, 100.0f);
    s.diveGapSec      = maxf(2.2f - 0.15f * w, 0.7f);
    s.diverFirePerSec = minf(0.6f + 0.12f * w, 1.6f);
    s.alienShotSpeed  = minf(70.0f + 4.0f * w, 110.0f);
    s.swayHz          = minf(0.15f + 0.02f * w, 0.35f);

    s.maxDivers = 1 + (wave - 1) / 2;
    if (s.maxDivers > 4) s.maxDivers = 4;
    return s;
}

int Divers::scoreFor(AlienKind kind, bool diving, int escortsAlive) {
    switch (kind) {
        case AlienKind::Blue:   return diving ? 60 : 30;
        case AlienKind::Purple: return diving ? 80 : 40;
        case AlienKind::Red:    return diving ? 100 : 50;
        case AlienKind::Flagship:
            if (!diving) return 60;
            if (escortsAlive >= 2) return 300;
            if (escortsAlive == 1) return 200;
            return 150;
    }
    return 0;
}

float Divers::lateralDeg(const Vec3& accel) {
    const float n = accel.norm();
    if (n < 0.2f) return 0.0f;   // freefall or a shake: no up to measure from
    // Negated: measured on the device, body +X points screen LEFT at this
    // rotation, so the raw component sends the ship the way the high edge went.
    return -std::asin(clampf(accel.x / n, -1.0f, 1.0f)) * kDeg;
}

float Divers::fuseTilt(float prevDeg, float accelDeg, float gyroDps, float dt) {
    if (dt <= 0.0f || dt > 0.5f) return accelDeg;
    const float turned = prevDeg + kGyroSign * gyroDps * dt;
    return turned + (accelDeg - turned) * (1.0f - std::exp(-dt / kFuseTauSec));
}

float Divers::shipXForTilt(float rollDeg, float zeroDeg) {
    float d = rollDeg - zeroDeg;
    if (d > -kDeadbandDeg && d < kDeadbandDeg) d = 0.0f;
    else d -= (d > 0.0f ? kDeadbandDeg : -kDeadbandDeg);

    // Clamped before the curve, not after: cubing a number past 1 runs away,
    // and clamping the result instead would flatten the end of the travel
    // into a place where more wrist does nothing.
    const float u = clampf(d / (kFullTiltDeg - kDeadbandDeg), -1.0f, 1.0f);
    const float v = kExpoLinear * u + (1.0f - kExpoLinear) * u * u * u;
    return kFieldW * 0.5f + v * kTravelPx;
}

// -----------------------------------------------------------------------------
void Divers::reset(uint32_t seed) {
    _rng = seed ? seed : 1u;   // xorshift is stuck at zero

    _score = 0;
    _wave  = 1;
    _lives = kLives;
    _dives = 0;

    _shipX      = kFieldW * 0.5f;
    _cooldown   = 0.0f;
    _swayPhase  = 0.0f;
    _diveTimer  = 0.0f;
    _phaseTimer = kReadySec;

    for (Shot& p : _shot) p = Shot{};
    for (Shot& s : _alienShot) s = Shot{};
    for (Blast& b : _blast) b = Blast{};
    for (Capsule& c : _capsule) c = Capsule{};

    _widthTier = 1;
    _rateTier  = 1;
    _widthSec  = 0.0f;
    _rateSec   = 0.0f;

    buildFormation();
    _phase = Phase::Ready;
}

const Divers::Shape& Divers::shapeFor(int wave) {
    if (wave < 1) wave = 1;
    return kShapes[(wave - 1) % kShapeCount];
}

void Divers::buildFormation() {
    const Shape& sh = shapeFor(_wave);

    int n = 0;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols && n < kMaxAliens; ++c) {
            AlienKind kind;
            switch (sh.row[r][c]) {
                case 'F': kind = AlienKind::Flagship; break;
                case 'R': kind = AlienKind::Red;      break;
                case 'P': kind = AlienKind::Purple;   break;
                case 'B': kind = AlienKind::Blue;     break;
                default:  continue;   // '.', and anything mistyped
            }
            Alien& a = _alien[n++];
            a = Alien{};
            a.row   = static_cast<uint8_t>(r);
            a.col   = static_cast<uint8_t>(c);
            a.kind  = kind;
            a.state = AlienState::Formation;
            a.x = slotX(a);
            a.y = slotY(a);
        }
    }
    // Whatever the shape did not use. A stale alien left here would be shot
    // at in a wave it does not belong to.
    for (; n < kMaxAliens; ++n) _alien[n] = Alien{};

    for (Capsule& cp : _capsule) cp.alive = false;
}

float Divers::slotX(const Alien& a) const {
    const float sway = kSwayPx * std::sin(_swayPhase);
    return kFormLeft + a.col * kColPitch + kColPitch * 0.5f + sway;
}

float Divers::slotY(const Alien& a) const {
    return kFormTop + a.row * kRowPitch;
}

int Divers::alive() const {
    int n = 0;
    for (const Alien& a : _alien)
        if (a.state != AlienState::Dead) ++n;
    return n;
}

int Divers::diving() const {
    int n = 0;
    for (const Alien& a : _alien)
        if (a.state == AlienState::Diving || a.state == AlienState::Returning) ++n;
    return n;
}

float Divers::rand01() {
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return static_cast<float>(_rng >> 8) * (1.0f / 16777216.0f);
}

// -----------------------------------------------------------------------------
void Divers::update(float dt, float shipX) {
    if (dt <= 0.0f) return;
    if (dt > kMaxStepSec) dt = kMaxStepSec;

    _shipX = clampf(shipX, kShipMargin, kFieldW - kShipMargin);

    switch (_phase) {
        case Phase::Ready:
            stepFormation(dt);
            _phaseTimer -= dt;
            if (_phaseTimer <= 0.0f) {
                _phase     = Phase::Playing;
                _diveTimer = waveSpec(_wave).diveGapSec;
            }
            break;

        case Phase::Playing:
            stepPlay(dt);
            break;

        case Phase::Dying:
            stepFormation(dt);
            _phaseTimer -= dt;
            if (_phaseTimer <= 0.0f) {
                if (_lives <= 0) {
                    _phase = Phase::GameOver;
                } else {
                    _phase      = Phase::Ready;
                    _phaseTimer = kReadySec;
                }
            }
            break;

        case Phase::WaveClear:
            _phaseTimer -= dt;
            if (_phaseTimer <= 0.0f) {
                ++_wave;
                _dives = 0;
                buildFormation();
                _phase      = Phase::Ready;
                _phaseTimer = kReadySec;
            }
            break;

        case Phase::GameOver:
            break;
    }

    stepBlasts(dt);
}

void Divers::stepPlay(float dt) {
    const WaveSpec spec = waveSpec(_wave);

    stepFormation(dt);

    _diveTimer -= dt;
    if (_diveTimer <= 0.0f) {
        launchDive(spec);
        _diveTimer = spec.diveGapSec;
    }

    stepDivers(dt, spec);
    stepPower(dt);
    stepCapsules(dt);

    // Automatic fire, held to one VOLLEY in the air whatever its width.
    _cooldown -= dt;
    if (!volleyAlive() && _cooldown <= 0.0f) fire();

    // Sampled after the shot, so a volley that is born and spent inside one
    // frame still reloads.
    const bool hadVolley = volleyAlive();

    stepShots(dt);
    collide();

    // THE RELOAD, in one place. A pellet is spent by leaving the panel or by
    // hitting something, and the gun answers when the last of them is,
    // whichever way that happened. Written once because it is the rule the
    // whole difficulty rests on: two copies drift, and the drift would be a
    // silent change to the fire rate.
    if (hadVolley && !volleyAlive()) _cooldown = cooldownSec();

    if (alive() == 0) {
        _phase      = Phase::WaveClear;
        _phaseTimer = kWaveClearSec;
    }
}

void Divers::stepFormation(float dt) {
    _swayPhase += kTwoPi * waveSpec(_wave).swayHz * dt;
    if (_swayPhase >= kTwoPi) _swayPhase -= kTwoPi;

    for (Alien& a : _alien) {
        if (a.state != AlienState::Formation) continue;
        a.x = slotX(a);
        a.y = slotY(a);
    }
}

void Divers::stepDivers(float dt, const WaveSpec& spec) {
    // Leaders first: an escort's position is read off its leader's, so the
    // leader has to have moved before the escort is placed.
    for (int i = 0; i < kMaxAliens; ++i) {
        Alien& a = _alien[i];
        if (a.state != AlienState::Diving || a.leader >= 0) continue;

        // Steer towards the deck under the ship at a limited rate: that limit
        // is what makes the break-out an arc instead of a corner. Past the
        // commit line it holds the heading, and the weave alone carries it in.
        if (a.y < kCommitY) {
            const float target = std::atan2(kDiveAimY - a.y, _shipX - a.x) * kDeg;
            const float maxTurn = kTurnRateDps * dt;
            a.headingDeg = wrap360(
                a.headingDeg + clampf(wrap180(target - a.headingDeg), -maxTurn, maxTurn));
        }

        a.weavePhase += kTwoPi * kWeaveHz * dt;
        const float h = (a.headingDeg + kWeaveAmpDeg * std::sin(a.weavePhase)) * kRad;
        a.x += std::cos(h) * spec.diveSpeed * dt;
        a.y += std::sin(h) * spec.diveSpeed * dt;

        // Only what is on the panel shoots, and only at the player's half of
        // the screen: a bullet from an alien nobody can see is not a fight.
        if (a.y > kHudH && a.y < kFieldH && rand01() < spec.diverFirePerSec * dt) {
            for (Shot& s : _alienShot) {
                if (s.alive) continue;
                s.x     = a.x;
                s.y     = a.y + kAlienHalfH;
                s.vy    = spec.alienShotSpeed;
                s.alive = true;
                break;
            }
        }

        if (a.y > kExitY) {
            // Round again: in at the top, in its own column. Escorts are cut
            // loose here rather than re-entering in formation with it.
            for (Alien& e : _alien)
                if (e.leader == i) e.leader = -1;
            a.state      = AlienState::Returning;
            a.leader     = -1;
            a.x          = slotX(a);
            a.y          = kEntryY;
            a.headingDeg = 90.0f;
        }
    }

    // Escorts, holding station in the leader's frame so the group banks as one.
    for (Alien& a : _alien) {
        if (a.state != AlienState::Diving || a.leader < 0) continue;
        const Alien& lead = _alien[a.leader];
        if (lead.state != AlienState::Diving) { a.leader = -1; continue; }

        const float h = lead.headingDeg * kRad;
        const float fx = std::cos(h), fy = std::sin(h);
        a.headingDeg = lead.headingDeg;
        a.x = lead.x - fy * a.offX - fx * kEscortOffY;
        a.y = lead.y + fx * a.offX - fy * kEscortOffY;

        if (a.y > kExitY) {
            a.state      = AlienState::Returning;
            a.leader     = -1;
            a.x          = slotX(a);
            a.y          = kEntryY;
            a.headingDeg = 90.0f;
        }
    }

    // Coming back in, straight to the slot it left.
    for (Alien& a : _alien) {
        if (a.state != AlienState::Returning) continue;
        const float tx = slotX(a), ty = slotY(a);
        const float dx = tx - a.x, dy = ty - a.y;
        const float d  = std::sqrt(dx * dx + dy * dy);
        const float step = spec.diveSpeed * dt;
        if (d <= kRejoinPx || step >= d) {
            a.state = AlienState::Formation;
            a.x = tx;
            a.y = ty;
            continue;
        }
        a.x += dx * (step / d);
        a.y += dy * (step / d);
    }
}

void Divers::launchDive(const WaveSpec& spec) {
    if (diving() >= spec.maxDivers) return;

    ++_dives;
    const bool wantFlagship = (_dives % kFlagshipEveryNthDive) == 0;

    // Reservoir sampling: one pass, no list of candidates to build.
    int  pick = -1;
    int  seen = 0;
    for (int i = 0; i < kMaxAliens; ++i) {
        const Alien& a = _alien[i];
        if (a.state != AlienState::Formation) continue;
        if ((a.kind == AlienKind::Flagship) != wantFlagship) continue;
        if (rand01() * static_cast<float>(++seen) < 1.0f) pick = i;
    }
    // A flagship's turn with none left, or nothing but flagships on the board.
    if (pick < 0) {
        seen = 0;
        for (int i = 0; i < kMaxAliens; ++i) {
            if (_alien[i].state != AlienState::Formation) continue;
            if (rand01() * static_cast<float>(++seen) < 1.0f) pick = i;
        }
    }
    if (pick < 0) return;

    Alien& a = _alien[pick];
    a.state      = AlienState::Diving;
    a.leader     = -1;
    a.weavePhase = rand01() * kTwoPi;
    // Peels away from the middle, so the loop happens over empty panel rather
    // than through the rest of the formation.
    a.headingDeg = (a.x < kFieldW * 0.5f) ? 210.0f : 330.0f;

    if (a.kind != AlienKind::Flagship) return;

    // Two escorts, the reds nearest its column, one either side.
    for (int n = 0; n < 2; ++n) {
        int best = -1, bestDist = 0;
        for (int i = 0; i < kMaxAliens; ++i) {
            const Alien& e = _alien[i];
            if (e.state != AlienState::Formation || e.kind != AlienKind::Red) continue;
            const int d = (e.col > a.col) ? (e.col - a.col) : (a.col - e.col);
            if (best < 0 || d < bestDist) { best = i; bestDist = d; }
        }
        if (best < 0) return;
        Alien& e = _alien[best];
        e.state      = AlienState::Diving;
        e.leader     = static_cast<int8_t>(pick);
        e.offX       = (n == 0) ? -kEscortOffX : kEscortOffX;
        e.headingDeg = a.headingDeg;
    }
}

bool Divers::volleyAlive() const {
    for (const Shot& p : _shot)
        if (p.alive) return true;
    return false;
}

void Divers::fire() {
    const float spread = kWidthSpreadPx[_widthTier - 1];
    const float y      = kShipY - kShipHalfH - kShotHalfH;

    for (int i = 0; i < kMaxPlayerShots; ++i) _shot[i] = Shot{};

    // One pellet on the centre line, two straddling it, three across. Drawn
    // from the tier rather than a table of positions so a fourth tier would
    // be one number.
    for (int i = 0; i < _widthTier; ++i) {
        const float off = (_widthTier == 1)
                              ? 0.0f
                              : -spread + 2.0f * spread * static_cast<float>(i) /
                                              static_cast<float>(_widthTier - 1);
        _shot[i].x     = _shipX + off;
        _shot[i].y     = y;
        _shot[i].vy    = -shotSpeed();
        _shot[i].alive = true;
    }
}

void Divers::stepShots(float dt) {
    for (Shot& p : _shot) {
        if (!p.alive) continue;
        p.y += p.vy * dt;
        if (p.y < static_cast<float>(kHudH)) p.alive = false;
    }
    for (Shot& s : _alienShot) {
        if (!s.alive) continue;
        s.y += s.vy * dt;
        if (s.y > static_cast<float>(kFieldH)) s.alive = false;
    }
}

// -----------------------------------------------------------------------------
void Divers::stepPower(float dt) {
    if (_widthSec > 0.0f) {
        _widthSec -= dt;
        if (_widthSec <= 0.0f) { _widthSec = 0.0f; _widthTier = 1; }
    }
    if (_rateSec > 0.0f) {
        _rateSec -= dt;
        if (_rateSec <= 0.0f) { _rateSec = 0.0f; _rateTier = 1; }
    }
}

void Divers::stepCapsules(float dt) {
    for (Capsule& c : _capsule) {
        if (!c.alive) continue;
        c.y += kCapsuleSpeed * dt;
        if (c.y > static_cast<float>(kFieldH)) { c.alive = false; continue; }
        if (!overlaps(c.x, c.y, kCapsuleHalfW, kCapsuleHalfH,
                      _shipX, kShipY, kShipHalfW, kShipHalfH)) continue;
        c.alive = false;
        take(c.kind);
    }
}

void Divers::dropCapsule(const Alien& a) {
    if (rand01() >= kDropChance[static_cast<int>(a.kind)]) return;
    for (Capsule& c : _capsule) {
        if (c.alive) continue;
        c.x     = a.x;
        c.y     = a.y;
        c.kind  = (rand01() < 0.5f) ? PowerKind::Width : PowerKind::Rate;
        c.alive = true;
        return;
    }
    // Three already falling and none caught: no room, and no capsule. Better
    // than stealing one the player is under.
}

void Divers::take(PowerKind k) {
    if (k == PowerKind::Width) {
        if (_widthTier < kMaxTier) ++_widthTier;
        _widthSec = kPowerSec;
    } else {
        if (_rateTier < kMaxTier) ++_rateTier;
        _rateSec = kPowerSec;
    }
}

void Divers::collide() {
    for (Shot& p : _shot) {
        if (!p.alive) continue;
        for (int i = 0; i < kMaxAliens; ++i) {
            const Alien& a = _alien[i];
            if (a.state == AlienState::Dead) continue;
            if (!overlaps(p.x, p.y, kShotHalfW, kShotHalfH,
                          a.x, a.y, kAlienHalfW, kAlienHalfH)) continue;
            hit(i);
            // Spent. When the last of them is, stepPlay reloads.
            p.alive = false;
            break;
        }
    }

    for (Shot& s : _alienShot) {
        if (!s.alive) continue;
        if (!overlaps(s.x, s.y, kShotHalfW, kShotHalfH,
                      _shipX, kShipY, kShipHalfW, kShipHalfH)) continue;
        s.alive = false;
        loseLife();
        return;
    }

    // A diver that reaches the ship takes it with it, and scores nothing: the
    // player did not shoot it down.
    for (int i = 0; i < kMaxAliens; ++i) {
        Alien& a = _alien[i];
        if (a.state != AlienState::Diving && a.state != AlienState::Returning) continue;
        if (!overlaps(a.x, a.y, kAlienHalfW, kAlienHalfH,
                      _shipX, kShipY, kShipHalfW, kShipHalfH)) continue;
        a.state = AlienState::Dead;
        addBlast(a.x, a.y);
        loseLife();
        return;
    }
}

void Divers::hit(int index) {
    Alien& a = _alien[index];
    const bool diving = (a.state == AlienState::Diving);

    int escorts = 0;
    if (a.kind == AlienKind::Flagship && diving) {
        for (const Alien& e : _alien)
            if (e.leader == index && e.state == AlienState::Diving) ++escorts;
    }
    _score += scoreFor(a.kind, diving, escorts);

    // Whatever it was leading is on its own now.
    for (Alien& e : _alien)
        if (e.leader == index) e.leader = -1;

    a.state  = AlienState::Dead;
    a.leader = -1;
    addBlast(a.x, a.y);
    // Only from a kill the player made: hit() is reached from the player's
    // pellets alone, and a diver that crashes into the ship drops nothing.
    dropCapsule(a);
}

void Divers::loseLife() {
    --_lives;
    addBlast(_shipX, kShipY);
    clearDivers();
    for (Shot& p : _shot) p.alive = false;
    for (Shot& s : _alienShot) s.alive = false;
    _phase      = Phase::Dying;
    _phaseTimer = kDyingSec;
}

void Divers::clearDivers() {
    for (Alien& a : _alien) {
        if (a.state != AlienState::Diving && a.state != AlienState::Returning) continue;
        a.state  = AlienState::Formation;
        a.leader = -1;
        a.x = slotX(a);
        a.y = slotY(a);
    }
}

void Divers::addBlast(float x, float y) {
    Blast* slot = nullptr;
    for (Blast& b : _blast) {
        if (!b.alive) { slot = &b; break; }
        // Nothing free: the oldest one has been seen the longest.
        if (!slot || b.age > slot->age) slot = &b;
    }
    slot->x     = x;
    slot->y     = y;
    slot->age   = 0.0f;
    slot->alive = true;
}

void Divers::stepBlasts(float dt) {
    for (Blast& b : _blast) {
        if (!b.alive) continue;
        b.age += dt;
        if (b.age >= kBlastSec) b.alive = false;
    }
}

}  // namespace sd
