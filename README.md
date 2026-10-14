# StickDriver

StickDriver is a versatile firmware base for the **M5Stack StickS3** that provides a framework with hardware services already wired up and arbitrated. It brings up the display, IMU, battery, audio, microphone, expansion port, WiFi and Bluetooth, so you can develop apps without wiring the hardware yourself and use the provided service interfaces. It comes with a ready-to-use web console over the stick's own WiFi AP.

The app registry allows further app development using the framework. Settings for each app are stored in NVS, and the base builds the HELP pages from what each app declares.

Apps are still being added; today it ships with the app launcher; the console and the system app as well as a game called DIVERS.

## Hardware

| | |
|---|---|
| SoC | ESP32-S3-PICO-1-N8R8, dual-core LX7 @ 240 MHz |
| Memory | 8 MB flash, 8 MB octal PSRAM |
| Display | 1.14" ST7789P3, 135x240 |
| IMU | BMI270 @ I2C 0x68 |
| Power | M5PM1 @ I2C 0x6e, 250 mAh cell |
| Audio | ES8311 codec, AW8737 amp, MEMS mic |
| Buttons | KEY1 G11, KEY2 G12 |
| Expansion | Hat2 bus, HY2.0-4P Port A |

Pin map, the board's quirks, battery and sleep:
[docs/device.md](docs/device.md).

## Build

Install [PlatformIO](https://platformio.org/install/cli), then:

```bash
pio run -e sticks3            # build
pio run -e sticks3 -t upload  # flash
pio device monitor            # 115200, exception decoder on
pio test -e native            # the hardware-free maths, on your laptop
```

## CONSOLE

CONSOLE puts the device on WiFi and serves its own pages. The front page
reports the cell, the die temperatures and free memory, and lists whatever
other pages the apps you installed registered.

The access point is open and anything served is readable by anyone in range.
Turn it off when you are done.

## GAMES

**DIVERS** — a formation shooter in the style of Galaxian (1979). A formation of
aliens sits above you, and the ones that dive out of it are the ones that can
kill you. Tilt to move; the gun fires on its own. `A` re-centres the tilt, hold
`A` pauses. One bullet at a time, as in the arcade, so a miss costs you the shot
until it clears.

It runs portrait, as the cabinet did: the game is the column of empty panel
above the ship. Physics ticks at 60 Hz and the panel redraws at 30, because a
full frame is 65 kB down a 40 MHz bus and costs 13 ms whatever the CPU is doing.

GAMES is a folder: it lists whatever games are compiled in, and holding `B`
inside one comes back to the list rather than the launcher. A second game is one
file that names `games` as its parent, and nothing else changes.

## Adding an app

One file, and no other file changes. See [docs/writing-apps.md](docs/writing-apps.md)
for the whole of it; the shape is:

```cpp
class MyApp : public sd::App { /* onStart, onEvent, onTick, onDraw */ };

const sd::AppInfo kInfo{ .id = "mine", .title = "MINE", .needs = sd::caps(sd::Cap::Imu) };
SD_REGISTER_APP(MyApp, kInfo)
```

Drop the directory into `src/apps/`, build, and it is in the launcher.

How the framework fits together, and what each layer owes the others:
[docs/architecture.md](docs/architecture.md).

## Licence

The code is MIT. See [LICENSE](LICENSE).

The StickDriver name and the banner are not part of that licence. Fork the
firmware and build on it, and give what you ship a name of your own.
