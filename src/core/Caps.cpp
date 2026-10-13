#include "core/Caps.h"

namespace sd {

// -----------------------------------------------------------------------------
//  Rules that turn a requested mask into a coherent hardware state.
// -----------------------------------------------------------------------------
CapMask expandImplied(CapMask want, bool libraryMode) {
    // The PORT.A 5V pin sits on the EXT_5V rail.
    if (has(want, Cap::PortA)) want |= Cap::Ext5V;

    // The infrared LED and receiver sit on the EXT_5V rail, and the receiver
    // reads nothing while the speaker amplifier is powered.
    if (has(want, Cap::Ir)) {
        want |= Cap::Ext5V;
        want &= ~caps(Cap::Speaker);
    }

    // Mic and speaker cannot coexist here, and the failure is silent. They sit
    // on different I2S ports (mic on 1, speaker on 0) but share BCLK G17, WS
    // G15 and MCLK G18, so both as master would drive the same clock lines.
    // M5Unified's two ES8311 callbacks also disagree: the speaker writes clock
    // manager 0x01 = 0xB5 and the mic 0xBA, while the mic's disable path writes
    // 0x00 = 0x00, a full codec power-down. Listening wins, because an app that
    // asked to record and silently got nothing is the worse outcome.
    if (has(want, Cap::Mic)) want &= ~caps(Cap::Speaker);

    // Library Mode is a standing decision rather than a per-request one, so it
    // wins over whatever an app asks for, as the mic does above. Every request
    // comes through here, so no app can forget to check. Striking the bit rather
    // than letting acquire() fail quietly keeps _active honest: switching the
    // mode off is then a change of mask, so the next request really does start
    // the speaker instead of finding it already marked acquired.
    if (libraryMode) want &= ~caps(Cap::Speaker);

    // WiFi and BLE share one 2.4 GHz front end, and running both makes the
    // hardware time-slice, halving each one's update rate exactly when a hunt
    // needs it. WiFi wins, being the higher-rate mode.
    if (has(want, Cap::WiFi)) want &= ~caps(Cap::Ble);

    return want;
}

}  // namespace sd
