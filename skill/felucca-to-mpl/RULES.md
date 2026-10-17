# What a moved firmware may contain

## The records

- Felucca is GPL-3.0-only, and stays so: github.com/hugelton/Felucca.
- Leo Kuroshita approved his Felucca code in felucca-bsp and fm1-base under MPL-2.0, with his name in the credits:
  https://github.com/hugelton/felucca-bsp/issues/4
- keremimo agreed for his part of `usb.c` (MIDI clock and transport sync, the TRS ring fix):
  https://github.com/hugelton/felucca-bsp/pull/3
- felucca-bsp's `LICENSING.md`, "Forks of Felucca": a fork does not become MPL-2.0 by using felucca-bsp; start a new
  project and import it.

## The rule

A firmware moved to MPL-2.0 is made of:

1. **felucca-bsp and fm1-base**, as a submodule, unchanged or changed under MPL-2.0 (changes to their files are
   published under MPL-2.0, file by file).
2. **The author's own code**: written by the person relicensing it, or by people who agreed, on the record, to
   MPL-2.0 for their part.
3. **Third-party code** under a licence that allows it beside MPL-2.0 files and in a firmware that links JieLi's
   libraries: MIT, BSD, ISC, zlib, Apache-2.0, CC0, public domain, SIL OFL (fonts). Not GPL or LGPL code, which
   would make the firmware GPL again. Each with its licence text and listed in `LICENSING.md`.

Nothing else from Felucca: not its engines, sequencer, UI, MIDI handling, editor or web pages, Leo's included. What
the firmware needs of them is replaced by fm1-base's, taken from the upstream library they came from (Felucca's
third-party parts are listed in its `LICENSING.md` and `CREDITS.md`: take those from their own repositories, under
their own licences), or written again.

## The author's own code

Code is the author's when they wrote it, in this project, without copying it from Felucca. Check, don't assume:

- The provenance tool (`tools/provenance.py`) finds lines copied from Felucca, including those that came in through
  contributors' pull requests that Leo applied under his own name. Run it on the whole project.
- A file that started as a copy of a Felucca file and was then changed is still Felucca's, however much it changed,
  unless it was rewritten from scratch.
- In the project's own history (`git log --follow FILE`), commits by other people are theirs: they need to agree
  to MPL-2.0 for their part, on the record (an issue or pull request comment, as felucca-bsp#3), or their part is
  rewritten.
- Some FM-1 firmwares took code from other GPL forks (X0X, Melodee, Jangada and others). That code is GPL too,
  unless its author relicenses it.

## Rewriting

When a feature has to be written again, write it from what it does (its behaviour on the device, its manual), not
from Felucca's source. Don't keep Felucca's file open beside the new one and translate it. A rewrite that follows
the original's structure line for line is a derived work. If the user can't rewrite something cleanly, it stays out.

## Why it matters

JieLi's Bluetooth and WiFi libraries are binaries under Apache-2.0. A GPL-3.0 firmware that links them can't be
distributed without an extra permission from the GPL code's copyright holders. An MPL-2.0 firmware can: MPL-2.0
applies file by file, and its files' sources are all published.
