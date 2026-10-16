/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* fm1-base on the FM-1: plat.h over felucca-bsp's chip and board layers, the panel map, the USB rings and storage.
 * Part of the unity build (unity.c), after usb.c, storage.c and panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

uint32_t plat_buttons(void)
{
    uint32_t raw = fm1_in.buttons, out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out;
}

uint32_t plat_keys(void) { return fm1_in.notes & ((1u << NKEYS) - 1u); }

int32_t plat_enc(int role)
{
    if (role < 0 || role >= NE)
        return 0;
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

static volatile uint32_t master_pos = 2048;   /* MASTER's position, 0..4096 (main_fm1.c reads the pot) */
uint32_t plat_master(void) { return master_pos; }

/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
/* the dim ones: felucca-bsp's glow, a short pulse on every scan frame, in two strengths (fm1_led_dim_level) */
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow)
{
    static int level = -1;
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t i, c;
    if (glow > 0 && glow != level) {
        fm1_led_dim_level(glow == 1 ? 1u : 0u);     /* 1: DIM LO, 2: DIM HI */
        level = glow;
    }
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        int dim = glow > 0 && (i < NB ? (dim_buttons >> i) & 1u : (dim_keys >> (i - NB)) & 1u);
        uint8_t q = led_pos[id];
        if (q == 0xFF)
            continue;
        if (on)
            nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
        else if (dim)
            dl[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    /* PLAY's second, green LED: the keyless matrix position column 8, row PA5 (found on the device) */
    if (buttons & LED_PLAY_GREEN)
        nl[8] |= 1u << 1;
    else if (glow > 0 && (dim_buttons & LED_PLAY_GREEN))
        dl[8] |= 1u << 1;
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
    }
}
static uint8_t glow_on = 1, play_red;
void plat_glow(int on) { glow_on = (uint8_t)(on != 0); }
void plat_play_red(int red) { play_red = (uint8_t)(red != 0); }
void plat_leds(uint32_t buttons, uint32_t keys)    /* PLAY green, unless red (recording); the unlit buttons glow */
{
    uint32_t play = 1u << B_PLAY, b = buttons, dim = glow_on ? ((1u << NB) - 1u) & ~buttons : 0u;
    if (!play_red) {
        b = (b & ~play) | (buttons & play ? LED_PLAY_GREEN : 0u);
        dim = (dim & ~play) | (dim & play ? LED_PLAY_GREEN : 0u);
    }
    plat_lights(b, keys, dim, 0, glow_on ? 2 : 0);
}

/* the 1 kHz tick (app_tick) runs in the timer interrupt: the main loop locks it out around what they share. The lock
 * nests; MIDI sent from the tick itself needs none (nothing that sends interrupts it) */
static volatile uint8_t in_tick;
static uint32_t lock_depth;
uint32_t plat_lock(void)
{
    fm1_irq_off();
    return lock_depth++;
}
void plat_unlock(uint32_t f)
{
    (void)f;
    if (lock_depth && --lock_depth == 0)
        fm1_irq_on();
}
void plat_yield(void)
{
#ifdef APP_YIELD
    app_yield();
#endif
}

/* MIDI: the rings carry 4-byte USB-MIDI event packets, byte 0 = cable / CIN. USB and the TRS jack share midi_in_q
 * (both from TIMER5); the jack's packets carry cable 1 (midi_uart.c), which the single-jack USB descriptor never
 * does. Bluetooth is the app's (APP_BLE_MIDI). */
int plat_midi_in(uint32_t *pkt, int *port)
{
    int from;
    if (mi_r != mi_w) {
        uint32_t p = midi_in_q[mi_r % MQ];
        RING_PUBLISH();
        mi_r++;
        from = (p >> 4 & 0xFu) == 1u ? PORT_TRS : PORT_USB;
        *pkt = p & ~0xF0u;
    }
#ifdef APP_BLE_MIDI
    else if (app_ble_midi_in(pkt))
        from = PORT_BLE;
#endif
    else
        return 0;
    if (port)
        *port = from;
    return 1;
}
void plat_midi_send(uint32_t pkt, uint32_t to)    /* the main loop's and the tick's rings are shared */
{
    uint32_t f = 0;
    if (!in_tick)
        f = plat_lock();
    if (to & TO_USB)
        midi_out_event(pkt);
#ifdef APP_BLE_MIDI
    if (to & TO_BLE)
        app_ble_midi_out(pkt);
#endif
    if (!in_tick)
        plat_unlock(f);
}
void plat_midi_out(uint32_t pkt) { plat_midi_send(pkt, TO_USB); }

/* the keys' velocity and pressure (APP_KEY_SENSE): felucca-bsp's analog rows, as fm1-ctl and fm1-monk read them (the
 * converter's clock, a held key driven, the top row probed). Off, the converter is the main loop's (MASTER). */
void plat_sense(int on)
{
    if (!on) {
        fm1_an.con = 0;
        return;
    }
    if (!fm1_an.con) {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);  /* the pot's slot comes round in a frame: no dip meanwhile */
        if (a >= 0)
            fm1_an.master = (uint16_t)a;
    }
    fm1_an.drive = 1;
    fm1_an.probe = 1;
    fm1_an.con = (uint16_t)FM1_ADC_CON(7, 2);
}
uint32_t plat_analog_mask(void)
{
    uint32_t m = 0, c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 3; r <= 4u; r++)
            if (FM1_KEYMAP[r][c] >= 14)
                m |= 1u << (FM1_KEYMAP[r][c] - 14);
    return m;
}

int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }

static volatile uint32_t audio_cpu_pct, audio_xruns;
uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }
uint32_t plat_xruns(void) { return audio_xruns; }
