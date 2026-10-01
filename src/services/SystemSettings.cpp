#include "services/SystemSettings.h"

#include "core/Log.h"
#include "services/Settings.h"

#include <cstddef>

namespace sd {
namespace {

/// Not any app's id, and no app may take it: an app's namespace goes when the
/// app does, and these have to outlive every one of them.
constexpr const char* kNs = "device";

constexpr const char* kLibraryKey = "library";
constexpr const char* kBrightKey  = "bright";
constexpr const char* kDimKey     = "dim";
constexpr const char* kSleepKey   = "sleep";
constexpr const char* kDimByKey   = "dimby";

/// The step nearest `want` in `steps`, so a value the list does not hold - an
/// older firmware's, or one written over the console - still lands on a row the
/// page can draw.
template <typename T, size_t N>
T nearestIn(const T (&steps)[N], long want) {
    T   best = steps[0];
    long gap = 0x7FFFFFFF;
    for (T step : steps) {
        const long d = want > step ? want - step : step - want;
        if (d < gap) { gap = d; best = step; }
    }
    return best;
}

}  // namespace

SystemSettings& SystemSettings::instance() {
    static SystemSettings s;
    return s;
}

void SystemSettings::begin() {
    Settings s(kNs, /*readOnly*/ true);
    if (!s.ok()) return;
    _libraryMode = s.getBool(kLibraryKey, false);
    if (_libraryMode) SD_LOGI("device", "library mode on - speaker withheld");

    // Clamped on the way in as well as on the way out: a partition written by an
    // older firmware, or by hand over the console, is not a promise.
    _brightnessPct  = nearestIn(SystemSettings::kBrightSteps,
                                s.getInt(kBrightKey, _brightnessPct));
    _dimAfterSec    = nearestIn(SystemSettings::kDimSteps,
                                s.getInt(kDimKey, _dimAfterSec));
    _sleepAfterSec  = nearestIn(SystemSettings::kSleepSteps,
                                s.getInt(kSleepKey, _sleepAfterSec));
    _dimByPct       = nearestIn(SystemSettings::kDimBySteps,
                                s.getInt(kDimByKey, _dimByPct));
}

void SystemSettings::setLibraryMode(bool on) {
    if (on == _libraryMode) return;
    _libraryMode = on;
    // Written now rather than on some exit path: a preference is set once and
    // expected to hold, and no later moment is more certain to happen.
    Settings s(kNs, /*readOnly*/ false);
    if (s.ok()) s.putBool(kLibraryKey, on);
    SD_LOGI("device", "library mode %s", on ? "on" : "off");
}

uint8_t SystemSettings::brightness() const {
    return static_cast<uint8_t>(_brightnessPct * 255u / 100u);
}

uint8_t SystemSettings::dimBrightness() const {
    const unsigned left = brightness() * (100u - _dimByPct) / 100u;
    return static_cast<uint8_t>(left < 1u ? 1u : left);
}

void SystemSettings::setBrightnessPct(uint8_t pct) {
    const uint8_t want = nearestIn(kBrightSteps, pct);
    if (want == _brightnessPct) return;
    _brightnessPct = want;
    Settings s(kNs, /*readOnly*/ false);
    if (s.ok()) s.putInt(kBrightKey, want);
}

void SystemSettings::setDimAfterSec(uint16_t sec) {
    if (sec == _dimAfterSec) return;
    _dimAfterSec = sec;
    Settings s(kNs, /*readOnly*/ false);
    if (s.ok()) s.putInt(kDimKey, sec);
}

void SystemSettings::setSleepAfterSec(uint16_t sec) {
    if (sec == _sleepAfterSec) return;
    _sleepAfterSec = sec;
    Settings s(kNs, /*readOnly*/ false);
    if (s.ok()) s.putInt(kSleepKey, sec);
}

void SystemSettings::setDimByPct(uint8_t pct) {
    if (pct == _dimByPct) return;
    _dimByPct = pct;
    Settings s(kNs, /*readOnly*/ false);
    if (s.ok()) s.putInt(kDimByKey, pct);
}

}  // namespace sd
