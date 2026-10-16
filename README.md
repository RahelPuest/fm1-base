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
