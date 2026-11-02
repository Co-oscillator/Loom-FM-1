/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Physical panel: which matrix button / encoder carries which printed label.
 * The matrix ids are known, the printed labels are not, so
 * the mapping is a table with a best guess, overridable by HARDWARE CALIBRATION
 * (hold OCT- and OCT+ while powering on): FELUCCA asks for each label in turn.
 * The learned table lives in .noinit and, with FELUCCA_FLASH, in flash with
 * the settings (project.c); read it back with `fm1t memr` to bake it in. */
#include "panel.h"
static const char *const B_NAME[NB] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                        "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const E_NAME[NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2",
                                        "KNOB 3", "KNOB 4"};
#define PANEL_MAGIC 0x50414E35u          /* "PAN5": bump when PANEL_DEFAULT changes */

#ifndef PANEL_NO_DEFINE
panel_t panel __attribute__((section(".noinit")));
#endif

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},   /* PLAY = 12, REC = 13 (measured on hardware) */
    {0, 1, 6, 2, 3, 4, 5},                           /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 (printed labels) */
    {1, 1, 1, 1, 1, 1, 1},
};

static void panel_init(void)                     /* also after a flash load: ids are used as array indexes and shifts */
{
    uint32_t i, buttons = 0, encoders = 0, ok = panel.magic == PANEL_MAGIC;
    for (i = 0; ok && i < NB; i++) {
        ok = panel.btn[i] < NB && !(buttons & (1u << panel.btn[i]));
        if (ok) buttons |= 1u << panel.btn[i];
    }
    for (i = 0; ok && i < NE; i++) {
        ok = panel.enc[i] < NE && !(encoders & (1u << panel.enc[i])) &&
             (panel.dir[i] == 1 || panel.dir[i] == -1);
        if (ok) encoders |= 1u << panel.enc[i];
    }
    if (!ok)
        panel = PANEL_DEFAULT;
}

static uint32_t panel_btn_of(uint32_t matrix_id)        /* label of a matrix button, NB if none */
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == matrix_id)
            return b;
    return NB;
}

/* Control-surface mode (docs/ANDROID-INTEGRATION.md): the FM-1 sends notes/CCs to a host and
 * stops playing locally. Off by default; the emulator sets it with web_ctl_surface(), the
 * hardware host will set it with the mode handshake. CC map: KNOB 1..4 = 22..25, SCL held =
 * 26..29; FX 102, HOME 103, SAVE 104, ENV 105, LFO 106, EDIT 107, GLO 108, ARP 109, PLAY 110,
 * REC 111; keys = notes 53..79 (F3..G5). FELUCCA_CTL: felucca.c / felucca_web.c define it;
 * the host tests that include panel.c do not, so the code compiles out there. */
#ifdef FELUCCA_CTL
static uint16_t ctl_knob[4] = {64, 64, 64, 64};
static uint32_t ctl_notes;
static void ctl_cc(uint32_t cc, uint32_t v)
{
    midi_out_event(0x0Bu | 0xB0u << 8 | (cc & 0x7Fu) << 16 | (v & 0x7Fu) << 24);
}
static void ctl_note(uint32_t note, uint32_t on)
{
    midi_out_event(on ? (0x09u | 0x90u << 8 | (note & 0x7Fu) << 16 | 100u << 24)
                      : (0x08u | 0x80u << 8 | (note & 0x7Fu) << 16));
}
static void ctl_surface_set(uint32_t on) { ctl_surface = on != 0; }
static void ctl_surface_enc(uint32_t role, int32_t steps)
{
    int32_t v;
    uint32_t k;
    if (!ctl_surface || role < EN_K1 || role > EN_K4 || !steps)
        return;
    k = role - EN_K1;
    v = (int32_t)ctl_knob[k] + steps * 4;
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    ctl_knob[k] = (uint16_t)v;
    ctl_cc(((fm1_in.buttons & (1u << panel.btn[B_SCL])) ? 26u : 22u) + k, (uint32_t)v);
}
static uint32_t ctl_btn_prev;
static void ctl_surface_buttons(uint32_t pressed)
{
    static const uint8_t CC[NB] = {102, 0, 105, 106, 107, 108, 103, 104, 109, 0, 110, 111, 0, 0};
    uint32_t b, now = fm1_in.buttons, released = ctl_btn_prev & ~now;
    if (!ctl_surface) {
        ctl_btn_prev = now;
        return;
    }
    for (b = 0; b < NB; b++) {
        if (!CC[b])
            continue;
        if ((pressed >> b) & 1u) ctl_cc(CC[b], 127);
        if ((released >> b) & 1u) ctl_cc(CC[b], 0);
    }
    ctl_btn_prev = now;
}
static void ctl_surface_notes(uint32_t pressed)
{
    uint32_t k, now, off;
    if (!ctl_surface)
        return;
    now = fm1_in.notes;
    off = ctl_notes & ~now;
    for (k = 0; k < 27u; k++)
        if ((off >> k) & 1u) ctl_note(53u + k, 0);
    for (k = 0; k < 27u; k++)
        if ((pressed >> k) & 1u) ctl_note(53u + k, 1);
    ctl_notes = now;
}
#endif

/* steps of a role, + = clockwise */
static uint8_t panel_moved;                     /* a knob turned since ui.c scr_input last looked (SCREEN OFF) */
static int32_t panel_enc(uint32_t role)
{
    int32_t s = fm1_enc_take(panel.enc[role]) * panel.dir[role];
    if (s) {
        panel_moved = 1;
#ifdef FELUCCA_CTL
        ctl_surface_enc(role, s);
#endif
    }
    return s;
}

/* Keep the retained settings layout fixed. palette: the index into UI_PALETTES (SET3 held an index
 * into the 20 palettes of earlier firmware) */
#define SETTINGS_MAGIC 0x53455434u              /* "SET4" */
#define SETTINGS_MAGIC_OLD 0x53455433u          /* "SET3" */
struct { uint32_t magic, palette, lowcut, zoom; } settings __attribute__((section(".noinit")));

static void settings_save(void);              /* project.c: flash copy (FELUCCA_FLASH) */

/* HOLD (menu): how long a button is held before its layer opens (ui_input.c). Saved in the settings record's
 * retired bold field as HOLD_TAG | index; any other value there (0 or 1 from older firmware) is the default */
#define HOLD_TAG 0x484C4400u
#define HOLD_DEF 1u
static const uint16_t HOLD_MS[4] = {300, 400, 500, 600};
static uint8_t settings_hold = HOLD_DEF;
static uint32_t hold_from_stored(uint32_t v) { return (v & ~3u) == HOLD_TAG ? v & 3u : HOLD_DEF; }
static uint32_t hold_to_stored(uint32_t old, uint32_t i)
{
    return i % 4u != HOLD_DEF ? HOLD_TAG | (i & 3u) : old > 1u ? 0u : old;
}
static int hold_stored_ok(uint32_t v) { return v <= 1u || (v & ~3u) == HOLD_TAG; }

/* LEDS (menu): OFF, DIM LO, DIM HI, INV. DIM HI (the default), the idle buttons and keys glow dim (~1/30) and the
 * active ones are lit (#35); DIM LO the same, darker (~1/60, hal/fm1_input.h FM1_LED_DIM_LO_NS); OFF no glow,
 * the active ones lit (as 1.0); INV the idle ones lit and the active ones dark, as the stock firmware (ui_input.c
 * ui_leds). Saved in the settings record's retired zoom field as LEDS_TAG | mode, the modes append-only: DIM 0
 * (now DIM HI) and INV 1 keep their values, OFF 2 and DIM LO 3 are new; any other value there (0 or 1 from older
 * firmware) is DIM HI. LEDS_MENU: the menu's order, darkest first */
#define LEDS_TAG 0x4C454400u                    /* "LED" */
enum { LEDS_DIM, LEDS_INV, LEDS_OFF, LEDS_DIM_LO, LEDS_COUNT };
static const uint8_t LEDS_MENU[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
static const char *const LEDS_NAME[LEDS_COUNT] = {"DIM HI", "INV", "OFF", "DIM LO"};
static uint8_t settings_leds = LEDS_DIM;
static uint32_t leds_from_stored(uint32_t v) { return (v & ~3u) == LEDS_TAG && (v & 3u) < LEDS_COUNT ? v & 3u : LEDS_DIM; }
static uint32_t leds_to_stored(uint32_t old, uint32_t m)
{
    return m != LEDS_DIM && m < LEDS_COUNT ? LEDS_TAG | m : old > 1u ? 0u : old;
}
static int leds_stored_ok(uint32_t v) { return v <= 1u || ((v & ~3u) == LEDS_TAG && (v & 3u) < LEDS_COUNT); }
/* the menu's next / previous mode (KNOB 1 stops at the ends, OCT+ (s 0) cycles) */
static uint32_t leds_step(uint32_t m, int32_t s)
{
    uint32_t i = 0;
    while (i + 1u < LEDS_COUNT && LEDS_MENU[i] != m)
        i++;
    i = s > 0 ? (i + 1u < LEDS_COUNT ? i + 1u : i) : s < 0 ? (i ? i - 1u : 0u) : (i + 1u) % LEDS_COUNT;
    return LEDS_MENU[i];
}

static void settings_init(void)
{
    if (settings.magic == SETTINGS_MAGIC_OLD && settings.palette < 20u) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = palette_from_stored(settings.palette);
    }
    if (settings.magic != SETTINGS_MAGIC || settings.palette >= NPALETTES) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = UI_GREY_INDEX;      /* GREY (default; named MONO before 1.0.2) */
        settings.lowcut = 0;
        settings.zoom = 0;                     /* (retired: the LEDS setting, settings_persist.c) */
    }
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut % 3u);
}
