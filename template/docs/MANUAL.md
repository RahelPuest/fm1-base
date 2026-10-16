# TEMPLATE

TEMPLATE is the smallest complete firmware for the M-VAVE FM-1, made to be copied. Every control shows on the
screen by its name as you use it, the keys play a simple voice, and a few controls do something: the octave, the
wave, the volume and saving. It installs, updates, runs in the browser and speaks to a screen reader, as a finished
firmware does.

This manual is also an example: copy it with the template and write your firmware's own.

![The TEMPLATE screen at rest: the name at the top, the 14 buttons in four rows, the seven knobs with their counts, the keys, then OCT +0, PRESET SINE, VOL 50%](img/screen-ready.png "TEMPLATE at rest")

---

## Contents

1. Install
2. The screen
3. Controls
4. Saving
5. MIDI
6. The screen reader
7. Troubleshooting

---

## 1. Install

**Try it first:** the emulator runs the same code in the browser, with sound, played with the mouse, a touch
screen or the computer keyboard.

**Install from the browser:** in Chrome or Edge, connect the FM-1 by USB, open the installer and press Install.
The browser asks once to allow MIDI: allow it. Don't unplug the cable while it writes.

**Install from a terminal:** `python3 tools/fm1_install.py template-VERSION.fwsc`.

**Back to the stock firmware:** the installer's last section writes M-VAVE's own firmware back. An FM-1 that
doesn't start can be recovered too. For details, see felucca-bsp's `tools/fm1_rescue.sh`.

---

## 2. The screen

![TEMPLATE while a key is held: C4 in the header, FX lit among the buttons, K3 at 3, the key lit on the keyboard](img/screen-playing.png "A key held, FX pressed, K3 turned")

| Part | What it shows |
| --- | --- |
| Header | The name, and the note while a key plays. A message for a moment after a change. |
| Buttons | All 14, by their names. A button lights while it's held. |
| Knobs | The seven encoders, each with a count of its turns. A knob lights for a moment as it turns. |
| Keys | The 27 keys, black above white. A key lights while it's held. |
| Bottom line | The octave, the wave (on PRESET) and the volume (MASTER). |

---

## 3. Controls

| Control | What it does |
| --- | --- |
| Keys | Play the voice, and send the note over USB-MIDI on channel 1. The last key pressed sounds. |
| OCT− · OCT+ | The octave, from −2 to +2. |
| PRESET | The wave: sine or triangle. |
| MASTER | The volume. |
| SAVE | Saves the octave and the wave. SAVE lights while there's something to save. |
| Every other button and knob | Shows its name on the screen, and nothing else. This is where a firmware wires its own. |

---

## 4. Saving

Press SAVE to keep the octave and the wave. They come back the next time the FM-1 starts. SAVE stays lit while
there are changes to save.

![After OCT+ and PRESET: OCT +1 and PRESET TRIANGLE, SAVE waiting](img/screen-settings.png "OCT+ and PRESET changed, not saved yet")

---

## 5. MIDI

MIDI comes in over USB and the TRS jack. A note on any channel plays the voice; a note off releases it. The keys
send notes out over USB.

---

## 6. The screen reader

TEMPLATE tells a screen reader what's on its screen, through the
[FM-1 Screen Reader](https://charlesvestal.github.io/fm1-reader/): the name and the settings when the reader
starts listening, then each change as it happens. The emulator does the same, with your own screen reader or
with its built-in voice.

---

## 7. Troubleshooting

**No sound:** turn MASTER up. The voice plays only while a key is held.

**The installer doesn't find the FM-1:** use Chrome or Edge, allow MIDI when asked, and close other programs
that use the FM-1's MIDI port.

**The FM-1 doesn't start after an install:** after two failed starts it waits in the chip's update mode. Put
the stock firmware back, then install again. For details, see felucca-bsp's `tools/fm1_rescue.sh`.
