# fm1-base

A base for M-VAVE FM-1 firmware, on [felucca-bsp](https://github.com/hugelton/felucca-bsp): the platform layer
above the hardware (screen, drawing and fonts, panel, LEDs, audio, MIDI over USB and TRS, storage, the screen reader),
a host simulator and a browser emulator that run the same app, and the tools to build, package and install it.
MPL-2.0: an app built on it may use any licence. See [DESIGN.md](DESIGN.md), [LICENSING.md](LICENSING.md) and
[CREDITS.md](CREDITS.md).

Status: assembled from the platform layer of the FM-1 firmwares (FoMni-1, X0X, CTL-1 and others). The template
(`tools/build.py template`) runs on an FM-1 (2026-10-16): screen, keys and their LEDs, the glow, sound, MASTER,
encoders, settings saved across a power cycle, USB-MIDI both ways, the screen reader, the USB names, and an update
started from the template itself (felucca-bsp's loader). The host simulator runs any app
(`tools/build.py template --host`, then `template/build/host/template_host template/tests/basic.txt`), and in the browser (`tools/build.py template --web`:
the simulator alone, panel, screen, sound and screen reader, for a firmware's site to link). Not yet moved over: the
optional interface groups.

    git clone --recursive https://github.com/charlesvestal/fm1-base

## Optional parts

A firmware has only what its fm1.toml asks for (and its app.h, for velocity and pressure); every build prints the
parts it has.

| Part | How to include it | What the app calls |
| --- | --- | --- |
| Bluetooth MIDI with a computer or tablet | `ble = true` (`tools/build.py --no-ble` leaves it out of one build) | `TO_BLE`, `PORT_BLE`; `plat_ble_state`, `plat_ble_connected`; `plat_ble_enable` |
| Bluetooth MIDI devices (the FM-1 connects to a keyboard or controller) | `ble_central = true`, with `ble` | `plat_ble_scan`, `plat_ble_found`, `plat_ble_connect`, `plat_ble_disconnect`, `plat_ble_device` |
| USB audio: the FM-1 recorded over the cable | on unless `usb_audio_in = false` | nothing: the master output |
| USB audio: a computer plays through the FM-1 | on unless `usb_audio_out = false` (needs the input too) | nothing: mixed after the app, at MASTER |
| Velocity and pressure from the keys | `#define APP_TOUCH` in app.h | `touch_on`, `touch_poll`, `touch_keys`, `touch_vel`, `touch_pressure` (lib/touch.h) |

Bluetooth starts at every power-on, as on M-VAVE's firmware, unless the last Bluetooth run crashed. A firmware
that lets the player turn it off keeps that as a setting, calls `plat_ble_enable(0)` / `plat_ble_enable(1)`
when it changes, and tells the platform whether to start it at power-on with `APP_BLE_AT_BOOT` in app.h (an
expression, e.g. `(set.bluetooth != 0)`). Off ends every link and stops advertising and scanning: the radio is quiet.
Its manual should say where the setting is (the template's: for details, see template/docs/MANUAL.md).

## Development over USB

`tools/fm1_remote.py` drives an FM-1 running any fm1-base firmware as a player would: buttons, keys, encoders,
MASTER, and reads back what the screen says (the screen reader's channel), the last crash and Bluetooth's log.
For example, `tools/fm1_remote.py listen tap REC wait 3000 screen log`.
