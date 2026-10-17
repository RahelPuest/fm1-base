# Porting a Felucca-based firmware onto fm1-base

Read fm1-base's `README.md`, `DESIGN.md`, `include/plat.h` and `template/` first: the template is the smallest
complete app, and the reference for everything below. CTL-1 (charlesvestal/fm1-ctl) and Radio
(charlesvestal/fm1-radio) are full ones.

## The new repository

```
my-firmware/
  base/            fm1-base, a git submodule (with felucca-bsp inside it as base/bsp)
  app/             the firmware: app.h, app.c and its other sources, fm1.toml, words.tsv, site/, tests/
  build.sh         runs base/tools/build.py app "$@"
  LICENSE          MPL-2.0
  LICENSING.md     where each part comes from (the table of the skill's step 1)
  README.md
```

```sh
git init my-firmware && cd my-firmware
git submodule add https://github.com/charlesvestal/fm1-base base
git submodule update --init --recursive
cp -r base/template app          # then rename and replace its contents with the firmware's
```

Start from the template's files, not from Felucca's: `app.h`, `fm1.toml` and the site page are fm1-base's (MPL-2.0).

## What fm1-base replaces

Felucca's tree, file by file (Felucca 1.x; a fork may have renamed some). None of these is copied into the new
project: fm1-base or felucca-bsp has its own.

| Felucca | In fm1-base / felucca-bsp |
| --- | --- |
| `firmware/hal/*` (chip, board, interrupts, timers, flash, USB registers, UART, guard, ADC, input, audio) | felucca-bsp `chip/`, `boards/fm1/` |
| `firmware/loader/` | felucca-bsp `loader/` |
| `firmware/src/main.c` (boot, the main loop, the audio interrupt, the watchdog, safe mode) | `device/main_fm1.c`, `device/unity.c` |
| `firmware/src/usb.c` | felucca-bsp `chip/usb.c` |
| `firmware/src/lcd.c`, `gfx.c` | `device/lcd.c`, `device/gfx.c` (the same drawing calls: `cv_begin`, `cv_rect`, `cv_line`, `cv_text`, `cv_blit`, `lcd_fill`), fonts from `tools/gen_font.py`; Felucca's icons (`icons.c`, the Fukiai font) are not in fm1-base |
| `firmware/src/panel.c`, `ui_input.c` (buttons, encoders, keys, LEDs) | `device/panel.c`, `plat_buttons`, `plat_keys`, `plat_enc`, `plat_lights` (`include/plat.h`) |
| `firmware/src/storage.c`, `upreset.c` | `device/storage.c`: `plat_store_load`, `plat_store_save`, the objects in `app.h`'s `APP_STORE_MAP` |
| `firmware/src/ota.c` | felucca-bsp `chip/ota.c` (updates over USB from the installer) |
| `firmware/src/midi_uart.c` (the TRS jack) | `device/midi_uart.c`: TRS in arrives through `plat_midi_in` as `PORT_TRS` |
| `firmware/src/libc.c` | felucca-bsp `chip/libc.c` |
| `firmware/src/console.c` | `tools/fm1_remote.py` (drive the FM-1 and read its screen and logs over USB) |
| `tools/build.py`, `fm1pkg_make.py`, `fm1_install.py`, `gen_font.py`, `lz4blk.py` | fm1-base `tools/` (one `build.py` for every firmware, driven by `fm1.toml`) |
| `web/index_pkg.html` (the web installer), `make_site.py` | `tools/make_site.py`, `tools/publish_site.sh` (installer, emulator, manual) |
| the browser emulator | `tools/build.py app --web` (the same app, in the browser) |

What is left of a Felucca fork after that is its app: its sound (engines, effects), sequencer, UI pages, project
format, MIDI handling. The parts the author wrote stay (they move into `app/`). The parts that came from Felucca are
the skill's "not kept" row: replaced or written again.

## The app contract

`app.h` (see the template's) defines the app's settings and declares what the platform calls:

| | |
| --- | --- |
| `APP_NAME`, `APP_SPLASH_RGB` | the splash |
| `APP_BLOCK` | frames per audio block (256: 5.8 ms at 44.1 kHz) |
| `APP_STORE_MAP`, the `OBJ_*` enum | the saved objects and their flash offsets |
| `app_init()` | at boot: the engine, then the saved settings |
| `app_render(int32_t *lr, uint32_t n, uint32_t master_q12)` | in the audio interrupt: n stereo frames, 24-bit; keep it short, it runs on an 8 KiB stack |
| `app_ui_init()`, `app_frame()`, `app_input_only()` | the first screen; the UI about 60 times a second; input between frames |
| `app_panic()` | silence now (an update is about to rewrite the flash) |
| `app_say(a, b)` | a short message on the screen and to a screen reader |

Optional, each turned on by a `#define` in `app.h`: `APP_TICK` (1 kHz, for a clock or a sequencer), `APP_LOOP`
(every pass of the main loop), `APP_SYSEX` (the app's own SysEx), `APP_TOUCH` (velocity and pressure from the
keys), `APP_BTDEV` (a page for Bluetooth MIDI devices), `APP_BOOT`, `APP_QUIET`, `APP_OVERLOAD`, `APP_SPLASH`. Grep
`device/` for each to see what it does.

`fm1.toml` picks the build: `name`, `id_prefix` (keep the firmware's if it has releases), the USB names, `o2` (the
DSP units built at -O2), `ble`, `ble_central`, `wifi`, `usb_audio_in` / `usb_audio_out`, `words`, and the site's
`tagline`, `repo`, `licence`, `credits`. Every key is documented at the top of `tools/build.py`.

## Porting the code

1. Move the app's files into `app/`. The build is a unity build: fm1-base's `device/unity.c` includes the app's
   `app.c`, and `app.c` includes the app's other `.c` files (as Radio's includes `radio.c` and `rg.c`). Heavy DSP
   goes in `fm1.toml`'s `o2` list instead, compiled apart at -O2.
2. Build (`./build.sh`). The compiler names what's missing: Felucca's own functions that fm1-base names
   differently, or that live in a file that was dropped. For each, find fm1-base's equivalent in `include/plat.h`
   or `device/`. The names of the drawing and panel calls are mostly Felucca's, as fm1-base's came from it.
3. Hardware is reached only through `plat.h` and felucca-bsp's headers. The build refuses register access in the
   app (`(volatile uint32_t *)`, inline assembly) outside the BSP.
4. No `double`: the FPU is single precision, and the build fails on the soft-double routines. No libm: use
   `fastmath.h`.
5. Run the host simulator (`./build.sh --host`, then the app's `tests/*.txt` scripts; the template's `tests/` shows
   the commands), then the browser build (`./build.sh --web`).
6. Install on an FM-1 (`python3 base/tools/fm1_install.py app/build/NAME.fwsc`) and check it: boot, screen, every
   button and knob, the keys, sound, MIDI in and out, a setting saved across a power cycle, an update from the
   firmware itself, Bluetooth or WiFi if it has them. `base/tools/fm1_remote.py` drives it over USB (`screen`,
   `log`, `tap`, `turn`, `key`).

Felucca's safety stays, from fm1-base: the boot guard (two failed starts: safe mode with USB on), the crash screen
and record, the watchdog, OCT- and OCT+ held 5 s for update mode, and the installer's way back to M-VAVE's firmware.

## Bluetooth and WiFi

These are the reason to move. Turn them on in `fm1.toml` (`ble = true`, or `wifi = true`; not both yet). fm1-base's
README has what each costs in flash and RAM, and the calls; CTL-1 shows Bluetooth MIDI with computers and devices,
Radio shows WiFi with lwIP's sockets and HTTPS. JieLi's libraries come from each builder's own AC79 SDK and are never
committed.

## The CPU

On the FM-1, a 32x32->64-bit multiply inlined among loads now and then leaves a register a bit off (felucca-bsp
`docs/CPU.md`). Fixed-point DSP is where it shows: fm1-base's build lists any function with a 64-bit multiply
inlined beside a load. Move those multiplies into `noinline` helpers (the product used at once), as Radio did for
its AAC decoder.
