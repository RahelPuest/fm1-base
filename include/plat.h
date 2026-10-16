/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* fm1-base's platform interface (from X0X's, by way of FoMni-1): everything the app needs from the hardware.
 *
 * The device implementation (plat_fm1.c) wraps Felucca's HAL and panel map; the
 * host implementation (host/plat_host.c) is a simulator fed by a script, which is
 * what lets the whole app — UI, sequencer, engines — run and be tested on a Mac.
 * Drawing goes through Felucca's lcd_fill / lcd_blit (lcd.c on the device, a
 * framebuffer on the host) and gfx.c on top. */
#pragma once
#include <stdint.h>

/* big zero-initialised buffers go in the .pool RAM region on the device */
#ifdef PLAT_HOST
#define PLAT_POOL
#else
#define PLAT_POOL __attribute__((section(".pool")))
#endif

/* panel controls, by printed label (Felucca's panel.c map turns these into matrix ids; Felucca
 * calls the second button SCL, the panel prints SEL) */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

#define NKEYS 27                       /* F3 .. G5 */
/* key k by colour: the 16 white keys and the 11 black keys, left to right (initialisers for a uint8_t array) */
#define PLAT_WHITE_KEYS {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26}
#define PLAT_BLACK_KEYS {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25}

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

uint32_t plat_ms(void);
uint32_t plat_buttons(void);           /* held: bit B_* */
uint32_t plat_keys(void);              /* held: bit k = key k (0 = F3) */
int32_t plat_enc(int role);            /* detents since the last call, + = clockwise; EN_* */
uint32_t plat_master(void);            /* MASTER pot's position, 0..4096 (the render gets it as a gain: APP_GAIN) */

/* lights: bright buttons and keys, and dim ones (a short pulse each scan frame; a bright bit wins); glow 0 the dim
 * ones off, 1 low, 2 high. Bit NB in buttons / dim_buttons is PLAY's second, green LED */
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow);
#define LED_PLAY_GREEN (1u << NB)
/* the same, simpler: the lit buttons and keys, the unlit buttons glowing if plat_glow(1); PLAY lights green, red
 * while plat_play_red(1) */
void plat_leds(uint32_t buttons, uint32_t keys);
void plat_glow(int on);
void plat_play_red(int red);

/* MIDI: 4-byte USB-MIDI event packets (byte 0 = cable / CIN, then status, data 1, data 2). In from USB, the TRS jack
 * (in only: the FM-1 has no MIDI out jack) and Bluetooth (FM1B_BLE); out to USB and Bluetooth. */
enum { PORT_USB, PORT_BLE, PORT_TRS, NPORTS };
#define TO_USB (1u << PORT_USB)
#define TO_BLE (1u << PORT_BLE)
int plat_midi_in(uint32_t *pkt, int *port);   /* 1 = got one, from port PORT_* (port may be 0) */
void plat_midi_send(uint32_t pkt, uint32_t to);   /* to: TO_USB | TO_BLE; main loop, the render or the tick */
void plat_midi_out(uint32_t pkt);      /* plat_midi_send(pkt, TO_USB) */

/* Bluetooth MIDI (fm1.toml ble = true: FM1B_BLE 1): JieLi's BT libraries on fm1-base's shim (ble/), started at
 * every boot unless the last Bluetooth run crashed (then by hand: plat_ble_start). The app gives it a store object,
 * OBJ_BLE (the radio's address and pairing). Without it (FM1B_BLE 0, tools/build.py --no-ble) nothing of JieLi's is
 * linked, and the state stays BLE_OFF */
#ifndef FM1B_BLE
#define FM1B_BLE 0
#endif
enum { BLE_OFF, BLE_STARTING, BLE_READY, BLE_FAILED };
int plat_ble_state(void);
int plat_ble_connected(void);          /* a central is connected and listening */
void plat_ble_start(void);
uint32_t plat_ble_log_total(void);     /* lines written since the last start (survives a reset) */
const char *plat_ble_log_line(int back);   /* 0 = the newest */
/* Bluetooth MIDI devices (fm1.toml ble_central = true: FM1B_BLE_CENTRAL 1): the FM-1 connects to one (a keyboard,
 * a controller), beside a computer connected to it. Its MIDI comes in as PORT_BLE too, and TO_BLE goes to both.
 * Scanning finds the devices that advertise the MIDI service (up to 8); connecting stops the scan. */
#ifndef FM1B_BLE_CENTRAL
#define FM1B_BLE_CENTRAL 0
#endif
enum { BLE_DEV_OFF, BLE_DEV_SCANNING, BLE_DEV_CONNECTING, BLE_DEV_SEARCHING, BLE_DEV_READY };
void plat_ble_scan(int on);
int plat_ble_found(int i, char *name, int *rssi);   /* device i of the scan (name: 21 bytes); 0 = none */
int plat_ble_connect(int i);           /* 0 = connecting */
void plat_ble_disconnect(void);
int plat_ble_device(void);             /* BLE_DEV_*: READY is connected, MIDI both ways */
/* Bluetooth on or off, as the player asks: off ends every link, stops advertising and scanning (the radio quiet);
 * on starts it (or advertises again). Whether it starts at power-on: APP_BLE_AT_BOOT (app.h, e.g. a saved setting;
 * default 1) */
void plat_ble_enable(int on);

/* the 1 kHz tick (app_tick, an app with APP_TICK) runs in the timer interrupt: the main loop locks it out around
 * what they share (it nests) */
uint32_t plat_lock(void);
void plat_unlock(uint32_t f);
void plat_yield(void);                 /* long work in the main loop (drawing) lets the app's background run (app_yield) */

/* the keys' velocity and pressure (an app with APP_KEY_SENSE): on, the scan reads the keys on the analog rows and
 * hands each reading to the app (app_key_sample, app_key_probe); off by default */
void plat_sense(int on);
uint32_t plat_analog_mask(void);       /* bit k: key k is on an analog row (PA7 / PA8; the top row is probed) */

/* persistent storage (Felucca storage.c objects: A/B sectors, CRC, torn-write safe) */
int plat_store_load(uint32_t obj, void *dst, uint32_t max);   /* bytes loaded, < 0 = none / bad */
int plat_store_save(uint32_t obj, const void *src, uint32_t len);   /* 0 = ok */
#define PLAT_STORE_MAX 3840u           /* bytes per object */
/* The objects are the app's (app.h): an enum that includes OBJ_PANEL (the panel calibration, the platform's) and
 * ends with OBJ_NOBJ, and APP_STORE_MAP, each object's flash offset: two 4 KiB sectors (A/B) from there, inside the
 * data area (0x97000..0xDFFFF) or Felucca's settings sector pair at 0xFC000. An object's number is written in its
 * record, so an app keeps its numbering from one release to the next. */

/* audio load in % of the render budget, and xruns since boot (device: from the ISR) */
uint32_t plat_cpu_pct(void);
uint32_t plat_xruns(void);

/* the app's optional hooks (app.h defines the macro when it has the function):
 *   APP_TICK        void app_tick(void)           1 kHz, in the timer interrupt (a clock, a sequencer)
 *   APP_BOOT        void app_boot(void)           once, after the platform is up, before app_ui_init (a radio)
 *   APP_LOOP        void app_loop(void)           every pass of the main loop (a radio's tasks)
 *   APP_YIELD       void app_yield(void)          from plat_yield, inside long work
 *   APP_OVERLOAD    void app_overload(void)       the render took over 90 % of its time (shed voices)
 *   APP_SPLASH      void app_splash(void)         the first screen at boot, instead of the name
 *   APP_GAIN(m)     the render's gain from MASTER's position m (0..4096; default the square, m * m >> 12)
 *   APP_QUIET       void app_quiet(int32_t *lr, uint32_t n)   the render while the flash must not be read (the
 *                   radio's first second, FM1B_BLE): keep time without sound (default: silence)
 *   APP_KEY_SENSE   void app_key_sample(uint32_t key, uint32_t v, uint32_t ticks, int driven) (rows PA7 / PA8),
 *                   int app_key_drive(uint32_t key) (read it driven), void app_key_probe(uint32_t key, uint32_t v,
 *                   uint32_t pressed) (the top row), int app_key_led_ok(uint32_t key) (0: its column dark when read);
 *                   from the scan's interrupt */
