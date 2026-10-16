/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The template app: the keys play a voice and go out over USB-MIDI, MIDI in plays it too, K1 sets the octave,
 * K2 the wave, MASTER the volume, SAVE stores the settings. One of each thing the platform offers, kept short.
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include "fastmath.h"

#define SET_MAGIC 0x54504C31u            /* "TPL1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t wave;                        /* 0 sine, 1 triangle */
    uint8_t rsv[2];
} settings_t;
static settings_t set;
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

static const char *const WAVE_NAME[] = {"SINE", "TRIANGLE"};

/* ------------------------------------------------------------------ voice --- */
/* one voice, the last note wins; the main loop and MIDI in set it, the audio interrupt plays it */
static volatile int32_t voice_note = -1;     /* MIDI note, -1 = released */
static float voice_phase, voice_level;

static void note_on(int32_t note) { voice_note = note; }
static void note_off(int32_t note)
{
    if (voice_note == note)
        voice_note = -1;
}

static void midi_in(void)                    /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        if ((st & 0xF0u) == 0x90u && d2)
            note_on((int32_t)d1);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            note_off((int32_t)d1);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    int32_t note = voice_note;
    float target = note >= 0 ? 0.25f : 0.0f;
    float inc = note >= 0 ? 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / 44100.0f : 0.0f;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    midi_in();
    for (i = 0; i < n; i++) {
        float s;
        voice_level += (target - voice_level) * 0.002f;          /* ~10 ms attack and release */
        voice_phase += inc;
        if (voice_phase >= 1.0f)
            voice_phase -= 1.0f;
        s = set.wave ? 4.0f * fm_fabsf(voice_phase - 0.5f) - 1.0f : fm_sinf(voice_phase * 6.2831853f);
        s *= voice_level * gain;
        lr[2 * i] = (int32_t)s;
        lr[2 * i + 1] = (int32_t)s;
    }
}

void app_panic(void)
{
    voice_note = -1;
    voice_level = 0.0f;
}

/* --------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.wave = 0;
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.wave > 1u)
        settings_defaults();
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];                     /* a short message over the header */
    uint32_t msg_until;
    uint32_t keys;                       /* keys held at the last input */
    uint8_t redraw;
} ui;

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48], *p = t;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        for (i = 0; ui.msg[0][i]; i++)
            *p++ = ui.msg[0][i];
        if (ui.msg[1][0])
            *p++ = ' ';
        for (i = 0; ui.msg[1][i]; i++)
            *p++ = ui.msg[1][i];
        *p = 0;
        ann_say(t, 0, ANN_NOW);
    }
}

static void note_name(int32_t note, char *b)
{
    static const char *const N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char *s = N[note % 12];
    int32_t oct = note / 12 - 1;
    while (*s)
        *b++ = *s++;
    if (oct < 0) {
        *b++ = '-';
        oct = -oct;
    }
    *b++ = (char)('0' + oct);
    *b = 0;
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    static uint32_t btn_was;
    int32_t e;
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k;
        int32_t note = 53 + (int32_t)k + 12 * set.octave;
        if ((keys & bit) && !(ui.keys & bit)) {
            note_on(note);
            plat_midi_out(0x09u | 0x90u << 8 | (uint32_t)note << 16 | 100u << 24);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            note_off(note);
            plat_midi_out(0x08u | 0x80u << 8 | (uint32_t)note << 16);
        }
    }
    if (keys != ui.keys)
        ui.redraw = 1;
    ui.keys = keys;
    if ((e = plat_enc(EN_K1)) != 0) {
        int32_t o = clamp(set.octave + (e > 0 ? 1 : -1), -2, 2);
        if (o != set.octave) {
            char t[4] = {o < 0 ? '-' : '+', (char)('0' + (o < 0 ? -o : o)), 0, 0};
            set.octave = (int8_t)o;
            set_dirty = 1;
            app_say("OCTAVE", t);
        }
    }
    if ((e = plat_enc(EN_K2)) != 0) {
        set.wave ^= 1u;
        set_dirty = 1;
        app_say("WAVE", WAVE_NAME[set.wave]);
    }
    if ((btn & (1u << B_SAVE)) && !(btn_was & (1u << B_SAVE))) {
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
            set_dirty = 0;
            app_say("SAVED", "");
        } else {
            app_say("SAVE", "FAILED");
        }
    }
    btn_was = btn;
    plat_glow(1);                            /* the unlit buttons glow, readable in the dark */
    plat_leds(set_dirty ? 1u << B_SAVE : 0u, keys);
}

void app_input_only(void) { input(); }

static void draw(void)
{
    char t[24];
    int32_t note = voice_note;
    uint32_t pct = plat_master() * 100u / 4096u;
    /* the header: the name, or a message for a moment */
    cv_begin(240, 32, C_BLACK);
    if (plat_ms() < ui.msg_until) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
    }
    cv_blit(0, 0);
    /* the note, large, and the settings */
    cv_begin(240, 144, C_BLACK);
    if (note >= 0) {
        note_name(note, t);
        cv_text((240 - text_w(&FONT_L, t)) / 2, 20, &FONT_L, t, C_HI);
    } else {
        cv_text((240 - text_w(&FONT_S, "PLAY A KEY")) / 2, 32, &FONT_S, "PLAY A KEY", C_DIM);
    }
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
    cv_text(10, 80, &FONT_S, "K1 OCTAVE", C_GRAY);
    cv_text(150, 80, &FONT_S, t, C_HI);
    cv_text(10, 100, &FONT_S, "K2 WAVE", C_GRAY);
    cv_text(150, 100, &FONT_S, WAVE_NAME[set.wave], C_HI);
    cv_text(10, 120, &FONT_S, "MASTER", C_GRAY);
    t[0] = (char)('0' + pct / 100u % 10u);
    t[1] = (char)('0' + pct / 10u % 10u);
    t[2] = (char)('0' + pct % 10u);
    t[3] = '%';
    t[4] = 0;
    cv_text(150, 120, &FONT_S, pct >= 100u ? t : t + (pct >= 10u ? 1 : 2), C_HI);
    cv_blit(0, 48);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
    app_say(APP_NAME, "READY");
}

void app_frame(void)
{
    static uint32_t last_pct;
    uint32_t pct = plat_master() * 100u / 4096u;
    input();
    if (pct != last_pct || (ui.msg_until && plat_ms() >= ui.msg_until)) {
        last_pct = pct;
        if (plat_ms() >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}
