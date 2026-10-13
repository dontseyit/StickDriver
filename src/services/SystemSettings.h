#pragma once
// -----------------------------------------------------------------------------
//  SystemSettings - the preferences that belong to the device, not to a tool.
//
//  Every app keeps its own settings under its own AppInfo::id, which is the
//  right home for a value one tool owns and the wrong home for a decision every
//  tool has to obey: a tool cannot be asked to look in another tool's
//  namespace, and removing that other tool would take the setting with it. So
//  the device has a namespace of its own, loaded once at boot before any app
//  can ask for hardware.
//
//  Library Mode is the first of them: every sound off, in every tool. It is not
//  enforced here. ServiceHub strikes Cap::Speaker from every request while it
//  is on, beside the rule that already says the microphone wins over the
//  speaker, so the amplifier is never powered rather than powered and told to
//  keep quiet.
//
//  Adding a preference is a field, a key, and a row on SYSTEM's SETTINGS page.
//  Brightness and the dim delay are the other two: both belong to the device
//  because the panel is one piece of hardware every tool draws on, and both were
//  constants in the framework before anybody could reach them.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace sd {

class SystemSettings {
public:
    static SystemSettings& instance();

    /// Loads every preference. Called once from ServiceHub::begin(), before the
    /// first capability request.
    void begin();

    /// Every sound off, everywhere, until switched back.
    bool libraryMode() const { return _libraryMode; }
    /// Takes effect on the next capability request and survives a reboot.
    void setLibraryMode(bool on);

    // -- the panel ------------------------------------------------------------
    //  Four numbers, because one of them was never enough: how bright it runs,
    //  how long it stays that way, how long before it goes dark, and how far it
    //  drops in between. The dim level is a reduction OF the brightness rather
    //  than a level of its own, so lowering the brightness lowers both.

    /// The steps SCREEN cycles brightness through, lowest first. Perceived
    /// brightness is not linear, but five even steps are what a person can aim
    /// at with one button.
    static constexpr uint8_t kBrightSteps[] = {20, 40, 60, 80, 100};
    /// Idle seconds before the panel goes dark; kNeverSleep keeps it lit.
    static constexpr uint16_t kSleepSteps[] = {30, 60, 120, 900, 0xFFFF};
    /// Idle seconds before it dims.
    static constexpr uint16_t kDimSteps[] = {10, 30, 60};
    /// How much brightness the dim takes away, as a percentage of it.
    static constexpr uint8_t kDimBySteps[] = {10, 50, 80};

    static constexpr uint16_t kNeverSleep = 0xFFFF;

    /// Panel brightness, as the percentage SCREEN shows. Stored as a percentage
    /// rather than the panel's own 0-255, so the number somebody chose is the
    /// number that comes back.
    uint8_t brightnessPct() const { return _brightnessPct; }
    /// Rounded to the nearest step: a panel at zero reads as a dead device
    /// rather than as a setting.
    void setBrightnessPct(uint8_t pct);
    /// The same brightness as M5.Display.setBrightness() wants it.
    uint8_t brightness() const;

    /// Seconds without a press or a pick-up before the panel dims. An app may
    /// still name its own; see AppInfo::dimAfterSec.
    uint16_t dimAfterSec() const { return _dimAfterSec; }
    void setDimAfterSec(uint16_t sec);

    /// Seconds before it goes dark altogether. A sleep no later than the dim
    /// leaves the dim no time to happen, and the panel goes straight to dark.
    uint16_t sleepAfterSec() const { return _sleepAfterSec; }
    void setSleepAfterSec(uint16_t sec);

    /// What the dim takes away, as a percentage of the brightness. 80 means the
    /// panel drops to a fifth of whatever it was running at.
    uint8_t dimByPct() const { return _dimByPct; }
    void setDimByPct(uint8_t pct);
    /// The brightness a dimmed panel runs at. Never zero: zero is dark, and a
    /// dim that reached it would be a sleep.
    uint8_t dimBrightness() const;

private:
    SystemSettings() = default;

    bool     _libraryMode  = false;
    /// 110 of 255 was the constant in main() before this was reachable, and 40%
    /// is 102 - the nearest step, and indistinguishable from it.
    uint8_t  _brightnessPct = 40;
    /// 30 s, 2 min and a drop to a fifth are the nearest steps to the 45 s, 180 s
    /// and brightness 12 the framework used before any of this was reachable.
    uint16_t _dimAfterSec   = 30;
    uint16_t _sleepAfterSec = 120;
    uint8_t  _dimByPct      = 80;
};

inline SystemSettings& systemSettings() { return SystemSettings::instance(); }

}  // namespace sd
