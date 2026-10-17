---
name: felucca-to-mpl
description: Move an M-VAVE FM-1 firmware that is based on Felucca (GPL-3.0) onto felucca-bsp and fm1-base (MPL-2.0), so it can be released under MPL-2.0 and may link JieLi's Bluetooth or WiFi libraries. Use when asked to relicense a Felucca fork, to move a Felucca-based firmware to fm1-base or felucca-bsp, to "make it MPL", or to check whether a firmware still contains Felucca code.
---

# Move a Felucca-based firmware to MPL-2.0

Felucca (github.com/hugelton/Felucca) is GPL-3.0-only. A firmware made from it is GPL too, and a GPL firmware
cannot ship JieLi's closed Bluetooth or WiFi libraries. Two MPL-2.0 projects take its place:

- **felucca-bsp** (github.com/hugelton/felucca-bsp): the chip and board layer, the update loader, the package and
  install tools. Leo Kuroshita approved his Felucca code here under MPL-2.0 (felucca-bsp#4), and keremimo his part
  of `usb.c` (felucca-bsp#3).
- **fm1-base** (github.com/charlesvestal/fm1-base): everything above the hardware (screen and drawing, panel and
  keys, audio, MIDI over USB and TRS, storage, safe mode, the screen reader, Bluetooth, WiFi, USB audio), a host
  simulator and a browser emulator, the build, and a template app. It includes felucca-bsp as `bsp/`.

The firmware becomes a **new project**: fm1-base as a git submodule, and the firmware's own code as an app on it.
Nothing is carried over from Felucca except what felucca-bsp and fm1-base already contain. That is the whole rule;
[RULES.md](RULES.md) explains it, and what counts as the author's own code.

Worked examples: CTL-1 (github.com/charlesvestal/fm1-ctl, Bluetooth MIDI) and Radio
(github.com/charlesvestal/fm1-radio, WiFi). Their `LICENSING.md` files show the result.

## The steps

Work through them in order, and tell the user what each one found before going on. The licensing steps (1 and 2)
decide what the port (3) may contain, so don't start porting first.

### 1. Inventory: where each file came from

1. Get the project (the user's checkout) and a fresh Felucca:
   `git clone https://github.com/hugelton/Felucca` (not an old copy: Felucca's history was republished).
2. Get fm1-base with its submodule: `git clone --recursive https://github.com/charlesvestal/fm1-base`.
3. Run the provenance check from fm1-base:

   ```sh
   python3 fm1-base/tools/provenance.py PROJECT Felucca
   ```

   It lists each file of PROJECT that has runs of Felucca's lines that are not in felucca-bsp or fm1-base, and
   whose they are (Leo's, or a contributor's pull request: Leo applied many under his own name, so `git blame`
   alone would hide them). `--lines` shows the lines. It needs `gh` (GitHub's CLI) to fetch the credited pull
   requests once.
4. Sort every file of PROJECT into one of these (write the table down; it becomes `LICENSING.md` later):

   | Kind | What happens to it |
   | --- | --- |
   | Felucca's platform (`firmware/hal/`, and in `firmware/src/` the boot, main loop, audio interrupt, USB, LCD, panel and input, storage, update, libc, gfx; the loader; the build, package and install tools; the web installer) | dropped: fm1-base and felucca-bsp provide it ([PORTING.md](PORTING.md) has the map) |
   | The author's own code, never in Felucca (the tool finds none of it, and the project's history shows the author wrote it) | kept, relicensed to MPL-2.0 by its author |
   | Felucca's other code (its engines, sequencer, UI, MIDI handling, editor): anything the tool reports | not kept: rewritten from scratch by the author, or replaced (fm1-base's equivalent, or an upstream library) |
   | Code by other people (the project's own contributors) | kept only with each one's agreement to MPL-2.0, on the record; otherwise rewritten |
   | Third-party code (DSP libraries, decoders, fonts, samples) | kept under its own licence if that licence allows the combination (MIT, BSD, Apache-2.0, CC0, OFL: yes; GPL: no); taken from its upstream, not from Felucca's copy |

5. Show the user the table, and the tool's report. Ask about anything unclear (who wrote a file, where a library
   came from). Don't guess authorship.

### 2. Decide what is rewritten

For every file in the "not kept" rows: is the feature needed? Is there an equivalent in fm1-base (see
[PORTING.md](PORTING.md))? If it has to be written again, it is written by the author from the behaviour, not from
Felucca's code. Don't paraphrase Felucca's source line by line: a rewrite that tracks the original is still a copy.
Agree the list with the user before porting.

### 3. Port: the app on fm1-base

Make the new repository (a new one, not a fork of Felucca's), with fm1-base as `base/` and the app in `app/`, the
way CTL-1 and Radio are laid out. Then follow [PORTING.md](PORTING.md): the app contract (`app.h`), `fm1.toml`, the
platform interface (`include/plat.h`), the build, the simulator, and the device. Keep the firmware's package identity
(`id_prefix`) if it already has releases, so the new firmware installs over the old one as an update.

### 4. Check it

- `python3 base/tools/provenance.py . Felucca` reports nothing but a few single lines, and those are checked by
  hand. A clean report is necessary, not sufficient: it finds copies, not rewrites that follow the original too
  closely. Review rewritten files with that in mind.
- The build is clean (`./build.sh`), the tests pass, the host simulator runs the app, and it runs on an FM-1:
  boot, screen, keys, sound, MIDI, saving, an update over USB, and Bluetooth or WiFi if it has them.
- fm1-base's build prints a note if any function has a 64-bit multiply inlined beside a load. On the FM-1 such code
  now and then corrupts a register (felucca-bsp `docs/CPU.md`). Move those multiplies into `noinline` helpers.

### 5. Licensing files and release

- `LICENSE`: the MPL-2.0 text. An SPDX line (`SPDX-License-Identifier: MPL-2.0`) and the author's copyright at the
  top of every file of the app.
- `LICENSING.md`: the table from step 1, as it is now: what each part is, where it came from, its licence (see
  CTL-1's and Radio's for the form). Third-party code with its licence file beside it.
- `README.md` credits: fm1-base and felucca-bsp, "from Felucca by Leo Kuroshita (Hügelton Instruments)"; the
  third-party code. No Felucca, M-VAVE or Hügelton names in the product's name.
- Release: `./build.sh --release X.Y`, then `base/tools/publish_site.sh app X.Y` for a site (installer, emulator),
  and a GitHub release with the `.fwsc` and notes.
- Ask the maintainers of felucca-bsp to list the firmware (felucca-bsp `LISTING.md`) if the user wants that.

## What not to do

- Don't copy a file from Felucca because "it's Leo's": the rule is felucca-bsp and fm1-base only.
- Don't carry over a Felucca file by renaming it or reformatting it. The check ignores spacing, and a copy is a copy.
- Don't relicense a contributor's code without their recorded agreement.
- Don't say the firmware is MPL-2.0 until step 4 is clean and the user has seen the table.
