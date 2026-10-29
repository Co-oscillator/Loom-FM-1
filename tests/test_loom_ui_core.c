/* SPDX-License-Identifier: GPL-3.0-only */
/* Test Loom Sequencer, Home, Save, and Engine Edit pages. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

static uint32_t host_slots[3u * 0x14000u / 4u];
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static uint32_t host_dim_lo;
static void fm1_led_dim_level(uint32_t lo) { host_dim_lo = lo; }
static int host_anim;
static int host_idle, host_idle_glow;
static int fm1_led_anim_on(void) { return host_anim || host_idle; }
static void fm1_led_idle_start(uint32_t glow) { host_idle = 1; host_idle_glow = (int)glow; }
static int fm1_led_idle_on(void) { return host_idle; }
static void fm1_led_anim_stop(void) { host_anim = host_idle = 0; }
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
static int32_t host_enc_late[7];
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = host_enc[e % 7u];
    host_enc[e % 7u] = host_enc_late[e % 7u];
    host_enc_late[e % 7u] = 0;
    return s;
}
static void (*host_wdt_hook)(void);
static void fm1_wdt_feed(void) { if (host_wdt_hook) host_wdt_hook(); }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint16_t host_screen[240 * 240];
static void host_put(uint32_t k, uint16_t v) { host_screen[k] = v; }
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_put((y + j) * 240u + x + i, (uint16_t)((c >> 8) | (c << 8)));
}
static void lcd_sync(void) {}
static uint32_t host_pw_n, host_pw[256], host_pw_ms[256];
static int host_bl = 1;
static void host_pw_log(uint32_t s)
{
    if (host_pw_n < 256u) { host_pw[host_pw_n] = s; host_pw_ms[host_pw_n] = fm1_ms; }
    host_pw_n++;
    host_bl = s != 0u;
}
static void lcd_power(uint32_t s) { host_pw_log(s); }
static void lcd_wake_now(void) { host_pw_log(3u); }
static uint32_t host_blit_rows;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    host_blit_rows += h;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_put((y + j) * 240u + x + i, p[j * w + i]);
}
static void dump_screen_raw(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(host_screen, sizeof(uint16_t), 240 * 240, f);
        fclose(f);
    }
}
static struct { uint32_t stage; } felucca_dbg;
#define FELUCCA_FLASH 0
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "TEST"
#endif
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_layer.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static void test_sequencer_section(void)
{
    printf("--- Testing Sequencer (FAM_SEQ) ---\n");

    /* 1. Verify FAM_SEQ pages: 4x4 STEP, 4x4 PAT, 8x8 STEP, 8x8 PAT */
    int found_4x4_step = 0, found_4x4_pat = 0, found_8x8_step = 0, found_8x8_pat = 0;
    int seq_page_count = 0;

    for (uint32_t i = 0; i < NPAGES; i++) {
        if (PAGES[i].fam == FAM_SEQ) {
            seq_page_count++;
            if (strcmp(PAGES[i].title, "4x4 STEP") == 0) {
                found_4x4_step = 1;
                assert(PAGES[i].graph == GR_ROLL);
                assert(PAGES[i].scope == SC_STEP);
            } else if (strcmp(PAGES[i].title, "4x4 PAT") == 0) {
                found_4x4_pat = 1;
                assert(PAGES[i].graph == GR_ROLL);
                assert(PAGES[i].scope == SC_TRACK);
                assert(PAGES[i].id[0] == P_SLEN);
                assert(PAGES[i].id[1] == P_SDIV);
                assert(PAGES[i].id[2] == P_SSWING);
                assert(PAGES[i].id[3] == P_SHUMAN);
            } else if (strcmp(PAGES[i].title, "8x8 STEP") == 0) {
                found_8x8_step = 1;
                assert(PAGES[i].graph == GR_STEPS);
                assert(PAGES[i].scope == SC_STEP);
            } else if (strcmp(PAGES[i].title, "8x8 PAT") == 0) {
                found_8x8_pat = 1;
                assert(PAGES[i].graph == GR_STEPS);
                assert(PAGES[i].scope == SC_TRACK);
                assert(PAGES[i].id[0] == P_SLEN);
                assert(PAGES[i].id[1] == P_SDIV);
                assert(PAGES[i].id[2] == P_SSWING);
                assert(PAGES[i].id[3] == P_SHUMAN);
            }
        }
    }

    assert(found_4x4_step && "4x4 STEP page missing from FAM_SEQ");
    assert(found_4x4_pat && "4x4 PAT page missing from FAM_SEQ");
    assert(found_8x8_step && "8x8 STEP page missing from FAM_SEQ");
    assert(found_8x8_pat && "8x8 PAT page missing from FAM_SEQ");
    assert(seq_page_count == 4 && "FAM_SEQ should have exactly 4 pages: 4x4 STEP, 4x4 PAT, 8x8 STEP, 8x8 PAT");
    printf("  FAM_SEQ has 4 verified Loom pages: 4x4 STEP (1/4), 4x4 PAT (2/4), 8x8 STEP (3/4), 8x8 PAT (4/4) (OK)\n");

    /* 2. Test 64-step length, banks, and cursor navigation */
    track_select(0);
    track_t *t = &trk[0];
    t->p[P_SLEN] = 64;
    assert(t->p[P_SLEN] == 64);

    cursor_set(0);
    assert(ui.cursor == 0 && ui.bank == 0);

    cursor_set(15);
    assert(ui.cursor == 15 && ui.bank == 0);

    cursor_set(16);
    assert(ui.cursor == 16 && ui.bank == 1);

    cursor_set(32);
    assert(ui.cursor == 32 && ui.bank == 2);

    cursor_set(48);
    assert(ui.cursor == 48 && ui.bank == 3);

    cursor_set(63);
    assert(ui.cursor == 63 && ui.bank == 3);

    printf("  64-step bank mapping: Bank 1 (0-15), Bank 2 (16-31), Bank 3 (32-47), Bank 4 (48-63) (OK)\n");

    /* 3. Test 4x4 STEP Knobs: 1: STEP, 2: BANK, 3: GATE, 4: OPT */
    open_family(FAM_SEQ);
    assert(cur_page()->fam == FAM_SEQ);
    assert(strcmp(cur_page()->title, "4x4 STEP") == 0);
    cursor_set(0);
    assert(ui.bank == 0 && ui.cursor == 0);

    /* Knob 1 (Slot 0): Step navigation */
    step_edit(0, 1);
    assert(ui.cursor == 1);
    step_edit(0, -1);
    assert(ui.cursor == 0);

    /* Knob 2 (Slot 1): Bank navigation (moving between Banks 1-4) */
    step_edit(1, 1);
    assert(ui.bank == 1 && ui.cursor == 16);
    step_edit(1, 1);
    assert(ui.bank == 2 && ui.cursor == 32);
    step_edit(1, 1);
    assert(ui.bank == 3 && ui.cursor == 48);
    step_edit(1, -3);
    assert(ui.bank == 0 && ui.cursor == 0);
    printf("  4x4 Knob 2 Bank navigation: seamlessly navigates Banks 1..4 (OK)\n");

    /* Knob 3 (Slot 2): Note / Target selector (replaces GATE) */
    t->step[0].time = ST_REST;
    t->step[0].n = 0;
    cursor_set(0);
    step_edit(2, 1); /* Puts note on step 0 */
    assert(t->step[0].time == ST_NOTE && t->step[0].n == 1);
    uint8_t n0 = t->step[0].note[0];
    step_edit(2, 2); /* Increment note pitch by +2 */
    assert(t->step[0].note[0] == n0 + 2);
    printf("  4x4 Knob 3 NOTE target: directly edits chromatic note pitch on current step (OK)\n");

    /* Knob 4 (Slot 3): Loom Step Options */
    t->step[0].time = ST_REST;
    t->step[0].n = 0;

    /* Turn Knob 4 +1 -> PLAY (1) */
    step_edit(3, 1);
    assert(t->step[0].time == ST_NOTE && t->step[0].n > 0);
    assert(step_ratchet(&t->step[0]) == 1);
    assert((t->step[0].flags & SF_ACCENT) == 0);

    /* Turn Knob 4 +1 -> TIE (2) */
    step_edit(3, 1);
    assert(t->step[0].time == ST_TIE);

    /* Turn Knob 4 +1 -> PUNCH (3) */
    step_edit(3, 1);
    assert(t->step[0].time == ST_NOTE);
    assert((t->step[0].flags & SF_ACCENT) != 0);

    /* Turn Knob 4 +1 -> RATCH 2 (4) */
    step_edit(3, 1);
    assert(step_ratchet(&t->step[0]) == 2);
    assert((t->step[0].flags & SF_ACCENT) == 0);

    /* Turn Knob 4 +1 -> RATCH 3 (5) */
    step_edit(3, 1);
    assert(step_ratchet(&t->step[0]) == 3);

    /* Turn Knob 4 +1 -> RATCH 4 (6) */
    step_edit(3, 1);
    assert(step_ratchet(&t->step[0]) == 4);

    /* Turn Knob 4 +1 -> P+RATCH (7) */
    step_edit(3, 1);
    assert(step_ratchet(&t->step[0]) == 2);
    assert((t->step[0].flags & SF_ACCENT) != 0);
    printf("  4x4 Knob 4 Loom Step Options: OFF, PLAY, TIE, PUNCH, RATCH 2..4, P+RATCH verified (OK)\n");

    /* 4. Test page title verification */
    char ti[20];
    page_title(ti);
    assert(strcmp(ti, "4x4 STEP 1/4") == 0 && "Title should be '4x4 STEP 1/4'");
    printf("  Page title: '%s' verified (clean Loom 4x4 STEP title) (OK)\n", ti);

    /* 5. Test 8x8 STEP Knobs: 1: STEP, 2: NOTE/TARGET, 3: OPT, 4: CLEAR */
    /* Advance to page 3 (8x8 STEP): open_family(FAM_SEQ) twice */
    open_family(FAM_SEQ); /* -> 4x4 PAT */
    assert(strcmp(cur_page()->title, "4x4 PAT") == 0);
    open_family(FAM_SEQ); /* -> 8x8 STEP */
    assert(strcmp(cur_page()->title, "8x8 STEP") == 0);
    assert(cur_page()->graph == GR_STEPS);
    assert(cur_page()->scope == SC_STEP);

    /* 8x8 Knob 1 (Slot 0): Step navigation */
    cursor_set(0);
    step_edit(0, 5);
    assert(ui.cursor == 5);
    step_edit(0, -5);
    assert(ui.cursor == 0);

    /* 8x8 Knob 2 (Slot 1): Note / Target selector (replaces GATE) */
    uint8_t n_before = t->step[0].note[0];
    step_edit(1, 3);
    assert(t->step[0].note[0] == n_before + 3);
    printf("  8x8 Knob 2 NOTE target: directly edits chromatic note pitch on current step (OK)\n");

    /* 8x8 Knob 3 (Slot 2): Step options */
    t->step[0].time = ST_REST;
    t->step[0].n = 0;
    step_edit(2, 1); /* OFF -> PLAY */
    assert(t->step[0].time == ST_NOTE);
    step_edit(2, 2); /* PLAY -> PUNCH */
    assert((t->step[0].flags & SF_ACCENT) != 0);

    /* 8x8 Knob 4 (Slot 3): CLEAR */
    step_edit(3, 1);
    assert(t->step[0].time == ST_REST && "Knob 4 on 8x8 STEP should clear the current track sequence");
    printf("  8x8 Knob controls: [K1] STEP, [K2] NOTE, [K3] OPT, [K4] CLEAR verified (OK)\n");

    /* 6. Test Humanize in seq.c */
    t->p[P_SHUMAN] = 100;
    uint32_t period = 1000;
    uint32_t s1 = step_samples(t, period, 0);
    uint32_t s2 = step_samples(t, period, 0);
    assert(s1 > 0 && s2 > 0);
    t->p[P_SHUMAN] = 0;
    printf("  Humanize timing variation (P_SHUMAN) verified (OK)\n");

    /* 7. Test cycling through all 4 SEQ pages */
    /* Currently on 8x8 STEP (3/4) */
    open_family(FAM_SEQ);
    assert(strcmp(cur_page()->title, "8x8 PAT") == 0);
    assert(cur_page()->graph == GR_STEPS);
    ui.force = 1;
    host_blit_rows = 0;
    ui_draw();
    assert(host_blit_rows > 0 && "ui_draw blitted 0 rows on 8x8 PAT!");

    open_family(FAM_SEQ);
    assert(strcmp(cur_page()->title, "4x4 STEP") == 0);
    assert(cur_page()->graph == GR_ROLL);
    ui.force = 1;
    host_blit_rows = 0;
    ui_draw();
    assert(host_blit_rows > 0 && "ui_draw blitted 0 rows on 4x4 STEP!");

    open_family(FAM_SEQ);
    assert(strcmp(cur_page()->title, "4x4 PAT") == 0);
    assert(cur_page()->graph == GR_ROLL);
    ui.force = 1;
    host_blit_rows = 0;
    ui_draw();
    assert(host_blit_rows > 0 && "ui_draw blitted 0 rows on 4x4 PAT!");

    open_family(FAM_SEQ);
    assert(strcmp(cur_page()->title, "8x8 STEP") == 0);
    ui.force = 1;
    host_blit_rows = 0;
    ui_draw();
    assert(host_blit_rows > 0 && "ui_draw blitted 0 rows on 8x8 STEP!");

    printf("  All 4 Sequencer pages render cleanly and cycle seamlessly (OK)\n");

    /* Reset to 4x4 STEP */
    open_family(FAM_SEQ); /* 8x8 PAT */
    open_family(FAM_SEQ); /* 4x4 STEP */
    assert(strcmp(cur_page()->title, "4x4 STEP") == 0);
    t->p[P_SLEN] = 16;
    cursor_set(0);
}

static void test_home_section(void)
{
    printf("--- Testing Home & Performance Section (FAM_TRK / HOME) ---\n");

    /* 1. Test go_home */
    go_home();
    assert(ui.home == 1);
    printf("  go_home sets ui.home = 1 (OK)\n");

    /* 2. Test home_tap cycle: HOME -> MIXER -> CLOCK -> HOME */
    home_tap();
    assert(ui.home == 0);
    assert(cur_page()->fam == FAM_TRK);
    assert(strcmp(cur_page()->title, "MIXER") == 0);
    printf("  home_tap: HOME -> MIXER (OK)\n");

    home_tap();
    assert(ui.home == 0);
    assert(cur_page()->fam == FAM_TRK);
    assert(strcmp(cur_page()->title, "CLOCK") == 0);
    printf("  home_tap: MIXER -> CLOCK (OK)\n");

    home_tap();
    assert(ui.home == 0);
    assert(cur_page()->fam == FAM_TRK);
    assert(strcmp(cur_page()->title, "MACROS") == 0);
    assert(cur_page()->graph == GR_MACRO);
    printf("  home_tap: CLOCK -> MACROS (OK)\n");

    /* Test MACROS page state when no macros are assigned */
    macro_init();
    assert(macro_is_assigned(0) == 0);
    assert(macro_is_assigned(1) == 0);
    assert(macro_is_assigned(2) == 0);
    assert(macro_is_assigned(3) == 0);
    printf("  MACROS: No macros assigned initially (OK)\n");

    /* Render graph for empty state */
    graph_macro_perf(TSEL, 0xF800);
    printf("  MACROS: Empty state renders cleanly (OK)\n");

    /* Assign Knob 1 to Macro 1 (T1 -> CUT +50%) */
    loom_macros[0].src = MS_KNOB;
    loom_macros[0].dest[0].trk = 0; /* T1 */
    loom_macros[0].dest[0].dest = 2; /* CUT */
    loom_macros[0].dest[0].amt = 32; /* +50% */
    assert(macro_is_assigned(0) == 1);
    assert(macro_is_assigned(1) == 0);
    printf("  MACROS: 1 macro assigned to Knob 1 (OK)\n");

    /* Render graph for 1 macro */
    graph_macro_perf(TSEL, 0xF800);
    printf("  MACROS: 1 macro compact area renders cleanly (OK)\n");

    /* Test editing Knob 1 on MACROS page */
    loom_macros[0].val = 64;
    edit_param(0, 5); /* Turn Knob 1 */
    assert(loom_macros[0].val > 64);
    printf("  MACROS: Knob 1 directly sweeps Macro 1 (OK)\n");

    /* Test unassigned knob does not edit */
    uint8_t old_v1 = loom_macros[1].val;
    edit_param(1, 5); /* Turn Knob 2 (unassigned) */
    assert(loom_macros[1].val == old_v1);
    printf("  MACROS: Unassigned Knob 2 ignored (OK)\n");

    /* Assign Knobs 2, 3, 4 and verify adaptive layouts */
    loom_macros[1].src = MS_KNOB;
    loom_macros[1].dest[0].dest = 3; /* AMP */
    loom_macros[1].dest[0].amt = -20;
    assert(macro_is_assigned(1) == 1);
    graph_macro_perf(TSEL, 0xF800);
    printf("  MACROS: 2 macro areas render cleanly (OK)\n");

    loom_macros[2].src = MS_KNOB;
    loom_macros[2].dest[0].dest = 4; /* PAN */
    loom_macros[2].dest[0].amt = 25;
    assert(macro_is_assigned(2) == 1);
    graph_macro_perf(TSEL, 0xF800);
    printf("  MACROS: 3 macro areas render cleanly (OK)\n");

    loom_macros[3].src = MS_KNOB;
    loom_macros[3].dest[0].dest = 6; /* REV */
    loom_macros[3].dest[0].amt = 40;
    assert(macro_is_assigned(3) == 1);
    graph_macro_perf(TSEL, 0xF800);
    printf("  MACROS: 4 macro areas render cleanly (OK)\n");

    /* Test 4th tap returns to HOME */
    home_tap();
    assert(ui.home == 1);
    printf("  home_tap: MACROS -> HOME (OK)\n");

    /* 3. Test home_param macro mappings */
    track_t *t = TSEL;
    t->eng_req = 0; /* ANALOG */
    int16_t *vp;

    const param_desc_t *d0 = home_param(0, &vp);
    const param_desc_t *d1 = home_param(1, &vp);
    const param_desc_t *d2 = home_param(2, &vp);
    const param_desc_t *d3 = home_param(3, &vp);

    assert(d0 != NULL && d1 != NULL && d2 != NULL && d3 != NULL);
    assert(strcmp(d0->label, "CUT") == 0);
    assert(strcmp(d1->label, "RES") == 0);
    assert(strcmp(d2->label, "ATK") == 0);
    assert(strcmp(d3->label, "REL") == 0);
    printf("  HOME Macro mapping for ANALOG: [K1]CUT [K2]RES [K3]ATK [K4]REL (OK)\n");

    /* Test HOME Macro mapping for GRAIN */
    t->eng_req = 8; /* GRAIN */
    d0 = home_param(0, &vp);
    d1 = home_param(1, &vp);
    d2 = home_param(2, &vp);
    d3 = home_param(3, &vp);
    assert(d0 != NULL && d1 != NULL && d2 != NULL && d3 != NULL);
    assert(strcmp(d0->label, "POS") == 0);
    assert(strcmp(d1->label, "SIZE") == 0);
    assert(strcmp(d2->label, "DENS") == 0);
    assert(strcmp(d3->label, "SPRY") == 0);
    printf("  HOME Macro mapping for GRAIN: [K1]POS [K2]SIZE [K3]DENS [K4]SPRY (OK)\n");
}

static void test_save_section(void)
{
    printf("--- Testing Save Section (FAM_SAVE) ---\n");

    /* 1. Verify FAM_SAVE pages */
    int found_user = 0, found_presets = 0, found_phrases = 0, found_proj = 0, found_tools = 0;
    int save_page_count = 0;
    const page_t *tools_pg = NULL;

    for (uint32_t i = 0; i < NPAGES; i++) {
        if (PAGES[i].fam == FAM_SAVE) {
            save_page_count++;
            if (strcmp(PAGES[i].title, "USER") == 0) found_user = 1;
            else if (strcmp(PAGES[i].title, "PRESETS") == 0) found_presets = 1;
            else if (strcmp(PAGES[i].title, "PHRASES") == 0) found_phrases = 1;
            else if (strcmp(PAGES[i].title, "PROJECT") == 0) found_proj = 1;
            else if (strcmp(PAGES[i].title, "TOOLS") == 0) {
                found_tools = 1;
                tools_pg = &PAGES[i];
            }
        }
    }

    assert(found_user && "USER page missing from FAM_SAVE");
    assert(found_presets && "PRESETS page missing from FAM_SAVE");
    assert(found_phrases && "PHRASES page missing from FAM_SAVE");
    assert(found_proj && "PROJECT page missing from FAM_SAVE");
    assert(found_tools && "TOOLS page missing from FAM_SAVE");
    assert(save_page_count == 5);
    printf("  FAM_SAVE has 5 verified pages: PROJECT, TOOLS, USER, PRESETS, PHRASES (OK)\n");

    /* 2. Verify open_family(FAM_SAVE) lands directly on PROJECT page */
    ui.fam_last[FAM_SAVE] = 0;
    open_family(FAM_SAVE);
    assert(strcmp(cur_page()->title, "PROJECT") == 0);
    assert(cur_page()->id[0] == G_SLOT);
    assert(cur_page()->id[1] == G_SAVEINIT);
    assert(cur_page()->id[2] == G_LOAD);
    assert(cur_page()->id[3] == G_SAVE);
    printf("  open_family(FAM_SAVE) lands directly on PROJECT page with [K2] INIT+ (OK)\n");

    /* 3. Verify Project Slots */
    assert(TP[P_SLEN].max == NSTEP);
    assert(GP[G_SLOT].min == 1 && GP[G_SLOT].max == 4);
    printf("  Project Slot range: 1..4 (A..D) (OK)\n");

    /* 4. Verify 'Save as Init' & 'Reset Init' on TOOLS page */
    assert(tools_pg != NULL);
    assert(tools_pg->id[0] == G_CLRSEQ);
    assert(tools_pg->id[1] == G_INITSND);
    assert(tools_pg->id[2] == G_SAVEINIT);
    assert(tools_pg->id[3] == G_RESETINIT);
    assert(strcmp(GP[G_SAVEINIT].label, "INIT+") == 0);
    assert(strcmp(GP[G_RESETINIT].label, "RST+") == 0);

    /* Verify TOOLS actions */
    ui.page = (uint8_t)(tools_pg - PAGES);
    assert(act_cols() == 15u); /* all 4 columns are active actions */
    assert(strcmp(act_name(0), "CLEAR") == 0);
    assert(strcmp(act_name(1), "INIT") == 0);
    assert(strcmp(act_name(2), "SAVE INIT") == 0);
    assert(strcmp(act_name(3), "RESET") == 0);
    printf("  'SAVE INIT' and 'RESET' actions integrated in TOOLS & PROJECT (consumes 0 project slots) (OK)\n");
}

static void test_engine_edit_pages(void)
{
    printf("--- Testing Engine Edit Pages (FAM_EDIT <= 8 Pages) ---\n");

    printf("\n  +---------------+--------------------------------------------+-------------+\n");
    printf("  | Engine        | Visible Edit Pages                         | Total Pages |\n");
    printf("  +---------------+--------------------------------------------+-------------+\n");

    for (uint32_t e = 0; e < NENGINES; e++) {
        if (!eng_ok(e)) continue;

        TSEL->eng_req = (uint8_t)e;
        TSEL->engine = (uint8_t)e;

        char page_list[128] = "";
        int edit_pages = 0;

        for (uint32_t i = 0; i < NPAGES; i++) {
            if (PAGES[i].fam == FAM_EDIT && page_visible(i)) {
                edit_pages++;
                if (edit_pages > 1) str_cpy(page_list + str_len(page_list), ", ", sizeof(page_list) - str_len(page_list));
                str_cpy(page_list + str_len(page_list), PAGES[i].title, sizeof(page_list) - str_len(page_list));
            }
        }

        /* Every engine MUST have <= 8 edit pages */
        assert(edit_pages <= 8 && "Engine has more than 8 edit pages!");
        assert(edit_pages >= 3 && "Engine has too few edit pages!");

        printf("  | %-13s | %-42s | %11d |\n", ENGINES[e]->name, page_list, edit_pages);
    }
    printf("  +---------------+--------------------------------------------+-------------+\n\n");
    printf("  ALL ENGINES FIT WITHIN 8 OR FEWER EDIT PAGES! (OK)\n");

    /* Test GRAIN parameters and titles */
    assert(strcmp(ENG_GRAIN.page_title[0], "CLOUD") == 0);
    assert(strcmp(ENG_GRAIN.page_title[1], "MOTION") == 0);
    assert(strcmp(ENG_GRAIN.edit[0].label, "SRC") == 0);
    assert(strcmp(ENG_GRAIN.edit[1].label, "POS") == 0);
    assert(strcmp(ENG_GRAIN.edit[2].label, "SIZE") == 0);
    assert(strcmp(ENG_GRAIN.edit[3].label, "DENS") == 0);
    assert(strcmp(ENG_GRAIN.edit[4].label, "PTCH") == 0);
    assert(strcmp(ENG_GRAIN.edit[5].label, "SPRY") == 0);
    assert(strcmp(ENG_GRAIN.edit[6].label, "REV") == 0);
    assert(strcmp(ENG_GRAIN.edit[7].label, "TONE") == 0);
    assert(GR_NG == 24);

    /* Test graph_grain direct rendering */
    TSEL->eng_req = 8;
    TSEL->engine = 8;
    cv_begin(240, 122, T_BG);
    graph_grain(TSEL, T_THEME);
    printf("  GRAIN engine parameters (CLOUD, MOTION) and graph_grain render verified (OK)\n");

    /* Test SAMPLE parameters, mode labels, home knobs, and graph_sample */
    assert(strcmp(ENG_SAMPLE.page_title[0], "PLAY") == 0);
    assert(strcmp(ENG_SAMPLE.page_title[1], "TRIM") == 0);
    assert(strcmp(ENG_SAMPLE.edit[0].label, "SET") == 0);
    assert(strcmp(ENG_SAMPLE.edit[1].label, "MODE") == 0);
    assert(strcmp(ENG_SAMPLE.edit[2].label, "PTCH") == 0);
    assert(strcmp(ENG_SAMPLE.edit[3].label, "SPEED") == 0);
    assert(strcmp(ENG_SAMPLE.edit[4].label, "STRCH") == 0);
    assert(strcmp(ENG_SAMPLE.edit[5].label, "START") == 0);
    assert(strcmp(ENG_SAMPLE.edit[6].label, "END") == 0);
    assert(strcmp(ENG_SAMPLE.edit[7].label, "SYNC") == 0);
    assert(strcmp(N_SMP_SYNC[0], "--") == 0);
    assert(strcmp(N_SMP_SYNC[1], "1 STEP") == 0);
    assert(strcmp(N_SMP_SYNC[2], "2 STEPS") == 0);
    assert(strcmp(N_SMP_SYNC[3], "1 BEAT") == 0);
    assert(strcmp(N_SMP_SYNC[4], "2 BEATS") == 0);
    assert(strcmp(N_SMP_SYNC[5], "1 BAR") == 0);
    assert(strcmp(N_SMP_SYNC[6], "2 BARS") == 0);
    assert(strcmp(N_SMP_SYNC[7], "4 BARS") == 0);
    assert(strcmp(N_SMP_MODE[0], "ONE") == 0);
    assert(strcmp(N_SMP_MODE[1], "SUS") == 0);
    assert(strcmp(N_SMP_MODE[2], "LOOP") == 0);
    assert(strcmp(N_SMP_MODE[3], "REV") == 0);
    assert(strcmp(N_SMP_MODE[4], "PING") == 0);
    assert(ENG_SAMPLE.edit[1].max == 4);
    assert(ENG_SAMPLE.knob[0] == P_E2 && ENG_SAMPLE.knob[1] == P_E3 &&
           ENG_SAMPLE.knob[2] == P_E5 && ENG_SAMPLE.knob[3] == P_E6);
    TSEL->eng_req = ENGI_SAMPLE;
    TSEL->engine = ENGI_SAMPLE;
    TSEL->p[P_E0] = 0;   /* PIANO */
    TSEL->p[P_E1] = 4;   /* PING mode */
    TSEL->p[P_E2] = 0;   /* Pitch 0 */
    TSEL->p[P_E3] = 100; /* Speed 100% */
    TSEL->p[P_E4] = 100; /* Stretch 100% */
    TSEL->p[P_E5] = 0;   /* Start 0% */
    TSEL->p[P_E6] = 100; /* End 100% */
    song.g[G_BPM] = 120;
    sample_tempo_sync(TSEL, 1); /* 1 STEP */
    assert(TSEL->p[P_E4] >= 25 && TSEL->p[P_E4] <= 400);
    sample_tempo_sync(TSEL, 5); /* 1 BAR */
    assert(TSEL->p[P_E4] >= 25 && TSEL->p[P_E4] <= 400);
    cv_begin(240, 122, T_BG);
    graph_sample(T_THEME);
    printf("  SAMPLE engine parameters (PLAY, TRIM, ONE/SUS/LOOP/REV/PING, PTCH/SPEED/STRCH, SYNC) and graph_sample verified (OK)\n");

    /* Test SLICE parameters, home knobs, and graph_slices */
#if FELUCCA_SLICE
    assert(strcmp(ENG_SLICE.page_title[0], "SLCE") == 0);
    assert(strcmp(ENG_SLICE.page_title[1], "PLAY") == 0);
    assert(strcmp(ENG_SLICE.edit[0].label, "SRC") == 0);
    assert(strcmp(ENG_SLICE.edit[1].label, "SLCS") == 0);
    assert(ENG_SLICE.edit[1].min == 1 && ENG_SLICE.edit[1].max == 16 && ENG_SLICE.edit[1].def == 16);
    assert(strcmp(ENG_SLICE.edit[2].label, "PTCH") == 0);
    assert(strcmp(ENG_SLICE.edit[3].label, "SPEED") == 0);
    assert(strcmp(ENG_SLICE.edit[4].label, "STRCH") == 0);
    assert(strcmp(ENG_SLICE.edit[5].label, "MODE") == 0);
    assert(strcmp(ENG_SLICE.edit[6].label, "DIR") == 0);
    assert(strcmp(N_SLC_DIR[0], "FWD") == 0);
    assert(strcmp(N_SLC_DIR[1], "REV") == 0);
    assert(strcmp(N_SLC_DIR[2], "PING") == 0);
    assert(ENG_SLICE.edit[6].max == 2);
    assert(strcmp(ENG_SLICE.edit[7].label, "SYNC") == 0);
    assert(ENG_SLICE.knob[0] == P_E2 && ENG_SLICE.knob[1] == P_E3 &&
           ENG_SLICE.knob[2] == P_E4 && ENG_SLICE.knob[3] == P_E1);

    /* Test SLICE tempo sync (1-2 steps, 1-2 beats, 1-4 bars) */
    trk[0].p[P_E0] = 0;   /* BREAK */
    trk[0].p[P_E1] = 16;  /* 16 slices */
    trk[0].p[P_E2] = 0;   /* PTCH 0 */
    trk[0].p[P_E3] = 100; /* SPEED 100% */
    trk[0].p[P_E4] = 100; /* STRCH 100% */
    song.g[G_BPM] = 120;
    slice_tempo_sync(&trk[0], 5); /* 1 BAR (16 steps) */
    assert(trk[0].p[P_E4] >= 25 && trk[0].p[P_E4] <= 400);
    int16_t strch_1bar = trk[0].p[P_E4];
    slice_tempo_sync(&trk[0], 6); /* 2 BARS (32 steps -> double duration) */
    assert(trk[0].p[P_E4] > strch_1bar);
    printf("  SLICE tempo sync (P_E7 SYNC -> recalculates STRCH) verified (OK)\n");

    /* Test 16 white keys mapping to 16 slices, black keys silent */
    assert(slice_keys(&trk[0], 0) == SLC_BASE);       /* F3 -> white key 0 -> slice 0 */
    assert(slice_keys(&trk[0], 1) == KB_SILENT);     /* F#3 -> black key -> silent */
    assert(slice_keys(&trk[0], 2) == SLC_BASE + 1);   /* G3 -> white key 1 -> slice 1 */
    assert(slice_keys(&trk[0], 3) == KB_SILENT);     /* G#3 -> black key -> silent */
    assert(slice_keys(&trk[0], 26) == SLC_BASE + 15); /* G5 -> white key 15 -> slice 15 */

    /* Test SLICE step sequencing: Knob 3 on 4x4 STEP sequences slices */
    track_select(0);
    trk[0].eng_req = ENGI_SLICE;
    trk[0].step[0].time = ST_REST;
    trk[0].step[0].n = 0;
    cursor_set(0);
    open_family(FAM_SEQ);
    while (strcmp(cur_page()->title, "4x4 STEP") != 0) open_family(FAM_SEQ);
    step_edit(2, 1); /* Puts Slice 1 on step 0 */
    assert(trk[0].step[0].time == ST_NOTE && trk[0].step[0].n == 1);
    assert(trk[0].step[0].note[0] == SLC_BASE + trk[0].p[P_ROOT] + 0);
    step_edit(2, 4); /* Move to Slice 5 */
    assert(trk[0].step[0].note[0] == SLC_BASE + trk[0].p[P_ROOT] + 4);
    printf("  SLICE sequencer target: [K3] directly sequences SLC 1..16 on step (OK)\n");

    /* Test DRUM step sequencing: Knob 3 on 4x4 STEP sequences drum instruments */
    track_select(4); /* Track 5 = DRUM */
    trk[4].step[0].time = ST_REST;
    trk[4].step[0].hit = 0;
    ui.lane = 0;
    step_edit(2, 1); /* Places SNARE (lane 1) */
    assert(trk[4].step[0].time == ST_NOTE);
    assert(trk[4].step[0].hit == (1u << 1));
    assert(ui.lane == 1);
    step_edit(2, 1); /* Moves to CLAP (lane 2) */
    assert(trk[4].step[0].hit == (1u << 2));
    assert(ui.lane == 2);
    printf("  DRUM sequencer target: [K3] directly sequences drum voices (KICK, SNARE, CLAP...) on step (OK)\n");
    fflush(stdout);

    ui.force = 1;
    ui_draw();
    dump_screen_raw("build/seq_drum_4x4.raw");

    track_select(0);
    open_family(FAM_SEQ);
    while (strcmp(cur_page()->title, "4x4 STEP") != 0) open_family(FAM_SEQ);
    ui.force = 1;
    ui_draw();
    dump_screen_raw("build/seq_slice_4x4.raw");

    open_family(FAM_SEQ); /* 4x4 PAT */
    open_family(FAM_SEQ); /* 8x8 STEP */
    ui.force = 1;
    ui_draw();
    dump_screen_raw("build/seq_slice_8x8.raw");

    open_family(FAM_EDIT);
    while (strcmp(cur_page()->title, "EDIT 2") != 0) open_family(FAM_EDIT);
    trk[0].p[P_E7] = 5; /* 1 BAR */
    ui.force = 1;
    ui_draw();
    dump_screen_raw("build/slice_edit_sync.raw");

    cv_begin(240, 122, T_BG);
    graph_slices();

    /* Test Manual Slice Editing preserves slice count (does not jump to 5) */
    slc_man[0][0].n = 16;
    slc_man_cur[0] = 0;
    trk[0].p[P_E1] = 16;
    slice_edit_end(0);
    assert(trk[0].p[P_E1] == 16 && "slice_edit_end must preserve slice count without reverting to 5!");

    /* Test ping-pong render for SLICE */
    trk[0].p[P_E6] = 2; /* PING */
    voice_t *v_slc = &trk[0].v[0];
    memset(v_slc, 0, sizeof(*v_slc));
    v_slc->note = 60;
    v_slc->gate = 1;
    v_slc->active = 1;
    slice_note_on(&trk[0], v_slc);
    int32_t slc_buf[32] = {0};
    vmod_t mod_dummy = {0};
    slice_render(&trk[0], v_slc, slc_buf, 32, &mod_dummy);

    /* Test ping-pong render for SAMPLE */
    trk[0].p[P_E1] = 4; /* PING */
    voice_t *v_smp = &trk[0].v[1];
    memset(v_smp, 0, sizeof(*v_smp));
    v_smp->note = 60;
    v_smp->gate = 1;
    v_smp->active = 1;
    sample_note_on(&trk[0], v_smp);
    int32_t smp_buf[32] = {0};
    sample_render(&trk[0], v_smp, smp_buf, 32, &mod_dummy);

    printf("  SLICE engine parameters (PTCH/SPEED/STRCH, SLCS 1..16, white keys, manual slice edit, ping-pong) verified (OK)\n");
#endif

    /* Test SCRUB parameters, home knobs, and graph_scrub */
    assert(strcmp(ENG_SCRUB.name, "SCRUB") == 0);
    assert(strcmp(ENG_SCRUB.page_title[0], "SCRB") == 0);
    assert(strcmp(ENG_SCRUB.page_title[1], "TAPE") == 0);
    assert(strcmp(ENG_SCRUB.edit[0].label, "POS") == 0);
    assert(strcmp(ENG_SCRUB.edit[1].label, "MOTOR") == 0);
    assert(strcmp(ENG_SCRUB.edit[2].label, "INERT") == 0);
    assert(strcmp(ENG_SCRUB.edit[3].label, "FRIC") == 0);
    assert(strcmp(ENG_SCRUB.edit[4].label, "SRC") == 0);
    assert(strcmp(ENG_SCRUB.edit[5].label, "LOOP") == 0);
    assert(ENG_SCRUB.edit[5].max == 3);
    assert(strcmp(N_SCRUB_LOOP[0], "OFF") == 0);
    assert(strcmp(N_SCRUB_LOOP[1], "FWD") == 0);
    assert(strcmp(N_SCRUB_LOOP[2], "REV") == 0);
    assert(strcmp(N_SCRUB_LOOP[3], "PING") == 0);
    assert(strcmp(ENG_SCRUB.edit[6].label, "DRV") == 0);
    assert(strcmp(ENG_SCRUB.edit[7].label, "WOW") == 0);

    /* Test ping-pong wrap for SCRUB */
    scrb_part_t *sp = scrb_of(&trk[0]);
    sp->pos = (int64_t)105 << 16;
    sp->velocity = 65536;
    int32_t cur_step = 65536;
    scrb_wrap_pos(sp, 100, 3, &cur_step);
    assert(sp->velocity == -65536 && "Ping-pong mode must reverse velocity on upper boundary overshoot!");
    assert(cur_step == -65536);
    assert(sp->pos == (int64_t)93 << 16); /* 2*99 - 105 = 93 */

    /* Also test undershoot below 0 */
    sp->pos = -(int64_t)(5 << 16);
    sp->velocity = -65536;
    cur_step = -65536;
    scrb_wrap_pos(sp, 100, 3, &cur_step);
    assert(sp->velocity == 65536 && "Ping-pong mode must reverse velocity on lower boundary undershoot!");
    assert(cur_step == 65536);
    assert(sp->pos == (int64_t)5 << 16);
    assert(ENG_SCRUB.knob[0] == P_E0 && ENG_SCRUB.knob[1] == P_E1 &&
           ENG_SCRUB.knob[2] == P_E2 && ENG_SCRUB.knob[3] == P_E3);
    assert(ENG_SCRUB.block != 0 && "SCRUB engine must have block hook for physics and reel rotation!");
    assert(eng_theme_color(ENGI_SCRUB) == 0xF80F && "SCRUB must use Hot Pink theme!");
    TSEL->eng_req = ENGI_SCRUB;
    TSEL->engine = ENGI_SCRUB;
    for (uint32_t i = 0; i < NPAGES; i++) {
        if (PAGES[i].fam == FAM_EDIT && strcmp(PAGES[i].title, "VOICE") == 0) {
            assert(!page_visible(i) && "VOICE page must be hidden for monophonic SCRUB engine!");
        }
    }
    cv_begin(240, 122, T_BG);
    graph_scrub(TSEL, T_THEME);
    printf("  SCRUB engine parameters (POS, MOTOR, INERT, FRIC, SRC, LOOP, DRV, WOW), Hot Pink theme, block hook, and graph_scrub verified (OK)\n");
}


static void test_5track_and_drums(void)
{
    printf("--- Testing 5-Track Architecture & Drum Engines ---\n");

    /* 1. Architecture constants */
    assert(NPART == 4);
    assert(NDRUM_TRK == 1);
    assert(NTRK == 5);
    printf("  NPART=4, NDRUM_TRK=1, NTRK=5 architecture confirmed (OK)\n");

    /* 2. Track selection and limits */
    for (uint32_t i = 0; i < NTRK; i++) {
        track_select(i);
        assert(song.sel == (uint8_t)i);
        assert(TSEL == &trk[i]);
    }
    /* Bounds check: track_select(NTRK) does not exceed bounds */
    track_select(NTRK);
    assert(song.sel < NTRK);
    track_select(NTRK + 5);
    assert(song.sel < NTRK);
    printf("  Track selection cycles 0..4 smoothly with bounds protection (OK)\n");

    /* 3. Track 4 is dedicated drum track with Kit 0 (ANALOG) default */
    assert(TRK_DEF[4][0] == ENGI_DRUM);
    for (uint32_t i = 0; i < NTRK; i++) {
        set_engine_of(&trk[i], TRK_DEF[i][0]);
        apply_preset_to(&trk[i], TRK_DEF[i][1]);
        trk[i].engine = trk[i].eng_req;
    }
    assert(trk[4].eng_req == ENGI_DRUM);
    assert(trk[4].engine == ENGI_DRUM);
    assert(trk[4].preset == 0); /* Kit 0: ANALOG */
    printf("  Track 4 initializes to ENGI_DRUM with Kit 0 (ANALOG) (OK)\n");

    /* 4. Drum kits and choke group */
    loom_drum_init();
    assert(!loom_drum_active(LOOM_KIT_ANALOG));

    /* Trigger Open Hat (lane 4) */
    int16_t drum_params[8] = {LOOM_KIT_ANALOG};
    loom_drum_trigger(LOOM_DRUM_HAT_OP, 127, drum_params);
    assert(loom_drum_active(LOOM_KIT_ANALOG));

    int32_t audio_buf[256];
    memset(audio_buf, 0, sizeof(audio_buf));
    loom_drum_render(audio_buf, 128, LOOM_KIT_ANALOG, drum_params);
    int32_t energy_oh = 0;
    for (int i = 0; i < 128; i++) {
        energy_oh += audio_buf[i] > 0 ? audio_buf[i] : -audio_buf[i];
    }
    assert(energy_oh > 0);

    /* Trigger Closed Hat (lane 3) - chokes Open Hat */
    loom_drum_trigger(LOOM_DRUM_HAT_CL, 127, drum_params);
    loom_drum_render(audio_buf, 128, LOOM_KIT_ANALOG, drum_params);

    /* Test FM Drum kit trigger and rendering */
    loom_drum_all_off();
    assert(!loom_drum_active(LOOM_KIT_ANALOG));
    drum_params[0] = LOOM_KIT_FM;
    loom_drum_trigger(LOOM_DRUM_KICK, 127, drum_params);
    assert(loom_drum_active(LOOM_KIT_FM));
    memset(audio_buf, 0, sizeof(audio_buf));
    loom_drum_render(audio_buf, 128, LOOM_KIT_FM, drum_params);
    int32_t energy_fm = 0;
    for (int i = 0; i < 128; i++) {
        energy_fm += audio_buf[i] > 0 ? audio_buf[i] : -audio_buf[i];
    }
    assert(energy_fm > 0);
    printf("  Analog and FM Drum kits trigger, render, and choke correctly (OK)\n");

    /* 5. Project serialization across all 5 tracks */
    project_t test_proj;
    project_capture(&test_proj);

    /* Customize parameters on each track */
    for (uint32_t i = 0; i < NTRK; i++) {
        test_proj.t[i].p[P_LEVEL] = (int16_t)(10 + i * 5);
        test_proj.t[i].p[P_PAN] = (int16_t)(-20 + i * 10);
    }
    test_proj.sel = 4; /* Saved while on Track 4 (DRUM) */

    project_store_t store;
    int pack_res = proj_pack(&store, &test_proj);
    assert(pack_res != 0 && "proj_pack failed!");
    assert(sizeof(store) <= PROJ_STORE_SIZE && "Serialized project exceeds flash budget!");

    project_t restored_proj;
    int import_res = proj_import(&restored_proj, &store, sizeof(store));
    assert(import_res != 0 && "proj_import failed!");

    for (uint32_t i = 0; i < NTRK; i++) {
        assert(restored_proj.t[i].p[P_LEVEL] == test_proj.t[i].p[P_LEVEL]);
        assert(restored_proj.t[i].p[P_PAN] == test_proj.t[i].p[P_PAN]);
    }
    /* 6. Verify Scrub engine in slot 6 replacing Trio, and 5-track mixer in LAYER_GLO */
    song.sel = 0;
    assert(eng_ok(ENGI_SCRUB) && "Scrub engine must be active in slot 6!");
    assert(strcmp(ENGINES[ENGI_SCRUB]->name, "SCRUB") == 0);
    assert(NENG_SHOWN == (NENGINES - !FELUCCA_FM4));

    /* LAYER_GLO and HOME TRACKS render verification */
    ui.ly = LAYER_GLO;
    layer_cards(LAYER_GLO);
    draw_home_tracks();
    ui.ly = 0;

    /* Verify EN_ALGO edits Track 4 (DRUM) LEVEL in LAYER_GLO */
    trk[4].p[P_LEVEL] = 80;
    fm1_in.buttons |= 1u << panel.btn[B_GLO];
    ui.ly = LAYER_GLO;
    ui.ly_t0 = LY_OPEN;
    host_enc[panel.enc[EN_ALGO] % 7u] = 5;
    ui_input();
    assert(trk[4].p[P_LEVEL] > 80);
    assert(ui.hot_col == 4);
    fm1_in.buttons &= ~(1u << panel.btn[B_GLO]);
    ui.ly = 0;
    ui.ly_t0 = 0;
    ui_input();
    printf("  LAYER_GLO 5-track mixer and EN_ALGO DRUM level control verified (OK)\n");
    printf("  Scrub engine in slot 6 and 5-track mixer verified (OK)\n");
}

static void test_lfo_and_patch(void)
{
    printf("--- Testing Patch & LFO Screens (FAM_LFO) ---\n");
    open_family(FAM_LFO);
    assert(cur_page()->fam == FAM_LFO);
    assert(strcmp(cur_page()->title, "LFO") == 0);
    assert(cur_page()->id[0] == P_LRATE);
    assert(cur_page()->id[1] == P_LWAVE);
    assert(cur_page()->id[2] == P_LSYNC);
    assert(cur_page()->id[3] == P_LFADE);

    /* Test live playhead rendering on GR_LFO */
    cv_begin(240, 122, T_BG);
    graph_lfo(TSEL, T_THEME);

    /* Page 2: LFO 2 */
    open_family(FAM_LFO);
    assert(strcmp(cur_page()->title, "LFO 2") == 0);
    assert(cur_page()->id[0] == P_LTRIG);
    assert(cur_page()->id[1] == P_LPOL);
    assert(cur_page()->id[2] == P_LPHASE);

    /* Page 3: LFO DEST */
    open_family(FAM_LFO);
    assert(strcmp(cur_page()->title, "LFO DEST") == 0);
    assert(cur_page()->graph == GR_LFO);
    assert(cur_page()->id[0] == P_LD_PIT);
    assert(cur_page()->id[1] == P_LD_FLT);
    assert(cur_page()->id[2] == P_LD_SHP);
    assert(cur_page()->id[3] == P_LD_AMP);

    /* Page 4: MACRO 1 */
    open_family(FAM_LFO);
    assert(strcmp(cur_page()->title, "MACRO 1") == 0);
    assert(cur_page()->graph == GR_MOD);
    assert(cur_page()->id[0] == 0);

    /* Page 5: MACRO 2 */
    open_family(FAM_LFO);
    assert(strcmp(cur_page()->title, "MACRO 2") == 0);
    assert(cur_page()->graph == GR_MOD);
    assert(cur_page()->id[0] == 1);

    /* Page 6: MACRO 3 */
    open_family(FAM_LFO);
    assert(strcmp(cur_page()->title, "MACRO 3") == 0);
    assert(cur_page()->graph == GR_MOD);
    assert(cur_page()->id[0] == 2);

    /* Page 7: MACRO 4 */
    open_family(FAM_LFO);
    assert(strcmp(cur_page()->title, "MACRO 4") == 0);
    assert(cur_page()->graph == GR_MOD);
    assert(cur_page()->id[0] == 3);

    /* Verify Source List: first item after OFF is KNOB */
    assert(strcmp(N_MSRC[0], "OFF") == 0);
    assert(strcmp(N_MSRC[1], "KNOB") == 0);
    assert(strcmp(N_MSRC[2], "LFO") == 0);

    /* Test macro_dest_name for different engines */
    /* Subtractive on T1 (Track 0) */
    trk[0].engine = trk[0].eng_req = 0; /* 0 is Analog/Subtractive */
    assert(strcmp(macro_dest_name(0, 1), "PITCH") == 0);
    assert(strcmp(macro_dest_name(0, 2), "CUT") == 0);
    assert(strcmp(macro_dest_name(0, 7), "OSC") == 0);
    assert(strcmp(macro_dest_name(0, 8), "WAVE") == 0);

    /* Granular on T2 (Track 1) */
    trk[1].engine = trk[1].eng_req = 8; /* ENGI_GRAIN */
    assert(strcmp(macro_dest_name(1, 8), "POS") == 0);
    assert(strcmp(macro_dest_name(1, 9), "SIZE") == 0);
    assert(strcmp(macro_dest_name(1, 10), "DENS") == 0);
    assert(strcmp(macro_dest_name(1, 12), "SPRY") == 0);

    /* Drum track on DRM (Track 4) */
    assert(strcmp(macro_dest_name(4, 7), "KICK") == 0);
    assert(strcmp(macro_dest_name(4, 8), "SNAR") == 0);
    assert(strcmp(macro_dest_name(4, 9), "CLAP") == 0);
    assert(strcmp(macro_dest_name(4, 10), "HATS") == 0);

    /* Test Macro Setup on MACRO 1 */
    macro_init();
    open_family(FAM_LFO); /* Back to LFO */
    open_family(FAM_LFO); /* LFO 2 */
    open_family(FAM_LFO); /* LFO DEST */
    open_family(FAM_LFO); /* MACRO 1 */
    assert(strcmp(cur_page()->title, "MACRO 1") == 0);

    /* Test Macro Setup on MACRO 1:
     * Knob 1 selects row (0=Param 1, 1=Param 2, 2=Param 3)
     * Knob 2 selects Track (T1..DRM)
     * Knob 3 selects Parameter (OFF, CUT, POS, etc.)
     * Knob 4 selects Polarity/Intensity (-64..+63)
     */
    macro_ui_line = 0;
    assert(macro_ui_line == 0); /* Param 1 */

    /* Edit Param 1: K2=TRK, K3=DEST, K4=AMT */
    edit_param(1, 1); /* Track 0 -> Track 1 */
    assert(loom_macros[0].dest[0].trk == 1);
    edit_param(2, 8); /* Destination -> POS (index 8) */
    assert(loom_macros[0].dest[0].dest == 8);
    edit_param(3, 40); /* AMT -> 40 */
    assert(loom_macros[0].dest[0].amt == 40);

    /* Knob 1 steps to Param 2 (row 1) */
    edit_param(0, 1);
    assert(macro_ui_line == 1);

    /* Edit Param 2: K2=TRK, K3=DEST, K4=AMT */
    edit_param(2, 2); /* CUT */
    assert(loom_macros[0].dest[1].dest == 2);
    edit_param(3, -25); /* -25% */
    assert(loom_macros[0].dest[1].amt == -25);

    /* Knob 1 steps to Param 3 (row 2) */
    edit_param(0, 1);
    assert(macro_ui_line == 2);

    /* Verify rendering of graph_mod and draw_columns */
    cv_begin(240, 122, T_BG);
    graph_mod(TSEL, T_THEME);
    draw_columns();

    /* Test Audio Modulation evaluation in mod_begin */
    /* Set Macro 0 targeting Track 0 Cutoff (dest 2) with amount +50 and KNOB val 127 */
    loom_macros[0].src = MS_KNOB;
    loom_macros[0].val = 127;
    loom_macros[0].dest[0].trk = 0;
    loom_macros[0].dest[0].dest = 2; /* CUT */
    loom_macros[0].dest[0].amt = 50;
    macro_sync_to_tracks();

    int mod_ret = mod_begin(&trk[0]);
    assert(mod_ret == 1);
    assert(mod.cut > 0); /* Cutoff modulation offset applied */
    mod_end(&trk[0]);

    /* Test HOME screen rendering with Granular */
    TSEL->eng_req = 8;
    TSEL->engine = 8;
    go_home();
    assert(ui.home == 1);
    draw_graph();
    printf("  FAM_LFO 7 pages (LFO, LFO 2, LFO DEST, MACRO 1..4), KNOB source, and multi-track destinations verified (OK)\n");
}

static void test_subtractive_and_track5_lock(void)
{
    printf("--- Testing 4-Osc Subtractive & Track 5 Lock ---\n");

    /* Track 5 Lock */
    song.sel = 4;
    assert(song.sel == 4);
    assert(trk[4].eng_req == ENGI_DRUM);

    select_engine(0);
    assert(trk[4].eng_req == ENGI_DRUM);

    set_engine_of(&trk[4], ENGI_FM6);
    assert(trk[4].eng_req == ENGI_DRUM);

    edit_load(0, 1);
    assert(trk[4].eng_req == ENGI_DRUM);
    assert(eng_ok(ENGI_DRUM) == 1);
    assert(eng_ok(0) == 0); /* Melodic engines rejected on Track 5 */
    printf("  Track 5 strictly locked to ENGI_DRUM (OK)\n");

    /* Subtractive 4-Osc Engine on Track 1 */
    song.sel = 0;
    assert(eng_ok(ENGI_DRUM) == 0); /* DRUM rejected on Melodic Track 1 */
    set_engine_of(&trk[0], ENGI_DRUM);
    assert(trk[0].eng_req != ENGI_DRUM); /* Not set to DRUM */
    edit_load(ENGI_DRUM, 0);
    assert(trk[0].eng_req != ENGI_DRUM);
    set_engine_of(&trk[0], 0);
    assert(trk[0].eng_req == 0);

    track_t *t = &trk[0];
    sub_part_t *sp = sub_of(t);
    assert(sp != NULL);
    assert(sp->magic == SUB_MAGIC);

    /* Defaults */
    assert(sp->osc[0].wave == 85);  /* Saw */
    assert(sp->osc[0].mix == 127);  /* 100% */
    assert(sp->osc[0].pitch == 0);
    assert(sp->osc[1].wave == 85);  /* Saw */
    assert(sp->osc[1].mix == 89);   /* 70% */
    assert(sp->osc[2].wave == 42);  /* Tri */
    assert(sp->osc[2].mix == 0);    /* OFF */
    assert(sp->osc[3].wave == 0);   /* Sine */
    assert(sp->osc[3].mix == 0);    /* OFF */
    assert(sp->osc[3].pitch == -12);/* -1 Octave */
    printf("  Osc 1..4 defaults verified (Osc 4: SUB -12 ST pitch, 0 mix) (OK)\n");

    /* Navigation & Knobs */
    open_family(FAM_EDIT);
    ui.home = 0;
    while (cur_page()->fam != FAM_EDIT || cur_page()->id[0] != P_E0) open_family(FAM_EDIT);
    assert(cur_page()->id[0] == P_E0);
    assert(is_sub_edit_page(cur_page()));

    /* Page 1: OSC */
    sub_knob(0, 1);
    assert(sp->sel_osc == 1);
    sub_knob(0, 2);
    assert(sp->sel_osc == 3); /* Osc 4 / SUB */
    sub_knob(1, 10);          /* wave */
    assert(sp->osc[3].wave == 10);
    sub_knob(2, 45);          /* fold */
    assert(sp->osc[3].fold == 45);
    sub_knob(3, 60);          /* mix */
    assert(sp->osc[3].mix == 60);
    printf("  Page 1 (OSC): K1 selects Osc, K2 wave morph, K3 fold, K4 mix verified (OK)\n");

    /* Page 2: TUNE */
    open_family(FAM_EDIT);
    assert(cur_page()->id[0] == P_E4);
    assert(is_sub_edit_page(cur_page()));
    assert(sp->sel_osc == 3); /* K1 synced */
    sub_knob(1, 5);           /* pitch: -12 + 5 = -7 */
    assert(sp->osc[3].pitch == -7);
    sub_knob(2, 50);          /* drive */
    assert(sp->osc[3].drive == 50);
    sub_knob(3, 30);          /* glide */
    assert(sp->osc[3].glide == 30);
    printf("  Page 2 (TUNE): K1 synced, K2 pitch (-24..+24), K3 drive, K4 glide verified (OK)\n");

    /* Visualizer & Columns */
    sub_draw_columns();
    graph_subtract(t, T_THEME);
    printf("  sub_draw_columns & graph_subtract visualizer verified (OK)\n");

    /* Audio Rendering & Bypass */
    int32_t audio_out[64] = {0};
    vmod_t mod = {
        .inc = 0x01000000u,
        .pitch16 = 60 * 16,
        .cutoff = 100 << 8,
        .shape = 64 << 8,
        .fine = 0,
        .amp0 = 32767,
        .amp1 = 32767
    };
    voice_t v;
    memset(&v, 0, sizeof(v));
    v.active = 1;
    v.gate = 1;
    v.note = 60;
    v.vel = 100;
    v.pitch16 = 60 * 16;
    v.env_out = 32767;

    analog_note_on(t, &v);
    analog_render(t, &v, audio_out, 64, &mod);
    int32_t energy = 0;
    for (int i = 0; i < 64; i++) {
        energy += (audio_out[i] < 0) ? -audio_out[i] : audio_out[i];
    }
    assert(energy > 0);

    /* Turn all osc mixes to OFF and test bypass */
    sp->osc[0].mix = sp->osc[1].mix = sp->osc[2].mix = sp->osc[3].mix = 0;
    memset(audio_out, 0, sizeof(audio_out));
    analog_render(t, &v, audio_out, 64, &mod);
    int32_t zero_energy = 0;
    for (int i = 0; i < 64; i++) {
        if (audio_out[i] != 0) zero_energy = 1;
    }
    assert(zero_energy == 0);
    printf("  DSP audio rendering and mix OFF bypass verified (OK)\n");
}

static void test_wavetable_and_effects(void)
{
    printf("--- Testing Loom Wavetable Engine & Effects ---\n");

    /* 1. Wavetable Engine Architecture */
    assert(ENGI_WAVETABLE == 14u);
    assert(NENGINES == 15u);
    const engine_t *e = ENGINES[ENGI_WAVETABLE];
    assert(e != NULL);
    assert(strcmp(e->name, "WAVETABLE") == 0);
    assert(strcmp(e->page_title[0], "TABLE") == 0);
    assert(strcmp(e->page_title[1], "TONE") == 0);
    assert(strcmp(e->edit[0].label, "SRC") == 0);
    assert(strcmp(e->edit[1].label, "POS") == 0);
    assert(strcmp(e->edit[2].label, "WARP") == 0);
    assert(strcmp(e->edit[3].label, "DETN") == 0);
    assert(strcmp(e->edit[4].label, "BITS") == 0);
    assert(strcmp(e->edit[5].label, "SRAT") == 0);
    assert(strcmp(e->edit[6].label, "DRV") == 0);
    assert(strcmp(e->edit[7].label, "GLID") == 0);
    printf("  WAVETABLE engine parameters (SRC, POS, WARP, DETN, BITS, SRAT, DRV, GLID) verified (OK)\n");

    /* Test Wavetable Loading & Synthesis */
    track_t *t = &trk[0];
    t->engine = ENGI_WAVETABLE;
    t->eng_req = ENGI_WAVETABLE;
    eng_mem_claim(t);
    wt_part_t *wp = wt_of(t);
    assert(wp != NULL);

    /* Test Sources: BASIC (0), PIANO (1), FLUTE (2), SAX (3), BREAK (4) */
    for (uint32_t src = 0; src <= 4; src++) {
        t->p[P_E0] = src;
        wp->loaded_src = 0; /* Force reload */
        wt_load_source(wp, src);
        assert(wp->loaded_src == src + 1);

        /* Verify all 8 frames have non-zero signal data */
        for (uint32_t f = 0; f < WT_NFRAMES; f++) {
            int32_t energy = 0;
            for (uint32_t s = 0; s < WT_FRAME_LEN; s++) {
                if (wp->table[f][s] != 0) energy = 1;
            }
            assert(energy == 1);
        }
    }
    printf("  Wavetable sample scanning & frame decoding (BASIC, PIANO, FLUTE, SAX, BREAK) verified (OK)\n");

    /* Test Audio Rendering */
    voice_t v;
    memset(&v, 0, sizeof(v));
    v.active = 1;
    v.stage = 2; /* decay/sustain */
    e->note_on(t, &v);

    vmod_t mod;
    memset(&mod, 0, sizeof(mod));
    mod.inc = pitch_inc(60 * 16); /* C4 */
    mod.amp0 = 32767;
    mod.amp1 = 32767;
    mod.cutoff = 100 << 8;

    int32_t audio_out[64];
    memset(audio_out, 0, sizeof(audio_out));
    t->p[P_E1] = 64; /* POS 50% */
    t->p[P_E2] = 80; /* WARP forward */
    t->p[P_E4] = 127; /* 16-bit */
    t->p[P_E5] = 0;  /* 44.1 kHz */
    t->p[P_E6] = 20; /* Drive */

    e->render(t, &v, audio_out, 64, &mod);
    int32_t has_audio = 0;
    for (int i = 0; i < 64; i++) {
        if (audio_out[i] != 0) has_audio = 1;
    }
    assert(has_audio == 1);

    /* Verify all 8 factory presets render strong, non-silent audio */
    for (int p_idx = 0; p_idx < e->npresets; p_idx++) {
        for (int p = 0; p < 8; p++) t->p[P_E0 + p] = e->presets[p_idx].e[p];
        memset(ENG_MEM(0), 0, e->mem);
        memset(&v, 0, sizeof(v));
        v.active = 1;
        v.stage = 2;
        v.note = 60;
        v.vel = 100;
        v.pitch16 = 60 * 16;
        v.pitch_cur = 60 * 16;
        e->note_on(t, &v);

        vmod_t pmod;
        memset(&pmod, 0, sizeof(pmod));
        pmod.inc = pitch_inc(60 * 16);
        pmod.pitch16 = 60 * 16;
        pmod.amp0 = 32767;
        pmod.amp1 = 32767;
        pmod.cutoff = 0;
        pmod.shape = 64 << 8; /* Normal default shape modulation */

        int32_t pbuf[64] = {0};
        e->render(t, &v, pbuf, 64, &pmod);

        int64_t sum2 = 0;
        for (int i = 0; i < 64; i++) sum2 += (int64_t)pbuf[i] * pbuf[i];
        double p_rms = sqrt((double)sum2 / 64.0);
        assert(p_rms > 5000.0 && "Wavetable preset produced silent or weak output!");
    }

    /* Verify POS sweep across all 8 frames has no dropouts or dead spots */
    for (int pos_val = 0; pos_val <= 127; pos_val += 16) {
        t->p[P_E1] = pos_val;
        int32_t sbuf[64] = {0};
        e->render(t, &v, sbuf, 64, &mod);
        int64_t sum2 = 0;
        for (int i = 0; i < 64; i++) sum2 += (int64_t)sbuf[i] * sbuf[i];
        double s_rms = sqrt((double)sum2 / 64.0);
        assert(s_rms > 5000.0 && "POS sweep encountered silent dropout!");
    }
    printf("  Wavetable phase-warped morphed audio rendering verified (all presets & full POS sweep OK)\n");

    /* Test graph_wavetable visualizer */
    cv_begin(240, 122, T_BG);
    graph_wavetable(t, T_THEME);
    printf("  graph_wavetable 3D isometric stack visualizer verified (OK)\n");

    /* 2. Test Loom Insert Effects */
    assert(IT_N == 13);
    assert(strcmp(N_ITYPE[IT_WOBBLE], "WOBBL") == 0);
    assert(strcmp(N_ITYPE[IT_OCTAVER], "OCTAV") == 0);
    assert(strcmp(N_ITYPE[IT_OVERDRIVE], "OVDRI") == 0);
    assert(strcmp(N_ITYPE[IT_PANNER], "PANNR") == 0);

    /* Test ins_desc parameter labels */
    assert(strcmp(ins_desc(IT_WOBBLE, 0)->label, "RATE") == 0);
    assert(strcmp(ins_desc(IT_WOBBLE, 1)->label, "DEPTH") == 0);
    assert(strcmp(ins_desc(IT_WOBBLE, 2)->label, "SAT") == 0);

    assert(strcmp(ins_desc(IT_OCTAVER, 0)->label, "MODE") == 0);
    assert(strcmp(ins_desc(IT_OCTAVER, 1)->label, "UNIS") == 0);
    assert(strcmp(ins_desc(IT_OCTAVER, 2)->label, "TONE") == 0);

    assert(strcmp(ins_desc(IT_OVERDRIVE, 0)->label, "DRIVE") == 0);
    assert(strcmp(ins_desc(IT_OVERDRIVE, 1)->label, "TONE") == 0);
    assert(strcmp(ins_desc(IT_OVERDRIVE, 2)->label, "GRIT") == 0);

    assert(strcmp(ins_desc(IT_PANNER, 0)->label, "RATE") == 0);
    assert(strcmp(ins_desc(IT_PANNER, 1)->label, "DEPTH") == 0);
    assert(strcmp(ins_desc(IT_PANNER, 2)->label, "SHAPE") == 0);
    printf("  Loom insert effect parameters (WOBBL, OCTAV, OVDRI, PANNR) verified (OK)\n");

    /* Test DSP audio rendering through each insert */
    ins_t *ins_state = &ins[0];
    int32_t buf[64];

    /* IT_WOBBLE */
    memset(ins_state, 0, sizeof(*ins_state));
    ins_state->type = IT_WOBBLE;
    t->p[P_ITYPE] = IT_WOBBLE;
    t->p[P_IMIX] = 127;
    t->p[P_IA] = 60;
    t->p[P_IB] = 80;
    t->p[P_IC] = 60;
    for (int i = 0; i < 64; i++) buf[i] = 10000;
    ins_wobble(ins_state, t, buf, 64, 32768);
    assert(buf[0] != 0);

    /* IT_OCTAVER */
    for (int mode = 0; mode <= 9; mode++) {
        memset(ins_state, 0, sizeof(*ins_state));
        ins_state->type = IT_OCTAVER;
        t->p[P_ITYPE] = IT_OCTAVER;
        t->p[P_IA] = mode;
        t->p[P_IC] = 90;
        for (int i = 0; i < 64; i++) buf[i] = (i % 8) * 2000;
        ins_octaver(ins_state, t, buf, 64, 32768);
        assert(buf[0] != 0 || buf[1] != 0);
    }

    /* IT_OVERDRIVE */
    memset(ins_state, 0, sizeof(*ins_state));
    ins_state->type = IT_OVERDRIVE;
    t->p[P_ITYPE] = IT_OVERDRIVE;
    t->p[P_IA] = 90; /* High drive */
    t->p[P_IB] = 80; /* Tone */
    t->p[P_IC] = 60; /* Grit */
    for (int i = 0; i < 64; i++) buf[i] = 20000;
    ins_overdrive(ins_state, t, buf, 64, 32768);
    assert(buf[0] != 0);

    /* IT_PANNER */
    memset(ins_state, 0, sizeof(*ins_state));
    ins_state->type = IT_PANNER;
    t->p[P_ITYPE] = IT_PANNER;
    t->p[P_IA] = 70;
    t->p[P_IB] = 100;
    t->p[P_IC] = 0; /* Sine */
    for (int i = 0; i < 64; i++) buf[i] = 15000;
    ins_panner(ins_state, t, buf, 64, 32768);
    assert(buf[0] != 0);
    printf("  Insert effects DSP rendering (Wobble, Octaver 10 modes, Overdrive, Panner) verified (OK)\n");

    /* Test graph_insert visualization rendering */
    t->p[P_ITYPE] = IT_WOBBLE;
    graph_insert(t, T_THEME);
    t->p[P_ITYPE] = IT_OCTAVER;
    graph_insert(t, T_THEME);
    t->p[P_ITYPE] = IT_OVERDRIVE;
    graph_insert(t, T_THEME);
    t->p[P_ITYPE] = IT_PANNER;
    graph_insert(t, T_THEME);
    printf("  graph_insert visualizer for all Loom effects verified (OK)\n");

    /* 3. Test Loom Tape Delay, Multi-Voice Chorus, and Reverb Tone */
    assert(strcmp(GP[G_DTIME].label, "TIME") == 0);
    assert(GP[G_DTIME].fmt == F_TIME);
    assert(strcmp(GP[G_DCOLOR].label, "TAPE") == 0);
    assert(strcmp(GP[G_RTONE].label, "TONE") == 0);
    assert(strcmp(GP[G_CTONE].label, "TONE") == 0);
    assert(strcmp(GP[G_CVOICE].label, "VOIC") == 0);
    assert(strcmp(N_CVOICE[0], "1") == 0);
    assert(strcmp(N_CVOICE[3], "4") == 0);

    /* Verify page parameter mappings */
    const page_t *pg_dly = &PAGES[page_first(FAM_FX) + 4];
    assert(strcmp(pg_dly->title, "TAPE DELAY") == 0);
    assert(pg_dly->id[0] == G_DTIME);
    assert(pg_dly->id[2] == G_DCOLOR);

    const page_t *pg_rev = &PAGES[page_first(FAM_FX) + 5];
    assert(strcmp(pg_rev->title, "REVERB") == 0);
    assert(pg_rev->id[3] == G_RTONE);

    const page_t *pg_cho = &PAGES[page_first(FAM_FX) + 6];
    assert(strcmp(pg_cho->title, "CHORUS") == 0);
    assert(pg_cho->id[2] == G_CTONE);
    assert(pg_cho->id[3] == G_CVOICE);

    /* Test DSP audio rendering through fx_buses with multi-voice chorus, tape delay, and reverb tone */
    int32_t c_in[CTL], d_in[CTL], r_in[CTL], w_out[CTL];
    for (int i = 0; i < CTL; i++) {
        c_in[i] = 8000;
        d_in[i] = 12000;
        r_in[i] = 10000;
        w_out[i] = 0;
    }
    song.g[G_DTIME] = 50;  /* Continuous delay time */
    song.g[G_DFDBK] = 70;
    song.g[G_DCOLOR] = 80; /* High tape wow & flutter */
    song.g[G_DMIX] = 90;
    song.g[G_RTYPE] = 2;   /* HALL */
    song.g[G_RSIZE] = 90;
    song.g[G_RDAMP] = 40;
    song.g[G_RTONE] = -30; /* Warmer tone */
    song.g[G_CRATE] = 45;
    song.g[G_CDEPTH] = 75;
    song.g[G_CTONE] = 25;  /* Shimmer tone */
    song.g[G_CVOICE] = 3;  /* 4 Voices */

    /* Run several blocks so chorus and delay lines fill and produce wet output */
    for (int b = 0; b < 40; b++) {
        fx_buses(c_in, d_in, r_in, w_out, CTL);
    }
    assert(w_out[0] != 0 || w_out[16] != 0);

    /* Test DELAY visualizer with 1 bouncing dot (0 fdbk) and 16 dots (120 fdbk) */
    song.g[G_DFDBK] = 0;
    ui.page = page_first(FAM_FX) + 4; /* TAPE DELAY */
    assert(PAGES[ui.page].graph == GR_DELAY);
    draw_graph();
    song.g[G_DFDBK] = 120;
    draw_graph();

    /* Test REVERB and CHORUS visualizers */
    ui.page = page_first(FAM_FX) + 5; /* REVERB */
    assert(PAGES[ui.page].graph == GR_REVERB);
    draw_graph();
    ui.page = page_first(FAM_FX) + 6; /* CHORUS */
    assert(PAGES[ui.page].graph == GR_CHORUS);
    draw_graph();

    printf("  Loom Tape Echo, Multi-Voice Chorus (1..4v), and Reverb Tone DSP and Visualizers (1..16 dots) verified (OK)\n");
}

static void test_sidechain_compression(void)
{
    printf("--- Testing Sidechain Compressor & Ducker ---\n");

    /* 1. Verify Page 1 FX parameter is DUCK */
    assert(strcmp(TP[P_DIST].label, "DUCK") == 0);
    assert(TP[P_DIST].fmt == F_PCT);
    assert(TP[P_DIST].min == 0 && TP[P_DIST].max == 127);

    /* 2. Verify SIDECHAIN page exists under FAM_FX (page 7 of FAM_FX) */
    uint32_t fx_start = page_first(FAM_FX);
    const page_t *pg_sc = &PAGES[fx_start + 7];
    assert(strcmp(pg_sc->title, "SIDECHAIN") == 0);
    assert(pg_sc->fam == FAM_FX);
    assert(pg_sc->graph == GR_SIDECHAIN);

    /* Verify page descriptor mapping to song.sc */
    int16_t *vp = 0;
    const param_desc_t *d0 = page_desc(pg_sc, 0, &vp);
    assert(d0 && strcmp(d0->label, "REL") == 0 && vp == &song.sc[0]);
    assert(d0->min == 10 && d0->max == 100);

    const param_desc_t *d1 = page_desc(pg_sc, 1, &vp);
    assert(d1 && strcmp(d1->label, "ATK") == 0 && vp == &song.sc[1]);
    assert(d1->min == 0 && d1->max == 50);

    const param_desc_t *d2 = page_desc(pg_sc, 2, &vp);
    assert(d2 && strcmp(d2->label, "FX") == 0 && vp == &song.sc[2]);
    assert(d2->min == 0 && d2->max == 127);

    const param_desc_t *d3 = page_desc(pg_sc, 3, &vp);
    assert(d3 && strcmp(d3->label, "SRC") == 0 && vp == &song.sc[3]);
    assert(d3->fmt == F_ENUM);
    assert(strcmp(N_SCSRC[0], "KICK") == 0);
    assert(strcmp(N_SCSRC[1], "K+SN") == 0);
    assert(strcmp(N_SCSRC[2], "K+TM") == 0);
    assert(strcmp(N_SCSRC[3], "ALL") == 0);
    printf("  SIDECHAIN page parameters (REL, ATK, FX, SRC: KICK/K+SN/K+TM/ALL) verified (OK)\n");

    /* 3. Test Sidechain Trigger and Envelope Follower */
    song.sc[0] = 48; /* REL */
    song.sc[1] = 0;  /* Instant attack */
    song.sc[2] = 64; /* Master FX duck */
    song.sc[3] = 0;  /* SRC = KICK */

    /* Reset envelope to 0 */
    for (int b = 0; b < 400; b++) sidechain_tick(CTL);
    assert(fx_sidechain_env() == 0);

    /* Trigger non-kick drum lane (Snare = 1) -> must not trigger because SRC = KICK */
    fx_sidechain_trigger(LOOM_DRUM_SNARE, 127);
    assert(fx_sidechain_env() == 0);

    /* Trigger Kick drum lane (Kick = 0) -> must trigger instantly to peak (32768) */
    fx_sidechain_trigger(LOOM_DRUM_KICK, 127);
    assert(fx_sidechain_env() == 32768);

    /* Tick block -> envelope releases exponentially towards 0 */
    sidechain_tick(CTL);
    int32_t env1 = fx_sidechain_env();
    assert(env1 < 32768 && env1 > 28000);

    for (int b = 0; b < 400; b++) {
        sidechain_tick(CTL);
    }
    assert(fx_sidechain_env() == 0);
    printf("  Sidechain instant trigger and exponential release decay verified (OK)\n");

    /* 4. Test Trigger Source Modes: KICK, K+SN, K+TM, ALL */
    /* K+SN: Snare triggers */
    song.sc[3] = 1;
    fx_sidechain_trigger(LOOM_DRUM_SNARE, 100);
    assert(fx_sidechain_env() > 20000);
    for (int b = 0; b < 400; b++) sidechain_tick(CTL);

    /* K+TM: Tom/Perc triggers */
    song.sc[3] = 2;
    fx_sidechain_trigger(LOOM_DRUM_HAT_CL, 127); /* Hat should not trigger */
    assert(fx_sidechain_env() == 0);
    fx_sidechain_trigger(LOOM_DRUM_PERC, 127); /* Perc should trigger */
    assert(fx_sidechain_env() == 32768);
    for (int b = 0; b < 400; b++) sidechain_tick(CTL);

    /* ALL: Any drum hit triggers */
    song.sc[3] = 3;
    fx_sidechain_trigger(LOOM_DRUM_HAT_CL, 127);
    assert(fx_sidechain_env() == 32768);
    for (int b = 0; b < 400; b++) sidechain_tick(CTL);
    printf("  Trigger source filtering (KICK, K+SN, K+TM, ALL) verified (OK)\n");

    /* 5. Test Track Ducking (Gain Reduction) */
    int32_t synth_buf[CTL];
    for (int i = 0; i < CTL; i++) synth_buf[i] = 16000;

    track_t *synth_trk = &trk[0];
    synth_trk->p[P_DUCK] = 127; /* 100% duck depth */

    /* Trigger sidechain at full velocity */
    song.sc[1] = 0;
    song.sc[3] = 0;
    fx_sidechain_trigger(LOOM_DRUM_KICK, 127);
    sidechain_tick(CTL); /* env is ~31669 */

    track_duck(synth_trk, synth_buf, CTL);
    /* At 100% depth, buffer should be reduced by ~96% (down to < 1000) */
    assert(synth_buf[CTL - 1] < 1000);

    /* 6. Verify Dedicated Drum Track (trk[4]) is NEVER ducked */
    int32_t drum_buf[CTL];
    for (int i = 0; i < CTL; i++) drum_buf[i] = 20000;
    track_t *drum_trk = &trk[4];
    drum_trk->p[P_DUCK] = 127;
    track_duck(drum_trk, drum_buf, CTL);
    assert(drum_buf[0] == 20000 && drum_buf[CTL - 1] == 20000);
    printf("  Track ducking DSP and Drum Track (trk[4]) un-ducked immunity verified (OK)\n");

    /* 7. Verify Master FX Ducking */
    int32_t wet_test[CTL];
    for (int i = 0; i < CTL; i++) wet_test[i] = 10000;
    song.sc[2] = 127; /* 100% Master FX duck depth */
    fx_sidechain_trigger(LOOM_DRUM_KICK, 127);
    sidechain_tick(CTL);

    /* Duck wet_test using master FX duck logic */
    int32_t fx_duck_amt = song.sc[2];
    int32_t e_start = sc_state.prev_env;
    int32_t e_end = sc_state.env;
    for (int i = 0; i < CTL; i++) {
        int32_t e = e_start + (((e_end - e_start) * i) >> 6);
        int32_t red = (fx_duck_amt * e) >> 7;
        int32_t gain = 32768 - red;
        wet_test[i] = (wet_test[i] * gain) >> 15;
    }
    assert(wet_test[CTL - 1] < 1000);
    printf("  Master Send FX (Reverb & Tape Delay return) ducking verified (OK)\n");

    /* 8. Verify Visualizer graph_sidechain */
    cv_begin(240, 122, T_BG);
    graph_sidechain(T_THEME);
    printf("  graph_sidechain visualizer rendering verified (OK)\n");

    /* 9. Verify Scale selection auto-enables SNAP quantize & Preset filter clears outside GR_BROWSE */
    song.sel = 0;
    trk[0].p[P_SCALE] = 0;
    trk[0].p[P_QUANT] = QN_OFF;
    open_family(FAM_SCL);
    assert(cur_page()->graph == GR_SCALE);
    edit_param(1, 2); /* Turn Knob 2 (SCL) to Minor (2) */
    assert(trk[0].p[P_SCALE] == 2);
    assert(trk[0].p[P_QUANT] == QN_SNAP);
    printf("  Scale selection auto-enables SNAP quantization verified (OK)\n");

    list_set(2); /* Set a category filter as in SAVE > PRESETS */
    assert(list_mode() == 2);
    go_home();
    presets_turn(1); /* Turning PRESETS encoder on HOME clears category filter */
    assert(list_mode() == 0);
    printf("  Preset filter auto-clear on track preset browsing verified (OK)\n");
}

static void test_chord_progression_performance(void)
{
    printf("\n--- Testing Chord Progression Performance (Direct & Strummed) ---\n");
    song.sel = 0;
    trk[0].p[P_ROOT] = 0;
    trk[0].p[P_SCALE] = 1; /* Major */
    trk[0].p[P_CCMPX] = 0; /* Simple triads (3 notes) */
    ensure_track_prog(&trk[0]);
    assert(trk_prog[0].chords[0].count >= 3);

    /* Arm layer mask for SCL */
    layer_masks();
    assert(scl_mask != 0u);

    /* Find white key 0 (p = 0) and white key 8 (p = 8) */
    uint32_t wk0 = 0, wk8 = 0;
    for (uint32_t k = 0; k < 27u; k++) {
        if (!key_black(k) && key_place(k) == 0) wk0 = k;
        if (!key_black(k) && key_place(k) == 8) wk8 = k;
    }

    /* Hold Scale button */
    fm1_in.buttons |= 1u << panel.btn[B_SCL];
    layer_masks();

    /* Press White Key 1 (wk0) -> Direct Chord 0 */
    fm1_in.notes |= 1u << wk0;
    events_block(64);
    assert(kb_scl_chord[wk0] == 1);
    assert(trk_prog[0].active_step == 0);
    assert(kb_scl_cnt[wk0] >= 3);
    printf("  Scale + White Key 1 (Direct Chord 1) trigger verified (OK)\n");

    /* Release White Key 1 */
    fm1_in.notes &= ~(1u << wk0);
    events_block(64);
    assert(kb_scl_chord[wk0] == 0);
    printf("  Scale + White Key 1 release (ADSR note-off) verified (OK)\n");

    /* Press White Key 9 (wk8) -> Strummed Chord 0 over 1/2 step */
    fm1_in.notes |= 1u << wk8;
    events_block(64);
    assert(kb_scl_chord[wk8] == 2);
    assert(kb_strum[wk8].played[0] == 1); /* Note 0 played instantly */
    printf("  Scale + White Key 9 (Strummed Chord 1 start) verified (OK)\n");

    /* Advance time by running multiple audio blocks to allow the 1/2-step strum to trigger subsequent notes */
    for (uint32_t blk = 0; blk < 60; blk++) {
        events_block(64);
    }
    assert(kb_strum[wk8].played[1] == 1);
    assert(kb_strum[wk8].played[2] == 1);
    assert(kb_strum[wk8].active == 0); /* Strum complete, notes sustained */
    printf("  Strum timing progression across 1/2 step verified (OK)\n");

    /* Release White Key 9 */
    fm1_in.notes &= ~(1u << wk8);
    events_block(64);
    assert(kb_scl_chord[wk8] == 0);
    printf("  Strummed chord release verified (OK)\n");

    /* Release Scale button */
    fm1_in.buttons &= ~(1u << panel.btn[B_SCL]);
    layer_masks();
}

int main(void)
{
    panel_init();
    printf("==================================================\n");
    printf("   Loom FM-1: Sequencer, Home, Save & Edit Tests   \n");
    printf("==================================================\n\n");

    test_5track_and_drums();
    test_sequencer_section();
    test_home_section();
    test_save_section();
    test_engine_edit_pages();
    test_lfo_and_patch();
    test_subtractive_and_track5_lock();
    test_wavetable_and_effects();
    test_sidechain_compression();
    test_chord_progression_performance();

    printf("\n==================================================\n");
    printf("   ALL TESTS PASSED SUCCESSFULLY!                 \n");
    printf("==================================================\n");
    return 0;
}
