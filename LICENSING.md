# Licensing

**fm1-base is MPL-2.0** (full text in `LICENSE`), file by file: every source file carries an
`SPDX-License-Identifier: MPL-2.0` header. If you change one of these files and distribute it, publish your change to
that file under MPL-2.0. Your own files, such as an app built on fm1-base, may use any licence, closed ones included.
So a firmware made of felucca-bsp, fm1-base and its own code may link JieLi's binary Bluetooth and Wi-Fi libraries.

## Where the code comes from

| Code | Origin | Why it is MPL-2.0 |
| --- | --- | --- |
| Leo Kuroshita's code (from Felucca, through the firmwares below) | [Felucca](https://github.com/hugelton/Felucca), GPL-3.0-only there | his permission to use the Felucca code he owns under MPL-2.0: [hugelton/felucca-bsp#4](https://github.com/hugelton/felucca-bsp/issues/4) |
| Charles Vestal's code | the platform layer of [fm1-x0x](https://github.com/charlesvestal/fm1-x0x), [fm1-fomni](https://github.com/charlesvestal/fm1-fomni) and the other FM-1 firmwares, GPL-3.0-only there | his own |
| `bsp/` | [felucca-bsp](https://github.com/hugelton/felucca-bsp), a git submodule | MPL-2.0 there |

The GPL copies in Felucca and in those firmwares stay under the GPL; the copies here are MPL-2.0. No code by other
Felucca contributors is in fm1-base: the history of every file was checked when it was imported (from fm1-fomni
`7e7547f`).

## Not part of fm1-base

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors) | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov) | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK files a package needs (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`) | Apache-2.0 | read from your SDK checkout at build time; never in this repository |
| The browser emulator's voice: eSpeak NG, through fm1-reader | GPL-3.0-or-later | taken from an fm1-reader checkout when the emulator is built; a separate program the page loads |
