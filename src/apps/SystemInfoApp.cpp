// -----------------------------------------------------------------------------
//  System - board vitals, on three pages.
//
//  VITALS is what the board is made of and what it is doing. THERMAL is the
//  dies as read, plus how much of their heat the device made itself. SETTINGS
//  holds the preferences no single tool owns and the device-level tools that
//  name this app as their parent. CALIBRATION is the first of those, and this
//  file was not edited to learn about it: the list comes from the registry.
//
//  There is no room temperature here. The board self-heats by some forty
//  degrees, which is larger than the whole range a room reading would live in,
//  so the dies are shown as dies and the figure under them measures the
//  hardware rather than the air. See mathx/Thermal.h for the arithmetic.
// -----------------------------------------------------------------------------
#include "core/App.h"
#include "core/AppManager.h"
#include "core/AppRegistry.h"
#include "mathx/Thermal.h"
#include "services/ServiceHub.h"
#include "services/SystemSettings.h"
#include "services/ThermalService.h"
#include "ui/Widgets.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <nvs.h>

#include <cstring>

namespace sd {
namespace {

constexpr AppInfo kInfo{
    .id       = "sysinfo",
    .title    = "SYSTEM",
    .subtitle = "board vitals",
    .group    = "SYS",
    .needs    = Cap::Display | Cap::Imu,
    .rotation = 1,
    .tickHz   = 10,
    .drawHz   = 5,
    .accent   = ui::theme::kCyan,
    .what     = "Board vitals, the dies as read, SCREEN for the panel, SETTINGS for the rest.",
    .how      = "B pages. A beeps; hold A on THERMAL forgets the warm-up; on SCREEN and SETTINGS A moves, hold A acts.",
    .care     = "No die is the air. Library Mode holds across a reboot; the LIB tag says it is on.",
};

enum Page : uint8_t { kVitals = 0, kThermal, kScreen, kSettings, kPageCount };

/// SCREEN rows: everything about the panel, in the order the three timings
/// happen - how bright, how long bright, how long before dark, how far down in
/// between.
enum { kScrBright = 0, kScrDim, kScrSleep, kScrDimBy, kScrRows };

/// SETTINGS rows: the preferences that are not about the panel, then one per
/// tool that named this app as its parent.
enum { kRowLibrary = 0, kFixedRows };

/// Seconds as the row shows them: never, minutes where they divide, seconds
/// otherwise.
void writeDuration(char* out, size_t cap, uint16_t sec) {
    if (sec == SystemSettings::kNeverSleep) {
        std::snprintf(out, cap, "never");
    } else if (sec >= 60 && sec % 60 == 0) {
        std::snprintf(out, cap, "%um", static_cast<unsigned>(sec / 60));
    } else {
        std::snprintf(out, cap, "%us", static_cast<unsigned>(sec));
    }
}

template <typename T, size_t N>
T nextStep(const T (&steps)[N], T cur) {
    for (size_t i = 0; i < N; ++i)
        if (steps[i] == cur) return steps[(i + 1) % N];
    return steps[0];
}

class SystemInfoApp : public App {
public:
    void onStart() override {
        buildChildren();
        _rows.setCount(kFixedRows + _childCount);
        // Coming back from CALIBRATION should land on the row that opened it.
        _rows.select(_settingsRow);
        _screen.setCount(kScrRows);
        _screen.select(_screenRow);
    }

    void onStop() override {
        services().audio().stop();
        apps().requestCaps(0);
    }

    void onTick(uint32_t) override { services().imu().poll(); }

    /// A is a speaker self-test. Audio failures here are silent: a mis-detected
    /// board, an unconfigured codec and a working speaker with nothing driving
    /// it all sound alike, so one place answers "is the hardware alive?".
    bool onEvent(const InputEvent& ev) override {
        if (ev.click(Btn::B)) {
            _page = static_cast<Page>((_page + 1) % kPageCount);
            return true;
        }
        if (_page == kThermal) {
            if (ev.longPress(Btn::A)) {
                services().thermal().forget();
                apps().toast("warm-up forgotten", ui::theme::kWarn);
                return true;
            }
            // A plain press does nothing here: the beep belongs to VITALS, and
            // one button meaning two things by page is worse than a quiet one.
            return false;
        }
        if (_page == kScreen) {
            // B is spoken for by the pages, so A does both jobs: a click moves
            // the cursor, a hold changes the row it is over. Moving is what you
            // do most and takes the cheaper gesture; changing something stored
            // is the committing one and takes the deliberate gesture. A hold
            // suppresses the click that would otherwise follow it, so one press
            // of A never does both, and Repeat is left alone so a long hold
            // steps once rather than spinning through the values.
            if (ev.click(Btn::A)) {
                _screen.next();
                _screenRow = _screen.selected();
                return true;
            }
            if (!ev.longPress(Btn::A)) return false;
            auto& set = systemSettings();
            switch (_screen.selected()) {
                case kScrBright:
                    set.setBrightnessPct(
                        nextStep(SystemSettings::kBrightSteps, set.brightnessPct()));
                    // Applied here rather than waited for: the panel is what the
                    // setting is about, so it has to answer the press.
                    M5.Display.setBrightness(set.brightness());
                    break;
                case kScrDim:
                    set.setDimAfterSec(
                        nextStep(SystemSettings::kDimSteps, set.dimAfterSec()));
                    break;
                case kScrSleep:
                    set.setSleepAfterSec(
                        nextStep(SystemSettings::kSleepSteps, set.sleepAfterSec()));
                    break;
                case kScrDimBy:
                    set.setDimByPct(
                        nextStep(SystemSettings::kDimBySteps, set.dimByPct()));
                    break;
                default: break;
            }
            return true;
        }
        if (_page == kSettings) {
            // As SCREEN: a click moves, a hold acts on the row.
            if (ev.click(Btn::A)) {
                _rows.next();
                _settingsRow = _rows.selected();
                return true;
            }
            if (!ev.longPress(Btn::A)) return false;
            if (_rows.selected() >= kFixedRows) {
                apps().launch(childInfo(_rows.selected() - kFixedRows).id);
                return true;
            }
            const bool on = !systemSettings().libraryMode();
            systemSettings().setLibraryMode(on);
            // The speaker may still be held from a test beep two pages ago, so
            // this is the ordinary path. Re-asking for the declared set runs
            // the hub's rules again, and release() powers the amplifier down.
            apps().requestCaps(0);
            apps().toast(on ? "library mode on" : "library mode off",
                         on ? ui::theme::kWarn : ui::theme::kOk);
            return true;
        }
        if (!ev.click(Btn::A)) return false;
        apps().requestCaps(caps(Cap::Speaker));
        auto& audio = services().audio();
        audio.setVolumePercent(80);
        if (audio.available()) {
            audio.beep(1000.0f, 200);
            apps().toast("beep", ui::theme::kOk);
        } else if (systemSettings().libraryMode()) {
            // Withheld, not broken: this button answers "is the hardware
            // alive?", so it must not say no for a setting.
            apps().toast("library mode is on", ui::theme::kWarn, 2000);
        } else {
            apps().toast("speaker unavailable", ui::theme::kErr, 2000);
        }
        return true;
    }

    void onDraw(ui::Canvas& c) override {
        if (_page == kThermal)  { drawThermal(c); return; }
        if (_page == kScreen)   { drawScreen(c); return; }
        if (_page == kSettings) { drawSettings(c); return; }
        // Ten rows have to land above the footer hairline at 123. Text has a
        // transparent background here, so an overlap interleaves rather than
        // covering.
        int y = ui::theme::kBarH + 4;
        const int lh = 11;

        auto& pwr = services().power();
        auto& imu = services().imu();

        row(c, y, "SoC",   "ESP32-S3 @ %u MHz", (unsigned)getCpuFrequencyMhz()); y += lh;
        // NVS beside the flash size: it is the one store here that can fill,
        // and when it did every app failed to save at once, WiFi would not
        // start, and the only trace was Arduino core logging nobody watched.
        row(c, y, "Flash", "%u MB   nvs %s", (unsigned)(ESP.getFlashChipSize() >> 20),
            nvsUse());                                                         y += lh;
        row(c, y, "PSRAM", "%u / %u KB free",
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10),
            (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) >> 10));  y += lh;
        row(c, y, "Heap",  "%u KB free", (unsigned)(ESP.getFreeHeap() >> 10)); y += lh;
        row(c, y, "Batt",  "%d %%  %.2f V%s", pwr.percent(), pwr.volts(),
            pwr.charging() ? "  CHG" : "");                                    y += lh;
        row(c, y, "IMU",   "%s @ %.0f Hz", imu.sensorName(), imu.rateHz());    y += lh;
        row(c, y, "5V in", "%.2f V   boost %s", pwr.ext5vInVolts(),
            pwr.ext5vBoostOn() ? "on" : "off");                                 y += lh;
        row(c, y, "Board", "%s", boardName());                                 y += lh;
        row(c, y, "Audio", "codec %s   amp %s   %u%%",
            M5.Speaker.isEnabled() ? "ok" : "NOT CFG",
            systemSettings().libraryMode()   ? "withheld"
            : services().audio().available() ? "on" : "off",
            services().audio().volumePercent());                               y += lh;
        row(c, y, "Up",    "%lu s", (unsigned long)(millis() / 1000));
    }


    // -- thermal --------------------------------------------------------------
    /// The dies as read, and the one figure derived from them: how much of that
    /// heat the board made itself. Shown with its working, including whether
    /// the session began cold enough for the figure to be measured rather than
    /// assumed, since a derived number with no provenance looks like a guess.
    void drawThermal(ui::Canvas& c) {
        auto&                 th = services().thermal();
        const ThermalReading& r  = th.reading();
        const ThermalModel&   m  = th.model();

        int y = ui::theme::kBarH + 8;
        for (int i = 0; i < kTempSourceCount; ++i) {
            const TempSource src = static_cast<TempSource>(i);
            c.textAt(6, y, tempSourceName(src), ui::theme::kCyan, &fonts::Font0,
                     middle_left);

            if (src == TempSource::Pmic) {
                // Millivolts, and said so: the part publishes no conversion to
                // degrees, so nothing here invents one.
                uint16_t mv = 0;
                if (th.pmicMillivolts(mv)) {
                    c.textf(34, y, ui::theme::kFaint, &fonts::Font0, middle_left,
                            "%u mV raw - not a temperature", (unsigned)mv);
                } else {
                    c.textAt(34, y, "no answer", ui::theme::kFaint, &fonts::Font0,
                             middle_left);
                }
            } else if (!r.ok[i]) {
                // Named, not blank: a part that did not answer is a different
                // fact from one that answered zero. For the SoC, also how many
                // times it has been put back, since a reading from one die
                // instead of two is a different measurement.
                const uint32_t restarts = th.socRestarts();
                if (src == TempSource::Soc && restarts) {
                    c.textf(34, y, ui::theme::kWarn, &fonts::Font0, middle_left,
                            "no answer - restarted %u",
                            static_cast<unsigned>(restarts));
                } else {
                    c.textAt(34, y, "no answer", ui::theme::kFaint, &fonts::Font0,
                             middle_left);
                }
            } else {
                c.textf(34, y, ui::theme::kText, &fonts::Font0, middle_left,
                        "%.1f C", static_cast<double>(r.c[i]));
            }
            y += 11;
        }

        c.rule(y - 2, ui::theme::kLine, 4);

        // The one derived number left, and it is about the hardware.
        const bool learned = m.plateauLearned();
        c.textAt(6, y + 14, "SELF-HEAT", ui::theme::kDim, &fonts::Font0,
                 middle_left);
        if (m.known()) {
            c.textf(64, y + 14, learned ? ui::theme::kOk : ui::theme::kWarn,
                    &fonts::FreeSansBold12pt7b, middle_left, "%.1f C",
                    static_cast<double>(m.plateauC()));
        } else {
            c.textAt(64, y + 14, "--", ui::theme::kFaint,
                     &fonts::FreeSansBold12pt7b, middle_left);
        }

        // What it was worked out from. `climbed` is the running maximum that
        // bounds the model, so beside the plateau it shows whether this session
        // has earned the figure. Both are suppressed rather than shown as zero
        // before the first sample: zero is a plausible die temperature.
        if (m.known()) {
            c.textf(c.w() - 6, y + 8, ui::theme::kDim, &fonts::Font0, middle_right,
                    "die %.1f", static_cast<double>(m.dieC()));
            c.textf(c.w() - 6, y + 20, ui::theme::kDim, &fonts::Font0,
                    middle_right, "climbed %.1f",
                    static_cast<double>(m.observedClimbC()));
        }

        c.textAt(6, y + 32,
                 learned ? "measured on a cold start" : "assumed - no cold start yet",
                 learned ? ui::theme::kFaint : ui::theme::kWarn, &fonts::Font0,
                 middle_left);

        // The anchor is recorded every session; only an anchored one can claim
        // it was the room at the time, so the two are worded apart.
        if (!m.known()) {
            c.textAt(6, y + 44, "waiting for a sensor", ui::theme::kFaint,
                     &fonts::Font0, middle_left);
        } else if (m.anchored()) {
            c.textf(6, y + 44, ui::theme::kFaint, &fonts::Font0, middle_left,
                    "cold start, dies read %.1f C", static_cast<double>(m.anchorC()));
        } else {
            c.textAt(6, y + 44, "warm start - no anchor this session",
                     ui::theme::kFaint, &fonts::Font0, middle_left);
        }

        // Left standing for anyone who knew the old page and comes looking.
        c.textAt(6, y + 56, "a die is not the air", ui::theme::kFaint,
                 &fonts::Font0, middle_left);

        // A device that switches itself off has to say so somewhere.
        c.textf(c.w() - 6, y + 56, ui::theme::kDim, &fonts::Font0, middle_right,
                "off at %.0f C",
                static_cast<double>(ThermalService::overheatLimitC()));
    }

    // -- screen ---------------------------------------------------------------
    /// The panel's four numbers. The line under them says what they come to
    /// together, because the dim is a reduction and nobody should have to do the
    /// arithmetic to find out where it lands.
    void drawScreen(ui::Canvas& c) {
        constexpr int kPitch = 19;
        const int     top    = 24;
        auto&         set    = systemSettings();

        for (int i = 0; i < kScrRows; ++i) {
            const int   y   = top + i * kPitch;
            const bool  sel = (i == _screen.selected());
            const char* name = "";
            char        val[12]{};
            uint16_t    tint = ui::theme::kCyan;

            switch (i) {
                case kScrBright:
                    name = "brightness";
                    std::snprintf(val, sizeof(val), "%u%%",
                                  static_cast<unsigned>(set.brightnessPct()));
                    break;
                case kScrDim:
                    name = "dim after";
                    writeDuration(val, sizeof(val), set.dimAfterSec());
                    break;
                case kScrSleep:
                    name = "sleep after";
                    writeDuration(val, sizeof(val), set.sleepAfterSec());
                    // The one value here that costs the cell rather than saving
                    // it.
                    if (set.sleepAfterSec() == SystemSettings::kNeverSleep)
                        tint = ui::theme::kWarn;
                    break;
                default:
                    name = "dim by";
                    std::snprintf(val, sizeof(val), "%u%%",
                                  static_cast<unsigned>(set.dimByPct()));
                    break;
            }

            if (sel) {
                c.fillRoundRect(2, y - 9, c.w() - 4, 18, 3, ui::theme::kSurface);
            }
            c.textAt(10, y, name, sel ? ui::theme::kText : ui::theme::kDim,
                     &fonts::Font2, middle_left);
            c.textAt(c.w() - 8, y, val, sel ? tint : ui::theme::kFaint,
                     &fonts::Font2, middle_right);
        }

        // One line, because four rows leave one line. It states the outcome
        // rather than explaining a row: the rows say what they are.
        const unsigned dimPct =
            set.brightnessPct() * (100u - set.dimByPct()) / 100u;
        char line[64];
        if (set.sleepAfterSec() == SystemSettings::kNeverSleep) {
            std::snprintf(line, sizeof(line), "%u%%, then %u%% - never dark",
                          static_cast<unsigned>(set.brightnessPct()), dimPct);
        } else if (set.sleepAfterSec() <= set.dimAfterSec()) {
            // 37 characters is one line of Font0 across this panel, and four rows
            // leave exactly one.
            std::snprintf(line, sizeof(line), "%u%%, then dark - sleep is sooner",
                          static_cast<unsigned>(set.brightnessPct()));
        } else {
            std::snprintf(line, sizeof(line), "%u%%, then %u%%, then dark",
                          static_cast<unsigned>(set.brightnessPct()), dimPct);
        }
        ui::drawWrapped(c, 8, top + kScrRows * kPitch + 4, c.w() - 16,
                        c.h() - ui::theme::kFootH - 2, 10, line,
                        ui::theme::kFaint, &fonts::Font0);
    }

    // -- settings -------------------------------------------------------------
    /// Rows in the manner of CALIBRATION's speaker page, and a line under them
    /// saying what the selected one is: a setting that has to be guessed at
    /// gets left alone.
    void drawSettings(ui::Canvas& c) {
        constexpr int kPitch = 19;
        const int     top    = 24;
        const int     rows   = kFixedRows + _childCount;

        for (int i = 0; i < rows; ++i) {
            const int   y    = top + i * kPitch;
            const bool  sel  = (i == _rows.selected());
            const char* name = "";
            const char* val  = "";
            uint16_t    tint = ui::theme::kDim;

            if (i == kRowLibrary) {
                const bool on = systemSettings().libraryMode();
                name = "library mode";
                val  = on ? "ON" : "off";
                tint = on ? ui::theme::kWarn : ui::theme::kDim;
            } else {
                const AppInfo& a = childInfo(i - kFixedRows);
                name = a.title;
                val  = "open";
                tint = a.accent ? a.accent : ui::theme::kCyan;
            }

            if (sel) {
                c.fillRoundRect(2, y - 9, c.w() - 4, 18, 3, ui::theme::kSurface);
            }
            c.textAt(10, y, name, sel ? ui::theme::kText : ui::theme::kDim,
                     &fonts::Font2, middle_left);
            c.textAt(c.w() - 8, y, val, sel ? tint : ui::theme::kFaint,
                     &fonts::Font2, middle_right);
        }

        const char* hint =
            (_rows.selected() == kRowLibrary)
                // Four lines of the five this page holds. Nothing measures this
                // string the way help_fit.py measures a HELP page, so it is
                // kept a line short on purpose.
                ? "Every sound off, in every tool - beeps, alarms, the synth, "
                  "the test tone. The amplifier is never powered. Held across "
                  "a reboot."
                : childInfo(_rows.selected() - kFixedRows).subtitle;
        ui::drawWrapped(c, 8, top + rows * kPitch + 4, c.w() - 16,
                        c.h() - ui::theme::kFootH - 2, 10, hint,
                        ui::theme::kFaint, &fonts::Font0);
    }

    const char* hintA() const override {
        switch (_page) {
            case kVitals:   return "A beep";
            case kThermal:  return "hold A forget";
            case kScreen:   return "A next - hold change";
            case kSettings:
                return (_rows.selected() == kRowLibrary) ? "A next - hold toggle"
                                                         : "A next - hold open";
            default:        return nullptr;
        }
    }

    const char* hintB() const override {
        switch (_page) {
            case kVitals:   return "B thermal";
            case kThermal:  return "B screen";
            case kScreen:   return "B settings";
            default:        return "B vitals";
        }
    }

    /// The die temperature, visible from the other page too, and labelled: an
    /// unqualified temperature in a corner is how the retired air estimate got
    /// believed.
    const char* badge() const override {
        if (_page != kVitals) return nullptr;
        const ThermalModel& m = services().thermal().model();
        if (!m.known()) return nullptr;
        static char buf[16];
        std::snprintf(buf, sizeof(buf), "die %.0fC", static_cast<double>(m.dieC()));
        return buf;
    }
    uint16_t badgeColour() const override { return ui::theme::kCyan; }

private:
    /// M5GFX keeps its board-name table private, so this covers the boards this
    /// firmware cares about and reports the raw id for anything else, which is
    /// what you want to see when autodetection goes wrong.
    static const char* boardName() {
        static char other[24];
        switch (M5.getBoard()) {
            case m5::board_t::board_M5StickS3: return "M5StickS3";
            case m5::board_t::board_M5StickCPlus2: return "StickC Plus2";
            case m5::board_t::board_M5StackCoreS3: return "CoreS3";
            case m5::board_t::board_M5AtomS3: return "AtomS3";
            default:
                std::snprintf(other, sizeof(other), "unknown id %d",
                              static_cast<int>(M5.getBoard()));
                return other;
        }
    }

    static void row(ui::Canvas& c, int y, const char* label, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));

    /// How much of the NVS partition is spoken for, or why that is not known.
    static const char* nvsUse() {
        static char buf[24];
        nvs_stats_t st{};
        if (nvs_get_stats(nullptr, &st) != ESP_OK) return "nvs ?";
        const unsigned pct = st.total_entries
                                 ? (unsigned)(st.used_entries * 100 / st.total_entries)
                                 : 0u;
        std::snprintf(buf, sizeof(buf), "%u%% of %u", pct,
                      (unsigned)st.total_entries);
        return buf;
    }

    /// Tools that named this app as their parent, in registry order. The panel
    /// is the limit, not the array: five rows and their hint fill a 135 px
    /// page, and a sixth would want a scrolling list.
    static constexpr int kMaxChildren = 4;

    void buildChildren() {
        const auto& reg = AppRegistry::instance();
        _childCount     = 0;
        for (size_t i = 0; i < reg.count() && _childCount < kMaxChildren; ++i) {
            const char* p = reg.at(i).info->parent;
            if (p && std::strcmp(p, kInfo.id) == 0) {
                _child[_childCount++] = static_cast<int>(i);
            }
        }
    }

    const AppInfo& childInfo(int slot) const {
        return *AppRegistry::instance().at(static_cast<size_t>(_child[slot])).info;
    }

    ui::ListMenu _rows;
    ui::ListMenu _screen;
    int          _child[kMaxChildren]{};
    int          _childCount = 0;

    /// Both survive teardown, so returning from CALIBRATION lands where it was
    /// opened from.
    static Page _page;
    static int  _settingsRow;
    static int  _screenRow;
};

Page SystemInfoApp::_page        = kVitals;
int  SystemInfoApp::_screenRow   = 0;
int  SystemInfoApp::_settingsRow = kRowLibrary;

void SystemInfoApp::row(ui::Canvas& c, int y, const char* label, const char* fmt, ...) {
    char    buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    c.textAt(6, y, label, ui::theme::kDim, &fonts::Font0, middle_left);
    c.textAt(52, y, buf, ui::theme::kText, &fonts::Font0, middle_left);
}

}  // namespace

SD_REGISTER_APP(SystemInfoApp, kInfo)

}  // namespace sd
