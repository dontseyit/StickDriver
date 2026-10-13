#pragma once
// -----------------------------------------------------------------------------
//  ServiceHub - the single owner of shared hardware.
//
//  Apps declare the capabilities they need in their AppInfo; the AppManager
//  hands that mask here before onStart() and clears it after onStop(). All the
//  awkward, board-specific sequencing lives in applyCaps() and nowhere else:
//
//    * The microphone cannot record while the speaker amplifier is powered, so
//      requesting Mic forcibly stops the speaker.
//    * PORT.A needs the EXT_5V rail that M5Unified switches off at boot, since
//      its 5V pin is that rail: an unpowered module cannot answer a scan.
//    * The infrared receiver reads nothing while the speaker amplifier is
//      powered, and the infrared pair sits on EXT_5V, so requesting Ir drops
//      the speaker and raises the rail.
//    * WiFi and BLE share one 2.4 GHz front end, so asking for WiFi drops BLE.
//    * Library Mode, the device-wide silence switch on SYSTEM, strikes the
//      speaker from every request while it is on.
//
//  Encoding those rules once means a recording app cannot be broken by an audio
//  app, and neither has to know the other exists.
// -----------------------------------------------------------------------------
#include "core/Caps.h"
#include "services/AudioService.h"
#include "services/ImuService.h"
#include "services/MicService.h"
#include "services/MoodService.h"
#include "services/PortService.h"
#include "services/RadioService.h"
#include "services/ThermalService.h"
#include "services/InputService.h"
#include "services/PowerService.h"

namespace sd {

class ServiceHub {
public:
    static ServiceHub& instance();

    void begin();

    /// Bring the acquired set to exactly `want`.
    void applyCaps(CapMask want);
    CapMask activeCaps() const { return _active; }

    InputService& input() { return _input; }
    ImuService&   imu()   { return _imu; }
    PowerService& power() { return _power; }
    AudioService& audio() { return _audio; }
    MicService&   mic()   { return _mic; }
    MoodService&  mood()  { return _mood; }
    RadioService& radio() { return _radio; }
    PortService&  port()  { return _port; }
    ThermalService& thermal() { return _thermal; }

private:
    ServiceHub() = default;

    void acquire(Cap c);
    void release(Cap c);

    InputService _input;
    ImuService   _imu;
    PowerService _power;
    AudioService _audio;
    MicService   _mic;
    MoodService  _mood;
    RadioService _radio;
    PortService  _port;
    ThermalService _thermal;
    CapMask      _active = 0;
};

inline ServiceHub& services() { return ServiceHub::instance(); }

}  // namespace sd
