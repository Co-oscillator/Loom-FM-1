/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI: the panel (Y_GRAPH .. Y_GRAPH + H_GRAPH) between the cards and the footer: a SURF area
 * (rounded, on BG) holding the page graphs (ADSR, LFO, steps, piano roll, drum grid, scale, FX, SLICER,
 * MOD, lists: presets, user presets, patterns, project slots, song), the HOME oscilloscope, the
 * instrument diagrams; the MIXER page draws four SURF columns instead. Each graph is redrawn only when
 * graph_signature() changes. The look:
 * curves THEME (2 px), guides and empty marks RAISE, captions MID / DIM, the active thing ACCENT,
 * bars rounded; a list's selected row is a THEME bar with INK text. */
#define PANEL_X0 10                                  /* the graphs' inner area: x 10..230 */
#define PANEL_W 220
#define GOY 11                                       /* graphs drawn on a 100 px scale sit at y 11..111 */
/* the height ADSR and LFO are drawn on: 100 px, or (MENU > LARGE's strip: draw_graph) the strip's ~ 56 */
static int32_t graph_ht = 100;
#include "ui_fm6op.c"                                /* EDIT > OPERATOR, OP ENV, OPERATOR 2: FM6's operators (their charts below) */
static inline int is_sub_edit_page(const page_t *pg)
{
    return ENGINES[TSEL->eng_req % NENGINES] == &ENG_ANALOG &&
           pg->fam == FAM_EDIT && pg->scope == SC_ENGINE;
}

static void graph_subtract(const track_t *t, uint16_t c)
{
    sub_part_t *sp = sub_of(t);
    uint32_t cur = sp->sel_osc % 4u;

    int pill_w = 46, pill_h = 16, start_x = 18, pill_y = 6;
    for (int o = 0; o < 4; o++) {
        int x = start_x + o * (pill_w + 8);
        int is_sel = (o == (int)cur);
        int is_on = (sp->osc[o].mix > 0);
        uint16_t border_col = is_sel ? c : is_on ? T_THEME : T_DIM;
        uint16_t bg_col = is_sel ? c : T_SURF;
        uint16_t txt_col = is_sel ? T_BG : is_on ? T_THEME : T_DIM;

        cv_rrect(x, pill_y, pill_w, pill_h, 3, bg_col, border_col);
        const char *name = (o == 3) ? "SUB" : (o == 0 ? "OSC 1" : o == 1 ? "OSC 2" : "OSC 3");
        int tx = x + (pill_w - (int)str_len(name) * 6) / 2;
        cv_text_on(tx, pill_y + 4, &AF_S, name, txt_col, bg_col);
    }

    int box_x = 18, box_y = 28, box_w = 204, box_h = 64;
    cv_rrect(box_x, box_y, box_w, box_h, 4, T_BG, T_SURF);

    int cy = box_y + box_h / 2;
    cv_line(box_x + 4, cy, box_x + box_w - 4, cy, T_DIM);

    sub_osc_t *osc = &sp->osc[cur];
    int py = cy;
    for (int px = 0; px < box_w - 8; px++) {
        uint32_t ph = (uint32_t)(((uint64_t)px * 0x100000000ULL) / (uint64_t)(box_w - 8));
        int32_t s = osc_morphed(ph, 0x01000000u, osc->wave);
        if (osc->fold) s = osc_fold(s, osc->fold);
        if (osc->drive) s = osc_drive(s, osc->drive);
        int y = cy - (int)(s * (box_h / 2 - 4) / 32768);
        if (y < box_y + 2) y = box_y + 2;
        if (y > box_y + box_h - 2) y = box_y + box_h - 2;
        if (px > 0) {
            cv_line(box_x + 4 + px - 1, py, box_x + 4 + px, y, osc->mix ? c : T_DIM);
        }
        py = y;
    }

    char info[32];
    if (osc->mix == 0) {
        str_cpy(info, "BYPASSED (OFF)", sizeof info);
        cv_text_on(box_x + 8, box_y + 6, &AF_S, info, T_DIM, T_BG);
    } else {
        int pt = osc->pitch;
        if (pt > 0) {
            info[0] = '+';
            fmt_int(info + 1, pt);
            int n = (int)str_len(info);
            str_cpy(info + n, " ST", sizeof info - n);
        } else if (pt < 0) {
            fmt_int(info, pt);
            int n = (int)str_len(info);
            str_cpy(info + n, " ST", sizeof info - n);
        } else {
            str_cpy(info, "0 ST", sizeof info);
        }
        cv_text_on(box_x + 8, box_y + 6, &AF_S, info, T_THEME, T_BG);
    }
}

/* Matches voice.c: attack is linear, decay and release are exponential
 * (env += (target - env) * k each tick, ~99 % after the set time). Time
 * axis is the parameter value (the times themselves are exponential). */
static void graph_adsr(const track_t *t, uint16_t c)
{
    const page_t *pg = cur_page();
    int32_t a = 4 + t->p[pg->id[0]] * 50 / 127, d = 6 + t->p[pg->id[1]] * 50 / 127, r = 6 + t->p[pg->id[3]] * 60 / 127;
    int32_t top = 6 * graph_ht / 100, bot = 88 * graph_ht / 100, sus = t->p[pg->id[2]] * 1000 / 127;   /* 0..1000 */
    int32_t x0 = 12, x1 = x0 + a, x3 = 226 - r, i, px, py;
    int32_t e = 32768;                                                  /* exp(-4.6 u), Q15 */
#define EGY(lvl) (bot - (lvl) * (bot - top) / 1000)
    cv_rect(PANEL_X0, bot + 2, PANEL_W, 1, T_RAISE);
    cv_line_t(x0, bot, x1, top, c, 2);                                  /* attack: linear */
    px = x1;
    py = top;
    for (i = 1; i <= d; i++) {                                          /* decay: exponential to SUS */
        int32_t lvl;
        e = (e * (32768 - 150733 / d)) >> 15;           /* k^d = exp(-4.6) */
        lvl = sus + ((1000 - sus) * e >> 15);
        cv_line_t(px, py, x1 + i, EGY(lvl), c, 2);
        px = x1 + i;
        py = EGY(lvl);
    }
    cv_line_t(px, py, x3, EGY(sus), c, 2);                               /* sustain */
    px = x3;
    py = EGY(sus);
    e = 32768;
    for (i = 1; i <= r; i++) {                                          /* release: exponential to 0 */
        e = (e * (32768 - 150733 / r)) >> 15;
        cv_line_t(px, py, x3 + i, EGY(sus * e >> 15), c, 2);
        px = x3 + i;
        py = EGY(sus * e >> 15);
    }
#undef EGY
}

/* ARP page: the note list the arp is walking (the held keys over OCT, as seq.c arp_list builds it), one bar
 * per note from low to high, the note sounding now filled and capped. The mode name and the position read
 * along the top. MODE OFF or nothing held: a flat rule, as the page had. */
static void graph_arp(const track_t *t, uint16_t c)
{
    uint32_t n = arp_viz_len, i;
    int32_t lo = 127, hi = 0, top = 16, floor = graph_ht - 4, gap, w;
    char buf[12];

    if (!t->p[P_AMODE] || !n || !t->nheld) {
        cv_rect(PANEL_X0, graph_ht / 2, PANEL_W, 1, T_LINE);
        cv_text_in(PANEL_X0 + 4, graph_ht / 2 - 12, PANEL_W - 8, &AF_S,
                   N_AMODE[clamp(t->p[P_AMODE], 0, AM_COUNT - 1)], T_DIM, T_BG);
        return;
    }
    for (i = 0; i < n; i++) {
        if (arp_viz_notes[i] < lo) lo = arp_viz_notes[i];
        if (arp_viz_notes[i] > hi) hi = arp_viz_notes[i];
    }
    if (hi <= lo) hi = lo + 1;
    gap = n > 24u ? 1 : 2;
    w = ((int32_t)PANEL_W - (int32_t)n * gap) / (int32_t)n;
    if (w < 1) w = 1;
    for (i = 0; i < n; i++) {
        int32_t x = PANEL_X0 + (int32_t)i * (w + gap);
        int32_t h = 6 + (int32_t)(arp_viz_notes[i] - lo) * (floor - top - 6) / (hi - lo);
        int32_t on = (i == arp_viz_pos);
        cv_rect(x, floor - h, w, h, on ? c : T_TINT);
        if (on) {
            cv_rect(x, floor - h - 3, w, 3, T_ACCENT);
            cv_rect(x, floor + 1, w, 2, T_ACCENT);
        }
    }
    cv_rect(PANEL_X0, floor + 1, PANEL_W, 1, T_LINE);
    str_cpy(buf, N_AMODE[clamp(t->p[P_AMODE], 0, AM_COUNT - 1)], sizeof buf);
    cv_text_in(PANEL_X0 + 4, 2, PANEL_W / 2 - 4, &AF_S, buf, T_MID, T_BG);
    {   /* "pos/len" and the sounding note, right aligned-ish */
        char num[8];
        unsigned k = 0, m = 0;
        fmt_int(num, (int32_t)arp_viz_pos + 1);
        while (num[k] && k < 4) buf[k] = num[k], k++;
        buf[k++] = '/';
        fmt_int(num, (int32_t)arp_viz_len);
        while (num[m] && k < 8) buf[k++] = num[m++];
        buf[k++] = ' ';
        {   /* the sounding note's name (ui.c note_name) */
            char nb[8];
            note_name(nb, arp_viz_cur);
            str_cpy(buf + k, nb, sizeof buf - k);
        }
        cv_text_in(PANEL_X0 + PANEL_W / 2, 2, PANEL_W / 2 - 4, &AF_S, buf, c, T_BG);
    }
}

static void graph_lfo(const track_t *t, uint16_t c)
{
    int32_t x, cy = graph_ht / 2, a = 38 * graph_ht / 100, py = cy, uni = t->p[P_LPOL] != 0;
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25;
    int32_t z = uni ? cy + a : cy;                   /* the zero line: POL UNI (LFO 2) draws 0..+1 over it */
    cv_rect(PANEL_X0, z, PANEL_W, 1, T_RAISE);
    for (x = 0; x < PANEL_W; x++) {                  /* two cycles (lfo_wave only reads the track) */
        int32_t w = lfo_wave((track_t *)t, ph + (uint32_t)x * (0xFFFFFFFFu / (PANEL_W / 2u))), y;
        if (t->p[P_LWAVE] == 4)
            w = (int32_t)((x / 20 * 2654435761u) >> 16) - 32768;
        y = uni ? z - (w + 32768) * a / 32768 : cy - w * a / 32768;
        if (x)
            cv_line_t(PANEL_X0 + x - 1, py, PANEL_X0 + x, y, c, 2);
        py = y;
    }

    /* Live playhead indicator dot tracking current phase */
    uint32_t ph_curr = t->lfo_ph + ph;
    int32_t live_x = (int32_t)(((uint64_t)ph_curr * (PANEL_W / 2u)) >> 32);
    if (live_x >= 0 && live_x < (int32_t)(PANEL_W / 2u)) {
        int32_t live_w = t->lfo_val;
        int32_t live_y = uni ? z - (live_w + 32768) * a / 32768 : cy - live_w * a / 32768;
        /* Draw glowing indicator pip for each of the 2 visible cycles */
        int32_t px1 = PANEL_X0 + live_x;
        int32_t px2 = px1 + (int32_t)(PANEL_W / 2u);
        cv_rect(px1 - 2, live_y - 2, 5, 5, T_ACCENT);
        cv_rect(px1 - 1, live_y - 1, 3, 3, T_TEXT);
        cv_rect(px2 - 2, live_y - 2, 5, 5, T_ACCENT);
        cv_rect(px2 - 1, live_y - 1, 3, 3, T_TEXT);
    }
}

/* step bar x of step i of a 16-step row: 4 groups, as the footer (9 px bars, 13 px apart, 4 px between groups),
 * the row centred: x 12 .. 228 */
static int32_t bar_x(uint32_t i) { return 12 + (int32_t)(i % 16u) * 13 + (int32_t)(i % 16u / 4u) * 4; }

/* STEP page (a melodic track): a piano roll of the cursor's 16-step page. PR_ROWS semitone rows of PR_RH px,
 * the highest on top; on the left the C rows named (S, DIM) and a keyboard strip (white keys RAISE, black keys a
 * shorter DIM; a key held on the track: its row ACCENT). Rows in the track's scale (SCL ROOT / SCALE; CHR: the white
 * keys) are tinted LANE, a C row closes with a RAISE line; step lines GRID, every 4th RAISE. A note is a 9 x 3 bar in
 * its column (an accent: TEXT, the row's full height; the cursor step's notes: ACCENT), chords stacked, lane hits as
 * their notes (a RATCH step's bars in its 2..4 parts, 1 px apart); a TIE carries the bars of the note before through
 * its column; a REST is empty; a slide is a TEXT diagonal from the end of its bar into the next note (not from a
 * RATCH step: it never slides out). Notes outside
 * the view: a 1 px mark on the edge. The cursor: a TEXT column frame; the playhead: a 1 px ACCENT line. The view
 * (proll.lo, the bottom row) fits the notes of the page, else centres on the cursor's note, and follows in steps (a
 * third of the way per frame); graph_signature() holds it, so a page that does not change is not drawn again. */
#define PR_ROWS 21                                  /* semitones shown (1.75 octaves) */
#define PR_RH 5                                     /* px per row */
#define PR_Y0 9                                     /* the top row (canvas y) */
#define PR_X0 35                                    /* the first step column */
#define PR_CW 12                                    /* px per step */
#define PR_KX 24                                    /* the keyboard strip, 9 px */
static struct {
    uint8_t lo, trk, init, moving;                    /* lo: the note of the bottom row; moving: on its way */
    uint32_t frame;
} proll;
static const uint8_t KEY_BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};

static int32_t pr_note(const step_t *st, uint32_t j)   /* the step's note j (0..3), then its lane hits (-1: none) */
{
    if (j < 4u)
        return j < st->n ? st->note[j] : -1;
    return (st->hit >> (j - 4u)) & 1u ? DRUM_LANE_NOTE[j - 4u] : -1;
}
static uint32_t pr_src(const track_t *t, uint32_t si, uint32_t len)   /* the step whose notes sound at si (NSTEP: none) */
{
    uint32_t k;
    for (k = 0; k < len && seq_steps(t)[si].time == ST_TIE; k++)
        si = (si + len - 1u) % len;
    return step_on(&seq_steps(t)[si]) ? si : NSTEP;
}
/* the rows lit by keys held on the selected track (bit r: row r from the top) */
static uint32_t pr_held(void)
{
    uint32_t k, i, m = 0;
    for (k = 0; k < 27u; k++)
        if (kb_chn[k] && kb_trk[k] == song.sel)
            for (i = 0; i < kb_chn[k]; i++) {
                int32_t r = (int32_t)proll.lo + PR_ROWS - 1 - kb_chord[k][i];
                if (r >= 0 && r < PR_ROWS)
                    m |= 1u << r;
            }
    return m;
}
/* once a frame: the view toward the page's notes (centred; once there it stays while they fit); at once on a redraw
 * forced, a gap in the frames, another track or MENU > ANIM OFF */
static void pr_follow(const track_t *t)
{
    uint32_t i, j, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    int32_t lo = 127, hi = -1, tgt, d, cur = proll.lo;
    int snap = ui.force || !proll.init || proll.trk != song.sel || ui.frame != proll.frame + 1u || (ui_prefs & PREF_ANIM_OFF);
    if (proll.init && proll.frame == ui.frame)
        return;
    for (i = 0; i < 16u && base + i < len; i++) {
        uint32_t s = pr_src(t, base + i, len);
        for (j = 0; s < NSTEP && j < 4u + NLANE; j++) {
            int32_t n = pr_note(&seq_steps(t)[s], j);
            if (n < 0)
                continue;
            lo = n < lo ? n : lo;
            hi = n > hi ? n : hi;
        }
    }
    if (hi < 0)
        lo = hi = last_note;
    if (hi - lo <= PR_ROWS - 1)                     /* all fit: centred, then kept while they fit */
        tgt = !snap && !proll.moving && lo >= cur && hi <= cur + PR_ROWS - 1 ? cur : (lo + hi + 1) / 2 - PR_ROWS / 2;
    else {                                            /* too wide: the cursor's (lowest) note in the middle */
        const step_t *st = &seq_steps(t)[ui.cursor];
        tgt = (st->n ? st->note[0] : (lo + hi) / 2) - PR_ROWS / 2;
    }
    tgt = clamp(tgt, 0, 128 - PR_ROWS);
    d = tgt - cur;
    proll.lo = (uint8_t)(snap ? tgt : cur + (d / 3 ? d / 3 : d > 0 ? 1 : d < 0 ? -1 : 0));
    proll.moving = proll.lo != tgt;
    proll.trk = (uint8_t)song.sel;
    proll.frame = ui.frame;
    proll.init = 1;
}
static int32_t pr_row_y(int32_t n) { return PR_Y0 + ((int32_t)proll.lo + PR_ROWS - 1 - n) * PR_RH; }
/* a bar w px wide in `hits` equal parts, 1 px apart (a RATCH step; 1: the bar) */
static void pr_split(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t hits)
{
    int32_t k, pw = (w - (int32_t)hits + 1) / (int32_t)hits;
    if (hits < 2u) {
        cv_rect(x, y, w, h, c);
        return;
    }
    for (k = 0; k < (int32_t)hits; k++)
        cv_rect(x + k * (pw + 1), y, pw, h, c);
}
/* the bars of step s in column x (in its RATCH parts, hits; a TIE's carried bars: 1): a 1 px edge mark for a note
 * out of view; first: row y of the step's first note */
static void pr_bars(const step_t *st, int32_t x, int32_t w, uint16_t c, int acc_all, int32_t *first, uint32_t hits)
{
    uint32_t j;
    int32_t ybot = PR_Y0 + PR_ROWS * PR_RH;
    for (j = 0; j < 4u + NLANE; j++) {
        int32_t n = pr_note(st, j), y;
        int acc;
        if (n < 0)
            continue;
        acc = acc_all || (j >= 4u && ((st->acc >> (j - 4u)) & 1u));
        y = pr_row_y(n);
        if (*first < -1000)
            *first = y;
        if (y < PR_Y0)
            pr_split(x, PR_Y0, w, 1, c, hits);
        else if (y >= ybot)
            pr_split(x, ybot - 1, w, 1, c, hits);
        else if (acc)
            pr_split(x, y, w, PR_RH, c == T_ACCENT ? c : T_TEXT, hits);
        else
            pr_split(x, y + 1, w, PR_RH - 2, c, hits);
    }
}
static void graph_roll(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u, ncol, mask = scale_mask(t), held = pr_held();
    uint64_t locks = motion_lock_steps(trk_index(t));
    int32_t r, ybot = PR_Y0 + PR_ROWS * PR_RH, gw;
    char nb[8];
    ncol = base < len ? (len - base < 16u ? len - base : 16u) : 0u;
    gw = (int32_t)ncol * PR_CW + 1;
    for (r = 0; r < PR_ROWS; r++) {                 /* rows: the lane, the key, the C names */
        int32_t n = (int32_t)proll.lo + PR_ROWS - 1 - r, y = PR_Y0 + r * PR_RH;
        uint32_t pc = (uint32_t)n % 12u;
        int in = mask == 0xFFFu ? !KEY_BLACK[pc] : (int)((mask >> ((uint32_t)(n - t->p[P_ROOT] + 120) % 12u)) & 1u);
        if (in)
            cv_rect(PR_X0, y, gw, PR_RH, T_LANE);
        if ((held >> r) & 1u)
            cv_rect(PR_KX, y, 9, PR_RH - 1, T_ACCENT);
        else
            cv_rect(PR_KX, y, KEY_BLACK[pc] ? 5 : 9, PR_RH - 1, KEY_BLACK[pc] ? T_DIM : T_RAISE);
        if (pc == 0u) {
            cv_rect(PR_X0, y + PR_RH - 1, gw, 1, T_RAISE);
            note_name(nb, (uint32_t)n);
            GFX_HOOK_ALIGN(0, y, 23, y + PR_RH, AL_V, "piano roll C label on its row");
            GFX_HOOK_ALIGN(0, y, 23, y + PR_RH, AL_R | AL_PASS, "piano roll C labels' right edge");
            {   /* its ink ends at x 23, a column before the keys (by the advance, C1's 1 ended 2 px short) */
                int32_t b[4];
                text_ink(&AF_S, nb, b);
                cv_text_on(23 - b[2], y - 5, &AF_S, nb, T_DIM, T_SURF);   /* (the widest, "C-1", inside the panel) */
            }
        }
    }
    for (i = 0; i <= ncol; i++)                       /* step lines, the beats brighter */
        cv_rect(PR_X0 + (int32_t)i * PR_CW, PR_Y0, 1, ybot - PR_Y0, i % 4u ? T_GRID : T_RAISE);
    for (i = 0; i < ncol; i++) {
        uint32_t si = base + i;
        int32_t x = PR_X0 + (int32_t)i * PR_CW;
        if (si == ui.cursor)
            cv_frame(x, PR_Y0 - 2, PR_CW + 1, ybot - PR_Y0 + 4, T_TEXT);
        if (song.playing && si == t->seq_idx)
            cv_rect(x + 6, PR_Y0, 1, ybot - PR_Y0, T_ACCENT);
        if ((locks >> si) & 1u)                       /* a parameter lock: a mark under the step */
            cv_rect(x + 3, ybot + 3, 7, 2, T_ACCENT);
        if (step_detailed(&seq_steps(t)[si]))         /* 1.5.1: CHANCE / RATCH / NUDGE set: a mark over it (DETAIL) */
            cv_rect(x + 4, 2, 5, 3, c);
    }
    for (i = 0; i < ncol; i++) {                      /* the notes */
        uint32_t si = base + i, s = pr_src(t, si, len), nx = (si + 1u) % len;
        const step_t *st, *ns = &seq_steps(t)[nx];
        int32_t x = PR_X0 + (int32_t)i * PR_CW, y = -10000;
        uint16_t col;
        if (s == NSTEP)
            continue;                                 /* a REST, empty, or a TIE after one */
        st = &seq_steps(t)[s];
        col = s == ui.cursor || si == ui.cursor ? T_ACCENT : c;
        if (s == si)
            pr_bars(st, x + 2, 9, col, (st->flags & SF_ACCENT) != 0u, &y, step_ratchet(st));
        else                                          /* a TIE: the bars on through the gap before */
            pr_bars(st, x - 1, 12, col, (st->flags & SF_ACCENT) != 0u, &y, 1u);
        if ((st->flags & SF_SLIDE) && step_ratchet(st) == 1u &&   /* (a ratcheted step never slides out) */
            ns->time == ST_NOTE && ns->n && y >= PR_Y0 && y < ybot) {
            int32_t ny = clamp(pr_row_y(ns->note[0]), PR_Y0, ybot - PR_RH);
            cv_line(x + 10, y + 2, x + 14, ny + 2, T_TEXT);   /* the slide into the next note */
        }
    }
}
/* SEQ > STEP on a DRUM track: the grid, 8 lanes x the 16 steps of the page shown. Lanes by their two-letter
 * names (BD SD CP CH OH TM RS CB; CG CL CY on the other kits). A hit is a rounded square (accented: the
 * accent; a RATCH step's: its 2..4 parts as bars), an empty step a dot (brighter on the beats and on the selected lane); the selected lane is
 * underlaid RAISE, the cursor framed in TEXT; a TEXT bar over the step playing. 1.4.1: a muted lane (the EDIT layer's
 * black keys, eng_drum.c drum_mute): its name DIM, the MUTE icon after it, its hits DIM */
static void graph_grid(const track_t *t, uint16_t c)
{
    uint32_t l, i, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u, mute = drum_mute[trk_index(t) % NPART];
    uint64_t locks = motion_lock_steps(trk_index(t));
    int32_t y0 = 6;
    for (i = 0; i < 16u && base + i < len; i++) {   /* a parameter lock: a mark over the step; CHANCE / RATCH / */
        if ((locks >> (base + i)) & 1u)               /* NUDGE set (1.5.1, DETAIL): a dot under it */
            cv_rect(41 + (int32_t)i * 12, 1, 8, 2, T_ACCENT);
        if (step_detailed(&seq_steps(t)[base + i]))
            cv_rect(43 + (int32_t)i * 12, 3, 4, 2, c);
    }
    for (l = 0; l < NLANE; l++) {
        int32_t y = y0 + 4 + (int32_t)l * 14;
        int sel = l == ui.lane, mu = (mute >> l) & 1u;
        uint16_t row = sel ? T_RAISE : T_SURF, hc = mu ? T_DIM : c, ac = mu ? T_DIM : T_ACCENT;
        if (sel) {
            GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_CELLS, "drum lane fill centred");
            cv_rrect(6, y, 228, 14, 3, T_RAISE, T_SURF);   /* (6 .. 234, as a list row; the hits 40 .. 230) */
        }
        GFX_HOOK_ALIGN(0, y + 2, 0, y + 12, AL_V, "drum lane label on its hits' line");
        cv_text_on(10, y - 1, &AF_S, drum_lane_abbr(t, l), mu ? T_DIM : sel ? T_ACCENT : T_MID, row);   /* ink rows 2 .. 11, as the hits */
        if (mu) {                                    /* (27 .. 39: between the name and the first hit) */
            GFX_HOOK_ALIGN(0, y, 0, y + 14, AL_V, "drum lane mute icon centred up/down");
            cv_icon_in(27, y, 0, 14, 12, ICON_MUTE, T_DIM, row);
        }
        for (i = 0; i < 16u && base + i < len; i++) {
            const step_t *st = &seq_steps(t)[base + i];
            uint32_t b = 1u << l;
            int32_t x = 40 + (int32_t)i * 12;
            if ((step_lanes(st) & b) && step_ratchet(st) > 1u)
                pr_split(x, y + 2, 10, 10, (step_accents(st) & b) ? ac : hc, step_ratchet(st));
            else if (step_lanes(st) & b)
                cv_rrect(x, y + 2, 10, 10, 2, (step_accents(st) & b) ? ac : hc, row);
            else
                cv_rect(x + 4, y + 6, 2, 2, i % 4u == 0u || sel ? T_MID : T_DIM);
            if (sel && base + i == ui.cursor)          /* the cursor: a 1 px frame */
                cv_frame(x - 1, y + 1, 12, 12, T_TEXT);
        }
    }
    if (song.playing && t->seq_idx < len && t->seq_idx / 16u == ui.bank)
        cv_rect(40 + (int32_t)(t->seq_idx % 16u) * 12, y0 - 1, 10, 3, T_TEXT);
}
/* SEQ > DETAIL (1.5.1, Discussion #199): the cursor's 16 steps as four lanes, the knobs' order top to bottom, each
 * named on the left (the lane of the knob just turned in the accent): CHANCE a bar as high as the chance, RATCH the
 * step's bar in its 2..4 parts (as the roll draws a ratchet), NUDGE a tick left / right of the step's middle, VEL a bar
 * as high as the velocity (an ACC step's full, TEXT). Off its default a value is bright (THEME, the cursor's ACCENT),
 * at it faint (RAISE: 100 %, x1, 0, a VEL never set); DIM where it does not play (a NUDGE with QUANTIZE ON or on a
 * ratchet, a VEL under ACC). A step that plays nothing: a dot (a value left on it DIM); past LEN nothing. The cursor
 * framed through the lanes, the step playing a line */
#define SD_X0 36                                    /* the first step column (12 px each) */
static void graph_detail(const track_t *t, uint16_t c)
{
    static const char *const NAME[4] = {"CHN", "RAT", "NDG", "VEL"};
    static const uint8_t LY[4] = {4, 38, 62, 86}, LH[4] = {28, 18, 18, 28};
    uint32_t i, l, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u, ncol;
    ncol = base < len ? (len - base < 16u ? len - base : 16u) : 0u;
    for (l = 0; l < 4u; l++) {
        int32_t yb = LY[l] + LH[l];
        cv_rect(SD_X0, yb, (int32_t)ncol * 12 + 1, 1, T_GRID);   /* the lane's floor */
        cv_text_on(8, LY[l] + LH[l] / 2 - 8, &AF_S, NAME[l], ui.hot_t && ui.hot_col == l ? T_ACCENT : T_MID, T_SURF);
    }
    for (i = 0; i < ncol; i++) {
        uint32_t si = base + i;
        const step_t *st = &seq_steps(t)[si];
        int32_t x = SD_X0 + (int32_t)i * 12;
        uint16_t hi = si == ui.cursor ? T_ACCENT : c;
        if (si == ui.cursor)
            cv_frame(x, 1, 13, 116, T_TEXT);
        if (song.playing && si == t->seq_idx)
            cv_rect(x + 6, 2, 1, 114, T_ACCENT);
        for (l = 0; l < 4u; l++) {
            uint32_t k = SD_KIND[l];
            int32_t v = ev_sval(st, k), y = LY[l], h = LH[l], set = v != ev_sdefv(k), bh;
            uint16_t col;
            if (!sd_takes(st, k) && !set) {           /* nothing plays there */
                cv_rect(x + 5, y + h - 3, 2, 2, T_DIM);
                continue;
            }
            col = !sd_takes(st, k) ? T_DIM : set ? hi : T_RAISE;
            if (k == EVK_CHANCE) {
                bh = v * h / 100;
                cv_rect(x + 2, y + h - (bh < 2 ? 2 : bh), 9, bh < 2 ? 2 : bh, col);
            } else if (k == EVK_RATCH) {
                pr_split(x + 2, y + 5, 9, 8, col, (uint32_t)v);
            } else if (k == EVK_NUDGE) {
                if (set && (t->p[P_SQNT] || !ev_nudge_plays(t, st)))
                    col = T_DIM;
                cv_rect(x + 6, y + 2, 1, h - 4, T_GRID);
                if (!set)
                    cv_rect(x + 5, y + h / 2 - 1, 3, 3, col);
                else
                    cv_rect(x + 5 + v * 5 / 8, y + 3, 3, h - 6, col);
            } else if (st->flags & SF_ACCENT) {       /* VEL: an ACC step plays 127 */
                cv_rect(x + 2, y, 9, h, sd_takes(st, k) ? T_TEXT : T_DIM);
            } else {
                bh = v * h / 127;
                cv_rect(x + 2, y + h - (bh < 2 ? 2 : bh), 9, bh < 2 ? 2 : bh, col);
            }
        }
    }
}

/* =========================================================================
 * LOOM SEQUENCER STEP DOT GRID
 * Signature 4x4 (16-step) or 8x8 (64-step) Matrix of Round Dots
 * ========================================================================= */

static void cv_circle_fill(int32_t xc, int32_t yc, int32_t r, uint16_t c)
{
    int32_t dy;
    int32_t r2 = r * r;
    for (dy = -r; dy <= r; ++dy) {
        int32_t rem = r2 - dy * dy;
        int32_t dx = 0;
        while ((dx + 1) * (dx + 1) <= rem) dx++;
        cv_rect(xc - dx, yc + dy, 2 * dx + 1, 1, c);
    }
}

static void cv_circle_ring(int32_t xc, int32_t yc, int32_t r, uint16_t c)
{
    int32_t x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        cv_rect(xc + x, yc + y, 1, 1, c);
        cv_rect(xc - x, yc + y, 1, 1, c);
        cv_rect(xc + x, yc - y, 1, 1, c);
        cv_rect(xc - x, yc - y, 1, 1, c);
        cv_rect(xc + y, yc + x, 1, 1, c);
        cv_rect(xc - y, yc + x, 1, 1, c);
        cv_rect(xc + y, yc - x, 1, 1, c);
        cv_rect(xc - y, yc - x, 1, 1, c);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

/* Strongly contrasting color for active playhead step against any theme color */
static uint16_t seq_contrast_color(uint16_t c)
{
    if (ux.mono) return (c == 0) ? 0xFFFF : 0x0000;
    uint32_t r = ((c >> 11) & 0x1Fu) * 255u / 31u;
    uint32_t g = ((c >> 5)  & 0x3Fu) * 255u / 63u;
    uint32_t b = ( c        & 0x1Fu) * 255u / 31u;

    /* Hot Pink / Magenta / Rose theme -> Electric Neon Cyan */
    if (r > 180 && b > 100 && g < 150) return 0x07FF; /* Neon Cyan (#00FFFF) */
    /* Coral / Red-Pink theme -> Electric Neon Cyan */
    if (r > 180 && g < 180) return 0x07FF;
    /* Neon Lime / Green theme -> Neon Hot Pink */
    if (g > 180 && r < 180) return 0xF80F;            /* Hot Pink (#FF007F) */
    /* Cyan / Blue theme -> Neon Hot Pink */
    if (b > 180 && r < 150) return 0xF80F;            /* Hot Pink */
    /* Amber / Yellow / Gold theme -> Electric Neon Cyan */
    if (r > 180 && g > 150 && b < 100) return 0x07FF; /* Neon Cyan */
    /* Purple / Violet theme -> Neon Lime */
    if (r > 120 && b > 180 && g < 100) return 0x3FE2; /* Neon Lime (#39FF14) */

    /* If T_ACCENT is distinct from c, use it, else Neon Cyan */
    if (T_ACCENT != c && T_ACCENT != 0) return T_ACCENT;
    return 0x07FF;
}

static void graph_loom_seq_grid(const track_t *t, uint16_t c)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    uint64_t locks = motion_lock_steps(trk_index(t));
    /* ---------------------------------------------------------
     * 4x4 Mode: 16 steps (4 columns x 4 rows) for current bank
     * Scaled up to 24px diameter circles for maximum visibility
     * --------------------------------------------------------- */
    uint32_t base = ui.bank * 16u;
    uint32_t row, col;
    int32_t r = 12;

    for (row = 0; row < 4u; ++row) {
        int32_t yc = 20 + (int32_t)row * 27;
        for (col = 0; col < 4u; ++col) {
            uint32_t step_in_bank = row * 4u + col;
            uint32_t si = base + step_in_bank;
            int32_t xc = 42 + (int32_t)col * 52;
            int in_len = (si < len);
            int is_cursor = (si == ui.cursor);
            int is_playing = (song.playing && si == t->seq_idx);
            int has_lock = (int)((locks >> si) & 1u);
            const step_t *st = &seq_steps(t)[si];

            if (!in_len) {
                cv_circle_ring(xc, yc, 4, T_DIM);
                continue;
            }

            int has_hit = 0;
            int other_hit = 0;
            int has_accent = 0;
            if (drum_track(t)) {
                uint32_t lb = 1u << ui.lane;
                has_hit = (step_lanes(st) & lb) != 0;
                other_hit = (step_lanes(st) != 0);
                has_accent = (step_accents(st) & lb) != 0;
            } else {
                has_hit = (st->time == ST_NOTE && st->n > 0);
                has_accent = ((st->flags & SF_ACCENT) != 0);
            }

            if (has_lock) {
                cv_circle_ring(xc, yc, r + 2, T_ACCENT);
            }

            uint16_t contrast_c = seq_contrast_color(c);
            if (has_accent && contrast_c == T_ACCENT)
                contrast_c = 0xFFFF; /* Pure White if accent color matches */

            if (is_playing) {
                /* Active playing step: strongly contrasting color from current theme */
                if (has_hit) {
                    cv_circle_fill(xc, yc, r, contrast_c);

                    if (has_accent) {
                        cv_circle_fill(xc, yc, 4, T_INK);
                    }

                    if (step_ratchet(st) > 1u) {
                        uint32_t ratch = step_ratchet(st);
                        if (ratch == 2u) {
                            cv_rect(xc - 4, yc - 5, 2, 11, T_INK);
                            cv_rect(xc + 2, yc - 5, 2, 11, T_INK);
                        } else if (ratch == 3u) {
                            cv_rect(xc - 5, yc - 5, 2, 11, T_INK);
                            cv_rect(xc - 1, yc - 5, 2, 11, T_INK);
                            cv_rect(xc + 3, yc - 5, 2, 11, T_INK);
                        } else {
                            cv_circle_ring(xc, yc, 6, T_INK);
                        }
                    }
                } else if (!drum_track(t) && st->time == ST_TIE) {
                    cv_circle_fill(xc, yc, r, ux_mix(contrast_c, T_SURF, 40));
                    cv_rect(xc - 6, yc - 1, 13, 2, contrast_c);
                } else if (drum_track(t) && other_hit) {
                    cv_circle_ring(xc, yc, r, contrast_c);
                    cv_circle_fill(xc, yc, 4, contrast_c);
                } else {
                    /* Active empty step: ring + center pip in contrasting color */
                    cv_circle_ring(xc, yc, r, contrast_c);
                    cv_circle_ring(xc, yc, r - 1, contrast_c);
                    cv_circle_fill(xc, yc, 4, contrast_c);
                }
                /* Outer glowing playhead indicator ring */
                cv_circle_ring(xc, yc, r + 2, contrast_c);
                cv_circle_ring(xc, yc, r + 3, contrast_c);
            } else {
                if (has_hit) {
                    uint16_t fill_c = has_accent ? T_ACCENT : c;
                    cv_circle_fill(xc, yc, r, fill_c);

                    if (has_accent) {
                        cv_circle_fill(xc, yc, 4, T_TEXT);
                    }

                    if (step_ratchet(st) > 1u) {
                        uint32_t ratch = step_ratchet(st);
                        if (ratch == 2u) {
                            cv_rect(xc - 4, yc - 5, 2, 11, T_INK);
                            cv_rect(xc + 2, yc - 5, 2, 11, T_INK);
                        } else if (ratch == 3u) {
                            cv_rect(xc - 5, yc - 5, 2, 11, T_INK);
                            cv_rect(xc - 1, yc - 5, 2, 11, T_INK);
                            cv_rect(xc + 3, yc - 5, 2, 11, T_INK);
                        } else {
                            cv_circle_ring(xc, yc, 6, T_INK);
                        }
                    }
                } else if (!drum_track(t) && st->time == ST_TIE) {
                    cv_circle_fill(xc, yc, r, ux_mix(c, T_SURF, 40));
                    cv_rect(xc - 6, yc - 1, 13, 2, T_TEXT);
                } else if (drum_track(t) && other_hit) {
                    cv_circle_ring(xc, yc, r, T_RAISE);
                    cv_circle_fill(xc, yc, 4, ux_mix(c, T_SURF, 50));
                } else {
                    cv_circle_ring(xc, yc, r, T_RAISE);
                }
            }

            if (is_cursor) {
                /* Top-left */
                cv_rect(xc - r - 3, yc - r - 3, 5, 1, T_TEXT);
                cv_rect(xc - r - 3, yc - r - 3, 1, 5, T_TEXT);
                /* Top-right */
                cv_rect(xc + r - 1, yc - r - 3, 5, 1, T_TEXT);
                cv_rect(xc + r + 3, yc - r - 3, 1, 5, T_TEXT);
                /* Bottom-left */
                cv_rect(xc - r - 3, yc + r + 3, 5, 1, T_TEXT);
                cv_rect(xc - r - 3, yc + r - 1, 1, 5, T_TEXT);
                /* Bottom-right */
                cv_rect(xc + r - 1, yc + r + 3, 5, 1, T_TEXT);
                cv_rect(xc + r + 3, yc + r - 1, 1, 5, T_TEXT);
            }
        }
    }
}

/* SEQ > 8x8 STEP / PAT: 64-step (8x8) overview grid of round dots showing
 * active note/drum hits, ratchets, locks, step length boundary, and live playhead */
static void graph_steps(const track_t *t, uint16_t c)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    uint64_t locks = motion_lock_steps(trk_index(t));
    uint32_t r, col;
    int32_t dot_r = 5;

    /* 8 rows x 8 columns of round step circles (scaled up to 10px diameter) */
    for (r = 0; r < 8u; ++r) {
        int32_t yc = 13 + (int32_t)r * 14;

        for (col = 0; col < 8u; ++col) {
            uint32_t si = r * 8u + col;
            int32_t xc = 36 + (int32_t)col * 24;
            int in_len = si < len;
            int is_cursor = (si == ui.cursor);
            int is_playing = (song.playing && si == t->seq_idx);
            int has_lock = (int)((locks >> si) & 1u);
            const step_t *st = &seq_steps(t)[si];

            if (!in_len) {
                cv_rect(xc - 1, yc - 1, 3, 3, T_DIM);
                continue;
            }

            int has_hit = 0;
            int other_hit = 0;
            int has_accent = 0;
            if (drum_track(t)) {
                uint32_t lb = 1u << ui.lane;
                has_hit = (step_lanes(st) & lb) != 0;
                other_hit = (step_lanes(st) != 0);
                has_accent = (step_accents(st) & lb) != 0;
            } else {
                has_hit = (st->time == ST_NOTE && st->n > 0);
                has_accent = ((st->flags & SF_ACCENT) != 0);
            }

            if (has_lock) {
                cv_circle_ring(xc, yc, dot_r + 2, T_ACCENT);
            }

            uint16_t contrast_c = seq_contrast_color(c);
            if (has_accent && contrast_c == T_ACCENT)
                contrast_c = 0xFFFF; /* Pure White if accent color matches */

            if (is_playing) {
                /* Active playing step: strongly contrasting color from current theme */
                if (has_hit) {
                    cv_circle_fill(xc, yc, dot_r, contrast_c);
                    if (has_accent) cv_rect(xc - 1, yc - 1, 3, 3, T_INK);
                    if (step_ratchet(st) > 1u) {
                        cv_rect(xc - 2, yc, 1, 1, T_INK);
                        cv_rect(xc + 2, yc, 1, 1, T_INK);
                    }
                } else if (!drum_track(t) && st->time == ST_TIE) {
                    cv_circle_fill(xc, yc, dot_r, ux_mix(contrast_c, T_SURF, 40));
                    cv_rect(xc - 3, yc - 1, 7, 2, contrast_c);
                } else if (drum_track(t) && other_hit) {
                    cv_circle_ring(xc, yc, dot_r, contrast_c);
                    cv_circle_fill(xc, yc, 3, contrast_c);
                } else {
                    /* Active empty step: ring + illuminated center in contrasting color */
                    cv_circle_ring(xc, yc, dot_r, contrast_c);
                    cv_rect(xc - 1, yc - 1, 3, 3, contrast_c);
                }
                /* Outer glowing playhead indicator ring */
                cv_circle_ring(xc, yc, dot_r + 2, contrast_c);
            } else {
                if (has_hit) {
                    cv_circle_fill(xc, yc, dot_r, has_accent ? T_ACCENT : c);
                    if (has_accent) cv_rect(xc - 1, yc - 1, 3, 3, T_TEXT);
                    if (step_ratchet(st) > 1u) {
                        cv_rect(xc - 2, yc, 1, 1, T_INK);
                        cv_rect(xc + 2, yc, 1, 1, T_INK);
                    }
                } else if (!drum_track(t) && st->time == ST_TIE) {
                    cv_circle_fill(xc, yc, dot_r, ux_mix(c, T_SURF, 40));
                    cv_rect(xc - 3, yc - 1, 7, 2, T_TEXT);
                } else if (drum_track(t) && other_hit) {
                    cv_circle_ring(xc, yc, dot_r, T_RAISE);
                    cv_circle_fill(xc, yc, 3, ux_mix(c, T_SURF, 50));
                } else {
                    cv_circle_ring(xc, yc, dot_r, T_RAISE);
                }
            }

            if (is_cursor) {
                cv_frame(xc - dot_r - 2, yc - dot_r - 2, 2 * dot_r + 5, 2 * dot_r + 5, T_TEXT);
            }
        }
    }
}

static void panel_note(const char *a, const char *b, const char *c);
static void panel_alert(uint32_t id, const char *a);

/* SCL: Loom scale screen with root, scale type, and 2-octave keyboard visualizer */
static void graph_scale(const track_t *t, uint16_t c)
{
    uint32_t i, mask = scale_mask(t);
    char buf[16];

    /* Header info */
    /* Root note */
    str_cpy(buf, N_NOTE[t->p[P_ROOT] % 12], sizeof(buf));
    str_cpy(buf + str_len(buf), "4", sizeof(buf) - str_len(buf));
    cv_text_on(12, 6, &AF_M, buf, T_ACCENT, T_SURF);

    /* Scale name */
    uint32_t sc_idx = clamp(t->p[P_SCALE], 0, NELEM(N_SCALE) - 1);
    cv_text_on(55, 6, &AF_M, N_SCALE[sc_idx], T_TEXT, T_SURF);

    /* Quantize & Transpose badge */
    str_cpy(buf, N_QUANT[clamp(t->p[P_QUANT], 0, 3)], sizeof(buf));
    int32_t tr = t->p[P_TRANS];
    if (tr != 0) {
        char tr_buf[8];
        tr_buf[0] = ' ';
        tr_buf[1] = tr > 0 ? '+' : '-';
        fmt_int(tr_buf + 2, tr < 0 ? -tr : tr);
        str_cpy(buf + str_len(buf), tr_buf, sizeof(buf) - str_len(buf));
    }
    cv_text_r(228, 6 + AF_M.asc - AF_S.asc, &AF_S, buf, T_MID, T_SURF);

    /* 2-Octave Keyboard (24 semitones: 14 white keys, 10 black keys) */
    static const uint8_t WHITE_SEMI[14] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23};
    static const uint8_t BLACK_POS[10] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
    static const uint8_t BLACK_SEMI[10] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};

    /* Draw white keys */
    for (i = 0; i < 14u; i++) {
        uint32_t semi = WHITE_SEMI[i];
        uint32_t deg = (semi + 24u - (uint32_t)t->p[P_ROOT]) % 12u;
        int in_scale = (mask >> deg) & 1u;
        int is_root = (deg == 0u);
        int32_t x = 9 + (int32_t)i * 16;
        uint16_t col = in_scale ? (is_root ? T_ACCENT : c) : T_RAISE;
        cv_rrect(x, 26, 15, 58, 3, col, T_SURF);
        if (in_scale) {
            cv_rrect(x + 4, 70, 7, 10, 2, is_root ? T_INK : T_SURF, col);
        }
    }

    /* Draw black keys */
    for (i = 0; i < 10u; i++) {
        uint32_t semi = BLACK_SEMI[i];
        uint32_t deg = (semi + 24u - (uint32_t)t->p[P_ROOT]) % 12u;
        int in_scale = (mask >> deg) & 1u;
        int is_root = (deg == 0u);
        int32_t wx = 9 + (int32_t)BLACK_POS[i] * 16;
        int32_t x = wx + 11;
        uint16_t col = in_scale ? (is_root ? T_ACCENT : c) : T_BG;
        cv_rrect(x, 26, 10, 36, 2, col, T_SURF);
    }
}

/* Loom 8-step Chord Progression block visualizer across 240px width */
static void graph_prog_blocks(const track_t *t, uint16_t c, int show_notes)
{
    ensure_track_prog(t);
    uint32_t k = trk_index(t);
    const loom_chord_prog_t *prog = &trk_prog[k];
    uint32_t active = prog->active_step % LOOM_CHORD_STEPS;
    char b[16];

    for (uint32_t s = 0; s < LOOM_CHORD_STEPS; s++) {
        const loom_chord_t *ch = &prog->chords[s];
        int32_t bx = 9 + (int32_t)s * 28;
        int is_act = (s == active);
        uint16_t bg = is_act ? T_ACCENT : T_RAISE;
        uint16_t ink = is_act ? T_INK : T_TEXT;
        uint16_t deg_col = is_act ? T_INK : c;

        cv_rrect(bx, 22, 26, 42, 4, bg, T_SURF);

        /* Roman Numeral Degree */
        static const char *const ROMAN_MAJ[8] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII"};
        static const char *const ROMAN_MIN[8] = {"i", "ii", "iii", "iv", "v", "vi", "vii", "viii"};
        uint32_t deg = (ch->degree > 0 && ch->degree <= 8) ? (ch->degree - 1u) : 0u;
        uint16_t mask = 0;
        for (uint8_t ci = 0; ci < ch->count; ci++) {
            mask |= (uint16_t)(1u << ((ch->notes[ci] - ch->notes[0]) % 12));
        }
        int is_min = (mask >> 3) & 1u;
        const char *rom = is_min ? ROMAN_MIN[deg] : ROMAN_MAJ[deg];
        int32_t rw = text_w(&AF_S, rom);
        cv_text_on(bx + (26 - rw) / 2, 26, &AF_S, rom, deg_col, bg);

        /* Chord root name */
        char cname[12];
        chord_name(cname, (uint32_t)ch->notes[0], mask);
        if (str_len(cname) > 4) {
            cname[4] = 0;
        }
        int32_t cw = text_w(&AF_S, cname);
        cv_text_on(bx + (26 - cw) / 2, 44, &AF_S, cname, ink, bg);
    }

    if (show_notes) {
        const loom_chord_t *act_ch = &prog->chords[active];
        int32_t nx = 12;
        cv_text_on(nx, 72, &AF_S, "VOICING:", T_MID, T_SURF);
        nx += 48;
        for (uint8_t ci = 0; ci < act_ch->count && ci < 6; ci++) {
            uint8_t note = act_ch->notes[ci];
            str_cpy(b, N_NOTE[note % 12], sizeof(b));
            char oct[4];
            fmt_int(oct, (int32_t)(note / 12) - 1);
            str_cpy(b + str_len(b), oct, sizeof(b) - str_len(b));
            cv_rrect(nx, 70, 22, 16, 3, T_RAISE, T_SURF);
            int32_t tw = text_w(&AF_S, b);
            cv_text_on(nx + (22 - tw) / 2, 72, &AF_S, b, (ci == 0 ? T_ACCENT : c), T_RAISE);
            nx += 25;
        }
    }
}

/* CHORD: Loom chord progression page with 8 blocks and voicing staves */
static void graph_chord(const track_t *t, uint16_t c)
{
    if (chord_kit(t)) {
        panel_note("NO CHORDS ON KITS", 0, 0);
        return;
    }
    if (t->p[P_CHRD] == CH_PROG) {
        ensure_track_prog(t);
        uint32_t k = trk_index(t);
        const loom_chord_prog_t *prog = &trk_prog[k];
        uint32_t active = prog->active_step % LOOM_CHORD_STEPS;
        const loom_chord_t *ch = &prog->chords[active];

        uint16_t mask = 0;
        for (uint8_t ci = 0; ci < ch->count; ci++) {
            mask |= (uint16_t)(1u << ((ch->notes[ci] - ch->notes[0]) % 12));
        }
        char b[24];
        chord_name(b, (uint32_t)ch->notes[0], mask);

        char title[32];
        str_cpy(title, "CHORD: ", sizeof(title));
        str_cpy(title + str_len(title), b, sizeof(title) - str_len(title));
        cv_text_on(12, 6, &AF_M, title, T_TEXT, T_SURF);

        /* Mood badge */
        char pill[24];
        str_cpy(pill, N_CMOOD[clamp(t->p[P_CMOOD], 0, 7)], sizeof(pill));
        str_cpy(pill + str_len(pill), " \xb7 ", sizeof(pill) - str_len(pill));
        str_cpy(pill + str_len(pill), N_CCMPX[clamp(t->p[P_CCMPX], 0, 2)], sizeof(pill) - str_len(pill));
        cv_text_r(228, 6 + AF_M.asc - AF_S.asc, &AF_S, pill, T_ACCENT, T_SURF);

        graph_prog_blocks(t, c, 1);
        return;
    }

    if (!t->p[P_CHRD]) {
        cv_text_on(12, 6, &AF_M, "CHORDS: OFF (SINGLE NOTES)", T_DIM, T_SURF);
        cv_text_r(228, 6 + AF_M.asc - AF_S.asc, &AF_S, "[K1] PROG TO ENABLE", T_MID, T_SURF);
        graph_prog_blocks(t, c, 0);
        return;
    }

    /* Standard fixed / diatonic chord */
    uint8_t nn[CHORD_MAX];
    int32_t r;
    uint16_t mask;
    char b[12];
    uint32_t n = chord_make(t, (uint32_t)(60 + t->p[P_ROOT]), nn, &r, &mask);
    chord_name(b, (uint32_t)r, mask);
    cv_text_on(14, 8, &AF_M, b, T_TEXT, T_SURF);
    cv_text_r(226, 8 + AF_M.asc - AF_S.asc, &AF_S, N_CHRD[clamp(t->p[P_CHRD], 0, 10)], T_MID, T_SURF);

    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    uint32_t lo = nn[0] - nn[0] % 12u;
    uint32_t cnt = nn[n - 1u] - lo < 24u ? 24u : 36u;
    uint32_t w = 216u / cnt;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t note = lo + i, on = 0;
        int32_t x = 13 + (int32_t)(i * w);
        for (uint32_t j = 0; j < n; j++)
            on |= nn[j] == note;
        cv_rrect(x, BLACK[note % 12u] ? 36 : 62, (int32_t)w - 2, 48, w > 6u ? 3 : 2,
                 !on ? T_RAISE : note % 12u == (uint32_t)r % 12u ? T_ACCENT : c, T_SURF);
    }
}

/* PROG: Loom progression control page with step feedback and rate display */
static void graph_prog(const track_t *t, uint16_t c)
{
    ensure_track_prog(t);
    uint32_t k = trk_index(t);
    const loom_chord_prog_t *prog = &trk_prog[k];
    uint32_t active = prog->active_step % LOOM_CHORD_STEPS;

    char head[32];
    str_cpy(head, "PROG STEP ", sizeof(head));
    char num[4];
    num[0] = (char)('1' + active);
    num[1] = '/';
    num[2] = '8';
    num[3] = 0;
    str_cpy(head + str_len(head), num, sizeof(head) - str_len(head));
    cv_text_on(12, 6, &AF_M, head, T_TEXT, T_SURF);

    char info[32];
    str_cpy(info, "RATE:", sizeof(info));
    str_cpy(info + str_len(info), N_PRATE[clamp(t->p[P_PRATE], 0, 5)], sizeof(info) - str_len(info));
    str_cpy(info + str_len(info), "  TRIG:", sizeof(info) - str_len(info));
    str_cpy(info + str_len(info), N_PHOLD[clamp(t->p[P_PHOLD], 0, 2)], sizeof(info) - str_len(info));
    cv_text_r(228, 6 + AF_M.asc - AF_S.asc, &AF_S, info, T_MID, T_SURF);

    if (t->p[P_CHRD] == CH_PROG) {
        /* Progression active: show chord blocks and active voicing notes */
        graph_prog_blocks(t, c, 1);
    } else {
        /* Progression inactive: show blocks and guidance to enable on page 2 */
        graph_prog_blocks(t, c, 0);
        cv_rrect(12, 70, 216, 20, 4, T_RAISE, T_SURF);
        cv_text_on(22, 74, &AF_S, "CHORDS OFF: SET CHRD TO PROG ON PAGE 2", T_MID, T_RAISE);
    }
}
/* the four sends as faders under their cards: a RAISE slot, the THEME fill from the bottom, a cap */
static void graph_fx(const track_t *t, uint16_t c)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t h = t->p[P_DIST + i] * 76 / 127, x = CARD_X(i) + 26;
        cv_rrect(x, 6, 4, 82, 2, T_RAISE, T_SURF);
        if (h > 3)
            cv_rrect(x, 88 - h, 4, h, 2, c, T_RAISE);
        cv_rrect(x - 4, 85 - h, 12, 6, 3, c, T_SURF);
    }
}

static inline uint32_t fx_isqrt(uint32_t n)
{
    uint32_t root = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static uint16_t fx_tone_color(uint16_t base, int32_t tone)
{
    uint32_t r = (base >> 11) & 0x1F;
    uint32_t g = (base >> 5) & 0x3F;
    uint32_t b = base & 0x1F;
    if (tone < 0) {
        int32_t num = 64 + (tone * 36) / 64;
        if (num < 18) num = 18;
        r = (r * (uint32_t)num) >> 6;
        g = (g * (uint32_t)num) >> 6;
        b = (b * (uint32_t)num) >> 6;
    } else if (tone > 0) {
        uint32_t add_r = (31u - r) * (uint32_t)tone / 64u;
        uint32_t add_g = (63u - g) * (uint32_t)tone / 64u;
        uint32_t add_b = (31u - b) * (uint32_t)tone / 64u;
        r += add_r; g += add_g; b += add_b;
    }
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* REVERB visualizer: Cave with cave size, stalactite damping, tone brightness, and 4 reverb types */
static void graph_reverb(uint16_t c)
{
    (void)c;
    int32_t rtype = clamp(song.g[G_RTYPE], 0, 3);
    int32_t size = clamp(song.g[G_RSIZE], 0, 127);
    int32_t damp = clamp(song.g[G_RDAMP], 0, 127);
    int32_t tone = clamp(song.g[G_RTONE], -64, 63);

    static const uint16_t BASE_COLS[4] = {
        0xE509, /* 0 ROOM: Amber limestone RGB(225, 160, 75) */
        0x36B7, /* 1 SPRING: Surf teal RGB(55, 215, 190) */
        0xF1C7, /* 2 HALL: Cathedral crimson red RGB(240, 55, 60) */
        0xBA9F  /* 3 SPACE: Cosmic purple RGB(190, 85, 255) */
    };
    uint16_t col = fx_tone_color(BASE_COLS[rtype], tone);
    uint16_t dim_col = fx_tone_color(BASE_COLS[rtype], -50);
    uint16_t dark_fill = fx_tone_color(BASE_COLS[rtype], -60);

    int32_t xc = 120;
    int32_t y_floor = 112;
    int32_t r_cave = 45 + (size * 60) / 127; /* 45 .. 105 */
    int32_t h_cave = 35 + (size * 62) / 127; /* 35 .. 97 */

    /* 1. Cavern interior fill and roof arch */
    int32_t prev_x = xc - r_cave, prev_y = y_floor;
    for (int32_t x = xc - r_cave; x <= xc + r_cave; x++) {
        int32_t dx = x - xc;
        int32_t num = r_cave * r_cave - dx * dx;
        if (num < 0) num = 0;
        int32_t arch_h = (int32_t)fx_isqrt((uint32_t)num) * h_cave / r_cave;
        int32_t rough = ((((x * 37) ^ 0x55) & 7) - 3) * 3 / 4;
        int32_t y_roof = y_floor - arch_h + rough;
        if (y_roof < 8) y_roof = 8;
        if (y_roof > y_floor) y_roof = y_floor;

        if (y_floor > y_roof)
            cv_line(x, y_roof, x, y_floor, dark_fill);

        if (x > xc - r_cave)
            cv_line(prev_x, prev_y, x, y_roof, col);
        prev_x = x;
        prev_y = y_roof;
    }

    /* 2. Cavern floor */
    cv_line(xc - r_cave - 8, y_floor, xc + r_cave + 8, y_floor, dim_col);
    for (int32_t rx = xc - r_cave + 10; rx <= xc + r_cave - 10; rx += 18) {
        cv_line(rx - 3, y_floor, rx, y_floor - 3, dim_col);
        cv_line(rx, y_floor - 3, rx + 3, y_floor, dim_col);
    }

    /* 3. Stalactites (Damping: more stalactites = higher damp) */
    int32_t nstals = 2 + (damp * 12) / 127; /* 2 .. 14 */
    int32_t span = (r_cave * 15) / 10;
    int32_t step = span / (nstals + 1);
    for (int32_t s = 0; s < nstals; s++) {
        int32_t sx = xc - (span / 2) + (s + 1) * step;
        int32_t dx = sx - xc;
        int32_t num = r_cave * r_cave - dx * dx;
        if (num < 0) num = 0;
        int32_t arch_h = (int32_t)fx_isqrt((uint32_t)num) * h_cave / r_cave;
        int32_t sy = y_floor - arch_h;
        if (sy < 8) sy = 8;
        int32_t slen = 7 + ((s * 7 + 13) % 15); /* 7 .. 21 px */
        cv_line(sx - 2, sy, sx, sy + slen, col);
        cv_line(sx + 2, sy, sx, sy + slen, col);
        cv_line(sx - 1, sy, sx + 1, sy, col);
        cv_pset(sx, sy + slen, col);
    }

    /* 4. Twinkling Cyan & White Crystals in SPACE mode */
    if (rtype == 3) {
        static const int8_t CX_OFF[10] = {-38, -25, -15, -6, 4, 12, 22, 32, -45, 42};
        static const int8_t CY_OFF[10] = {18, 12, 22, 14, 20, 15, 24, 18, 30, 28};
        uint16_t c_cyan = RGB(0, 240, 255);
        uint16_t c_white = RGB(255, 255, 255);
        for (int32_t k = 0; k < 10; k++) {
            int32_t kx = xc + (CX_OFF[k] * r_cave) / 60;
            int32_t ky = y_floor - h_cave + CY_OFF[k];
            if (ky >= y_floor - 2) ky = y_floor - 4;
            if (ky < 12) ky = 12;
            int32_t twinkle = (ui.frame / 4u + (uint32_t)k * 3u) & 7u;
            uint16_t crys_col = (twinkle == 0 || twinkle == 1) ? c_white :
                                (twinkle == 2 || twinkle == 3) ? c_cyan : dim_col;
            cv_pset(kx, ky, crys_col);
            cv_pset(kx - 1, ky, crys_col);
            cv_pset(kx + 1, ky, crys_col);
            cv_pset(kx, ky - 1, crys_col);
            cv_pset(kx, ky + 1, crys_col);
        }
    }

    /* 5. Type badge */
    const char *tname = N_RTYPE[rtype];
    cv_rrect(10, 8, 48, 15, 2, T_SURF, T_BG);
    cv_rect(10, 8, 48, 15, col);
    cv_text_in(10, 8 + CAP_IN(S, 15), 48, &AF_S, tname, T_INK, col);
}

/* CHORUS visualizer: Emoji singing face with stacked voice silhouettes and right-side sine wave */
static void graph_chorus(uint16_t c)
{
    (void)c;
    int32_t rate = clamp(song.g[G_CRATE], 0, 127);
    int32_t depth = clamp(song.g[G_CDEPTH], 0, 127);
    int32_t tone = clamp(song.g[G_CTONE], -64, 63);
    int32_t voices = clamp(song.g[G_CVOICE], 0, 3) + 1; /* 1..4 */

    /* Vivid color spectrum: Warm Red (-64) -> Magenta/Purple (0) -> Cool Electric Blue (+63) */
    int32_t u = clamp(tone + 64, 0, 127);
    int32_t r = 31 - (u * 25) / 127;  /* 31 down to 6 */
    int32_t b = 6 + (u * 25) / 127;   /* 6 up to 31 */
    int32_t g = (u < 64) ? (6 + (u * 10) / 64) : (16 + ((u - 64) * 20) / 63); /* 6 .. 16 .. 36 */
    uint16_t col = (uint16_t)((r << 11) | (g << 5) | b);
    uint16_t dim_col = fx_tone_color(col, -40);

    int32_t x0 = 54, y0 = 60, R = 24;

    /* 1. Stacked voice outlines behind and to the left */
    for (int32_t v = voices - 1; v >= 1; v--) {
        int32_t vx = x0 - v * 10;
        int32_t vy = y0 - v * 7;
        uint16_t vcol = fx_tone_color(col, -25 - v * 14);
        int32_t prev_vx = 0, prev_vy = 0;
        for (int32_t a = 0; a <= 360; a += 10) {
            uint32_t ph = (uint32_t)a * 0x01249249u;
            int32_t px = vx + (osc_sine(ph) * R >> 15);
            int32_t py = vy + (osc_sine(ph + 0x40000000u) * R >> 15);
            if (a > 0)
                cv_line(prev_vx, prev_vy, px, py, vcol);
            prev_vx = px;
            prev_vy = py;
        }
    }

    /* 2. Main singing face: solid circular fill to occlude background circles */
    for (int32_t dy = -R; dy <= R; dy++) {
        int32_t num = R * R - dy * dy;
        if (num < 0) continue;
        int32_t dx = (int32_t)fx_isqrt((uint32_t)num);
        cv_line(x0 - dx, y0 + dy, x0 + dx, y0 + dy, T_SURF);
    }

    /* Main face outline */
    int32_t prev_fx = 0, prev_fy = 0;
    for (int32_t a = 0; a <= 360; a += 10) {
        uint32_t ph = (uint32_t)a * 0x01249249u;
        int32_t px = x0 + (osc_sine(ph) * R >> 15);
        int32_t py = y0 + (osc_sine(ph + 0x40000000u) * R >> 15);
        if (a > 0)
            cv_line(prev_fx, prev_fy, px, py, col);
        prev_fx = px;
        prev_fy = py;
    }

    /* Singing arched eyes (^ ^) */
    int32_t ex1 = x0 - 9, ex2 = x0 + 9, ey = y0 - 7;
    cv_line(ex1 - 4, ey + 2, ex1, ey - 2, col);
    cv_line(ex1, ey - 2, ex1 + 4, ey + 2, col);
    cv_line(ex2 - 4, ey + 2, ex2, ey - 2, col);
    cv_line(ex2, ey - 2, ex2 + 4, ey + 2, col);

    /* Singing open mouth (vertical oval) */
    int32_t mx0 = x0, my0 = y0 + 7, mrx = 6, mry = 9;
    for (int32_t my = -mry; my <= mry; my++) {
        int32_t num = mry * mry - my * my;
        if (num < 0) continue;
        int32_t mw = (int32_t)fx_isqrt((uint32_t)num) * mrx / mry;
        cv_line(mx0 - mw, my0 + my, mx0 + mw, my0 + my, T_BG);
    }
    int32_t prev_mx = 0, prev_my = 0;
    for (int32_t a = 0; a <= 360; a += 15) {
        uint32_t ph = (uint32_t)a * 0x01249249u;
        int32_t px = mx0 + (osc_sine(ph) * mrx >> 15);
        int32_t py = my0 + (osc_sine(ph + 0x40000000u) * mry >> 15);
        if (a > 0)
            cv_line(prev_mx, prev_my, px, py, col);
        prev_mx = px;
        prev_my = py;
    }
    cv_line(mx0 - 3, my0 + 5, mx0 + 3, my0 + 5, dim_col);

    /* 3. Sine wave on right side: density scales visibly with RATE from 1.0 to 10.0 full cycles */
    int32_t x_start = 98, x_end = 226, wave_w = x_end - x_start;
    int32_t amp = 3 + (depth * 30) / 127;
    uint32_t lfo_inc = (1u << 25) + ((uint32_t)rate * 0x12000000u) / 127u;   /* (1u << 25): a phase step, written so it is not read as a register address */
    uint32_t ph_anim = (ui.frame * 0x02800000u);
    int32_t prev_wx = x_start, prev_wy = y0;
    for (int32_t x = x_start; x <= x_end; x++) {
        uint32_t ph = ph_anim + (uint32_t)(x - x_start) * lfo_inc;
        int32_t rel = (x - x_start);
        int32_t cur_amp = rel < 12 ? (amp * rel) / 12 : amp;
        int32_t wy = y0 + (osc_sine(ph) * cur_amp >> 15);
        if (x > x_start) {
            cv_line(prev_wx, prev_wy, x, wy, col);
            cv_line(prev_wx, prev_wy + 1, x, wy + 1, col);
        }
        prev_wx = x;
        prev_wy = wy;
    }

    /* 4. Voices badge */
    char vbuf[12];
    vbuf[0] = (char)('0' + voices);
    str_cpy(vbuf + 1, voices == 1 ? " VOICE" : " VOICES", sizeof vbuf - 1);
    cv_rrect(10, 8, 54, 15, 2, T_SURF, T_BG);
    cv_rect(10, 8, 54, 15, col);
    cv_text_in(10, 8 + CAP_IN(S, 15), 54, &AF_S, vbuf, T_INK, col);
}

/* DELAY visualizer: Bouncy trampoline line, wobble thickness/waviness, dots bouncing at delay time,
 * feedback count, and cross angle splitting into +/-45 degree lines */
static void graph_delay(uint16_t c)
{
    (void)c;
    int32_t time_val = clamp(song.g[G_DTIME], 0, 102);
    int32_t fdbk = clamp(song.g[G_DFDBK], 0, 120);
    int32_t tape = clamp(song.g[G_DCOLOR], 0, 127);
    int32_t cross = clamp(song.g[G_DMIX], 0, 127);

    uint16_t col_trampoline = RGB(255, 175, 50); /* Warm tape amber */
    uint16_t col_dot = RGB(80, 240, 120);        /* Glowing green */
    uint16_t col_core = RGB(255, 255, 255);

    int32_t xc = 120, yc = 61;
    int32_t half_len = 92 - (cross * 26) / 127; /* 92 down to 66 at 45 deg to fit canvas */
    int32_t thickness = 1 + (tape * 3) / 127;    /* 1..4 px */
    int32_t wobble_amp = (tape * 7) / 127;       /* 0..7 px */
    int32_t n_dots = 1 + (fdbk * 15) / 120;       /* 1..16 dots */

    int32_t sin_t = (cross * 23170) / 127;
    int32_t cos_t = 32768 - (cross * 9598) / 127;

    int32_t n_lines = (cross > 4) ? 2 : 1;

    for (int32_t l = 0; l < n_lines; l++) {
        int32_t s_ang = (l == 0) ? sin_t : -sin_t;
        int32_t c_ang = cos_t;

        int32_t prev_x = 0, prev_y = 0;
        for (int32_t u = -half_len; u <= half_len; u += 2) {
            int32_t wob = 0;
            if (wobble_amp > 0)
                wob = (osc_sine((uint32_t)(u * 0x01800000u) + ui.frame * 0x04000000u) * wobble_amp) >> 15;
            int32_t x = xc + ((u * c_ang - wob * s_ang) >> 15);
            int32_t y = yc + ((u * s_ang + wob * c_ang) >> 15);
            if (u > -half_len) {
                for (int32_t t = 0; t < thickness; t++)
                    cv_line(prev_x, prev_y + t, x, y + t, col_trampoline);
            } else {
                cv_rect(x - 2, y - 2, 4, 4, T_RAISE);
            }
            prev_x = x;
            prev_y = y;
        }
        cv_rect(prev_x - 2, prev_y - 2, 4, 4, T_RAISE);

        uint32_t bounce_inc = ((uint32_t)(105 - time_val) * 0x00800000u) + 0x01000000u;
        int32_t bounce_h = 24;
        for (int32_t d = 0; d < n_dots; d++) {
            int32_t u_dot = (n_dots == 1) ? 0 : (-half_len * 7 / 10 + (d * (half_len * 14 / 10)) / (n_dots - 1));
            uint32_t phase = (ui.frame * bounce_inc) + (uint32_t)((0x100000000ULL * (uint64_t)d) / (uint64_t)n_dots);
            int32_t h = (osc_sine(phase) * bounce_h) >> 15;
            if (h < 0) h = -h;

            int32_t dot_x = xc + ((u_dot * c_ang - h * s_ang) >> 15);
            int32_t dot_y = yc + ((u_dot * s_ang - h * c_ang) >> 15);

            int32_t c_x = xc + ((u_dot * c_ang) >> 15);
            int32_t c_y = yc + ((u_dot * s_ang) >> 15);
            cv_pset(c_x, c_y, T_RAISE);

            cv_rect(dot_x - 1, dot_y - 2, 3, 5, col_dot);
            cv_rect(dot_x - 2, dot_y - 1, 5, 3, col_dot);
            cv_pset(dot_x, dot_y, col_core);
        }
    }

    cv_rrect(10, 8, 48, 15, 2, T_SURF, T_BG);
    cv_rect(10, 8, 48, 15, col_trampoline);
    cv_text_in(10, 8 + CAP_IN(S, 15), 48, &AF_S, "ECHO", T_INK, col_trampoline);
}

/* SIDECHAIN visualizer: ducking envelope curve, 0 dB / floor guides, live GR meter, and trigger badge */
static void graph_sidechain(uint16_t c)
{
    cv_oy = 0;
    int32_t gh = (int32_t)graph_h();
    const int32_t x0 = PANEL_X0 + 2;     /* 12 */
    const int32_t w = PANEL_W - 32;      /* 188 */
    const int32_t y_top = 18;
    const int32_t y_bot = gh - 16;
    const int32_t h_area = y_bot > y_top ? y_bot - y_top : 30;

    int32_t rel = clamp(song.sc[0], 10, 100);
    int32_t atk = clamp(song.sc[1], 0, 50);
    uint32_t src = (uint32_t)song.sc[3] & 3u;
    int32_t live_env = fx_sidechain_env();

    /* Trigger source badge in upper left */
    char s_badge[20];
    s_badge[0] = 'S'; s_badge[1] = 'R'; s_badge[2] = 'C'; s_badge[3] = ':'; s_badge[4] = ' ';
    const char *sn = (src < 4u) ? N_SCSRC[src] : "KICK";
    uint32_t bp = 5;
    while (*sn && bp < sizeof(s_badge) - 1)
        s_badge[bp++] = *sn++;
    s_badge[bp] = 0;
    cv_rrect(x0, 4, bp * 6 + 8, 11, 2, T_RAISE, T_SURF);
    cv_text(x0 + 4, 5, &AF_S, s_badge, c);

    /* 0 dB reference line at top */
    cv_rect(x0, y_top, w, 1, T_RAISE);
    cv_text(x0 + w - 20, y_top - 9, &AF_S, "0dB", T_DIM);

    /* Floor line representing max duck depth */
    int32_t duck_dip = (h_area * 115) / 128;
    cv_rect(x0, y_top + duck_dip, w, 1, T_SURF);

    /* Draw ducking envelope shape: attack dip followed by release recovery */
    int32_t atk_w = 2 + (atk * 26) / 50;
    int32_t rel_w = 25 + (rel * (w - atk_w - 30)) / 100;
    if (rel_w > w - atk_w - 6) rel_w = w - atk_w - 6;

    int32_t px = x0;
    int32_t py = y_top;
    int32_t x;

    for (x = 0; x < w; x++) {
        int32_t y;
        if (x <= 4) {
            y = y_top;
        } else if (x <= 4 + atk_w) {
            int32_t prog = ((x - 4) << 8) / (atk_w > 0 ? atk_w : 1);
            y = y_top + ((duck_dip * prog) >> 8);
        } else if (x <= 4 + atk_w + rel_w) {
            int32_t dx = x - (4 + atk_w);
            int32_t rem = rel_w - dx;
            int32_t curve = (rem * rem) / (rel_w > 0 ? rel_w : 1);
            y = y_top + (duck_dip * curve) / (rel_w > 0 ? rel_w : 1);
        } else {
            y = y_top;
        }
        if (x > 0)
            cv_line_t(px, py, x0 + x, y, c, 2);
        px = x0 + x;
        py = y;
    }

    /* Live Gain Reduction (GR) Meter Bar on the right */
    const int32_t mx = PANEL_X0 + PANEL_W - 14;
    const int32_t mw = 8;
    /* Meter track background */
    cv_rrect(mx, y_top, mw, h_area, 2, T_RAISE, T_SURF);
    cv_text(mx - 1, y_top - 9, &AF_S, "GR", T_DIM);

    /* Live meter fill dropping down from 0 dB */
    int32_t gr_px = (live_env * h_area) >> 15;
    if (gr_px > h_area) gr_px = h_area;
    if (gr_px > 1)
        cv_rrect(mx, y_top, mw, gr_px, 2, c, T_RAISE);

    /* Reference tick marks on meter */
    cv_rect(mx - 3, y_top, 2, 1, T_DIM);
    cv_rect(mx - 3, y_top + h_area / 2, 2, 1, T_DIM);
    cv_rect(mx - 3, y_top + h_area - 1, 2, 1, T_DIM);
}
/* INSERT pages (1.5, fx.c track_insert): the drives and CRUSH as their curve, input (-1.5 .. +1.5 of a voice's full
 * scale) across, output up, dry and wet blended by MIX (fx.c ins_shape: the same shape; the dry diagonal RAISE); the
 * swept ones as two cycles of their sweep, as deep as DEPTH (FLANG the squared triangle, CHOR a sine, PHASR a
 * triangle). OFF: the dry diagonal, dim */
static void graph_insert(const track_t *t, uint16_t c)
{
    int32_t x, cy = graph_ht / 2, a = 38 * graph_ht / 100, py = cy, ty = clamp(t->p[P_ITYPE], 0, IT_N - 1);
    int32_t m = ins_mixq(t->p[P_IMIX]), g = 4096 + t->p[P_IA] * t->p[P_IA] * 3, lvl = LEVEL_Q12[t->p[P_IC] & 127];
    uint32_t sh = ins_bits_sh(t->p[P_IA]);
    cv_rect(PANEL_X0, cy, PANEL_W, 1, T_RAISE);
    cv_line(PANEL_X0 + PANEL_W / 2, cy - a, PANEL_X0 + PANEL_W / 2, cy + a, T_RAISE);
    if (ty == IT_OFF || ty == IT_WOBBLE || ty == IT_OCTAVER || ty == IT_PANNER || ty >= IT_PHASER || !m)
        cv_line(PANEL_X0 + PANEL_W / 6, cy + a, PANEL_X0 + PANEL_W * 5 / 6, cy - a, ty ? T_RAISE : T_DIM);
    if (ty == IT_OFF)
        return;
    for (x = 0; x < PANEL_W; x++) {
        int32_t y, w;
        if (ty == IT_OVERDRIVE) {
            int32_t v = (x - PANEL_W / 2) * 49152 / (PANEL_W / 2);
            int32_t o = softclip((v * (4096 + t->p[P_IA] * 180)) >> 12);
            if (t->p[P_IC] > 0 && (o > 12000 || o < -12000))
                o = softclip(o + ((osc_sine((uint32_t)(o * 4)) * t->p[P_IC]) >> 10));
            w = v + mulq16(o - v, (uint32_t)m << 1);
            y = cy - clamp(w, -49152, 49152) * a / 49152;
        } else if (ty == IT_WOBBLE || ty == IT_OCTAVER || ty == IT_PANNER || (ty >= IT_PHASER && ty <= IT_CHORUS)) {
            uint32_t ph = (uint32_t)x * (0xFFFFFFFFu / (PANEL_W / 2u)), tr = pf_tri(ph);
            if (ty == IT_WOBBLE) {
                w = (osc_sine(ph) + (osc_sine(ph * 3) >> 2)) * 2;
            } else if (ty == IT_OCTAVER) {
                w = (osc_sine(ph) + osc_sine(ph * 2)) * 2;
            } else if (ty == IT_PANNER) {
                int32_t shp = clamp(t->p[P_IC], 0, 2);
                w = (shp == 0 ? osc_sine(ph) : shp == 1 ? (int32_t)tr * 2 - 65536 : (ph & 0x80000000u ? -32767 : 32767)) * 2;
            } else {
                w = ty == IT_CHORUS ? osc_sine(ph) * 2 : ty == IT_FLANGER ? (int32_t)((tr * tr) >> 16) * 2 - 65536 : (int32_t)tr * 2 - 65536;
            }
            y = cy - (w >> 1) * a / 32768 * t->p[P_IB] / 127;
        } else {                                     /* the curve: v in, dry v, wet shaped */
            int32_t v = (x - PANEL_W / 2) * 49152 / (PANEL_W / 2), o;
            o = ty == IT_CRUSH ? ins_shape(IT_CRUSH, v, sh) : (ins_shape((uint32_t)ty, ((v >> 3) * g) >> 9, 0) * lvl) >> 12;
            w = v + mulq16(o - v, (uint32_t)m << 1);
            y = cy - clamp(w, -49152, 49152) * a / 49152;
        }
        if (x)
            cv_line_t(PANEL_X0 + x - 1, py, PANEL_X0 + x, y, c, 2);
        py = y;
    }
}
/* SLICER page: the pattern's 16 steps, a 'x' step a full bar; a '.' step: GATE a bar as high as it stays
 * open (DEPTH), STUT hatched (it repeats the last 'x'); the step playing underlined. Grey when OFF. */
static void graph_slicer(const track_t *t, uint16_t c)
{
    uint32_t i, pat = sl_pattern(t), mode = (uint32_t)t->p[P_SLCR], cur = sl[t - trk].idx;
    int32_t open = 70 - t->p[P_SLDEPTH] * 70 / 127;      /* px a closed GATE step keeps */
    uint16_t col = mode == SL_OFF ? T_DIM : c;
    for (i = 0; i < 16u; i++) {
        int32_t x = bar_x(i), y;
        if ((pat >> i) & 1u) {
            cv_rrect(x, 8, 9, 72, 2, mode == SL_OFF ? T_RAISE : col, T_SURF);
        } else if (mode == SL_STUT) {
            for (y = 8; y < 80; y += 4)
                cv_rect(x, y, 9, 1, col);
        } else {
            cv_rrect(x, 77, 9, 3, 1, T_RAISE, T_SURF);
            if (open > 3)
                cv_rrect(x, 80 - open, 9, open, 2, T_RAISE, T_SURF);
        }
        if (mode != SL_OFF && i == cur)
            cv_rect(x, 84, 9, 2, T_ACCENT);
    }
}
/* MACRO pages: Header shows Macro Knob & live value; Rows 1..3 are the 3 parameters in the macro */
static void graph_mod(const track_t *t, uint16_t c)
{
    (void)t;
    uint32_t m = cur_page()->id[0] % NMACROS;
    macro_t *mc = &loom_macros[m];
    uint8_t sel_line = macro_ui_line % NMACRO_DESTS;
    char b[16];

    /* Header Bar: Macro badge, Source Knob label, and Live Value gauge */
    {
        int32_t y = 8;
        char kb[16];
        kb[0] = 'K'; kb[1] = 'N'; kb[2] = 'O'; kb[3] = 'B'; kb[4] = ' '; kb[5] = (char)('1' + m); kb[6] = 0;
        cv_rrect(8, y, 56, 17, 3, T_THEME, T_SURF);
        cv_text_in(8, y + CAP_IN(M, 17), 56, &AF_M, kb, T_INK, T_THEME);

        cv_text_on(68, y + CAP_IN(M, 17), &AF_M, "MACRO", T_MID, T_SURF);
        char mb[4]; mb[0] = (char)('1' + m); mb[1] = 0;
        cv_text_on(126, y + CAP_IN(M, 17), &AF_M, mb, c, T_SURF);

        /* Value readout and meter */
        cv_text_on(138, y + CAP_IN(M, 17), &AF_M, "VAL:", T_MID, T_SURF);
        fmt_int(b, mc->val);
        cv_text_on(170, y + CAP_IN(M, 17), &AF_M, b, c, T_SURF);

        int32_t bx = 196, bw = 36;
        cv_rrect(bx, y + 4, bw, 9, 2, T_MID, T_SURF);
        int32_t fill_w = (int32_t)mc->val * (bw - 2) / 127;
        if (fill_w > 0)
            cv_rect(bx + 1, y + 5, fill_w, 7, c);

        /* Divider line below header */
        cv_rect(8, y + 21, 224, 1, T_RAISE);
    }

    /* 3 Parameter Rows: Parameters 1, 2, 3 in the macro */
    for (uint32_t d = 0; d < NMACRO_DESTS; d++) {
        int32_t y = 33 + (int32_t)d * 28;
        int sel = (sel_line == d);
        uint16_t bg = sel ? T_THEME : T_RAISE;
        macro_dest_t *dst = &mc->dest[d];
        int on = (dst->dest != 0 && dst->amt != 0);
        uint16_t col = sel ? T_INK : (on ? c : T_DIM);

        if (sel)
            cv_rrect(6, y, 228, 24, 4, T_THEME, T_SURF);
        else
            cv_rrect(6, y, 228, 24, 4, T_RAISE, T_SURF);

        b[0] = 'P'; b[1] = (char)('1' + d); b[2] = 0;
        cv_text_on(12, y + CAP_IN(M, 24), &AF_M, b, sel ? T_INK : T_MID, bg);

        if (!on && dst->dest == 0) {
            cv_text_on(34, y + CAP_IN(M, 24), &AF_M, macro_trk_name(dst->trk), sel ? T_INK : T_DIM, bg);
            cv_text_on(80, y + CAP_IN(M, 24), &AF_M, "OFF", col, bg);
            cv_text_on(142, y + CAP_IN(M, 24), &AF_M, "0%", col, bg);
        } else {
            /* Track */
            cv_text_on(34, y + CAP_IN(M, 24), &AF_M, macro_trk_name(dst->trk), sel ? T_INK : T_ACCENT, bg);

            /* Arrow */
            cv_line(70, y + 12, 78, y + 12, sel ? T_INK : T_MID);
            cv_line(74, y + 9, 78, y + 12, sel ? T_INK : T_MID);
            cv_line(74, y + 15, 78, y + 12, sel ? T_INK : T_MID);

            /* Destination Parameter Name */
            const char *dname = macro_dest_name(dst->trk, dst->dest);
            cv_text_on(84, y + CAP_IN(M, 24), &AF_M, dname, col, bg);

            /* Amount with explicit sign */
            int32_t pct = (int32_t)dst->amt * 100 / 63;
            if (dst->amt > 0) {
                b[0] = '+';
                fmt_int(b + 1, pct);
            } else {
                fmt_int(b, pct);
            }
            str_cpy(b + str_len(b), "%", sizeof b - str_len(b));

            uint16_t acol = sel ? T_INK : (dst->amt > 0 ? T_THEME : (dst->amt < 0 ? T_REC : T_MID));
            cv_text_on(142, y + CAP_IN(M, 24), &AF_M, b, acol, bg);

            /* Bipolar depth mini-meter at right edge */
            int32_t bx0 = 200;
            int32_t bw = (int32_t)dst->amt * 26 / 63;
            cv_line(bx0, y + 5, bx0, y + 19, sel ? T_INK : T_MID);
            if (bw > 0)
                cv_rect(bx0 + 1, y + 7, bw, 10, sel ? T_BG : T_THEME);
            else if (bw < 0)
                cv_rect(bx0 + bw, y + 7, -bw, 10, sel ? T_BG : T_REC);
        }
    }
}

/* Home MACROS page (GR_MACRO): Performance Macro screen with up to 4 compact areas */
static void graph_macro_perf(const track_t *t, uint16_t c)
{
    (void)t;
    uint32_t assigned[NMACROS];
    uint32_t count = 0;
    for (uint32_t m = 0; m < NMACROS; m++) {
        if (macro_is_assigned(m)) {
            assigned[count++] = m;
        }
    }

    if (count == 0) {
        const char *msg = "No Macros Assigned in LFO Menu";
        const aafont_t *f = text_w(&AF_M, msg) <= 226 ? &AF_M : &AF_S;
        cv_text_in(0, 42, 240, f, msg, T_TEXT, T_SURF);
        cv_text_in(0, 68, 240, &AF_S, "Assign Knobs & Dests in LFO Menu", T_MID, T_SURF);
        return;
    }

    int32_t x0 = 6, w = 228;

    if (count == 1) {
        uint32_t m = assigned[0];
        macro_t *mc = &loom_macros[m];
        int32_t y0 = 6, card_h = 110;

        cv_rrect(x0, y0, w, card_h, 4, T_RAISE, T_SURF);

        /* Header row: Knob badge + VAL + gauge */
        char kb[12];
        kb[0] = 'K'; kb[1] = 'N'; kb[2] = 'O'; kb[3] = 'B'; kb[4] = ' '; kb[5] = (char)('1' + m); kb[6] = 0;
        cv_rrect(x0 + 8, y0 + 6, 62, 18, 3, T_THEME, T_RAISE);
        cv_text_in(x0 + 8, y0 + 6 + CAP_IN(M, 18), 62, &AF_M, kb, T_INK, T_THEME);

        cv_text_on(x0 + 78, y0 + 6 + CAP_IN(M, 18), &AF_M, "VAL:", T_MID, T_RAISE);
        char vb[8];
        fmt_int(vb, mc->val);
        cv_text_on(x0 + 108, y0 + 6 + CAP_IN(M, 18), &AF_M, vb, c, T_RAISE);

        int32_t gx = x0 + 138, gw = 82;
        cv_rrect(gx, y0 + 9, gw, 11, 2, T_MID, T_RAISE);
        int32_t fill_w = (int32_t)mc->val * (gw - 2) / 127;
        if (fill_w > 0)
            cv_rect(gx + 1, y0 + 10, fill_w, 9, c);

        /* Divider line */
        cv_rect(x0 + 8, y0 + 29, w - 16, 1, T_SURF);

        /* Destinations */
        uint32_t row = 0;
        for (uint32_t d = 0; d < NMACRO_DESTS; d++) {
            macro_dest_t *dst = &mc->dest[d];
            if (dst->dest == 0) continue;

            int32_t ry = y0 + 35 + (int32_t)row * 24;
            char db[4]; db[0] = (char)('1' + d); db[1] = 0;
            cv_text_on(x0 + 12, ry + CAP_IN(M, 18), &AF_M, db, T_MID, T_RAISE);

            cv_text_on(x0 + 28, ry + CAP_IN(M, 18), &AF_M, macro_trk_name(dst->trk), T_ACCENT, T_RAISE);

            /* Arrow */
            cv_line(x0 + 52, ry + 9, x0 + 62, ry + 9, T_MID);
            cv_line(x0 + 58, ry + 6, x0 + 62, ry + 9, T_MID);
            cv_line(x0 + 58, ry + 12, x0 + 62, ry + 9, T_MID);

            cv_text_on(x0 + 68, ry + CAP_IN(M, 18), &AF_M, macro_dest_name(dst->trk, dst->dest), T_TEXT, T_RAISE);

            char ab[16];
            int32_t pct = (int32_t)dst->amt * 100 / 63;
            if (dst->amt > 0) {
                ab[0] = '+';
                fmt_int(ab + 1, pct);
            } else {
                fmt_int(ab, pct);
            }
            str_cpy(ab + str_len(ab), "%", sizeof ab - str_len(ab));
            uint16_t acol = dst->amt > 0 ? T_THEME : (dst->amt < 0 ? T_REC : T_MID);
            cv_text_on(x0 + 130, ry + CAP_IN(M, 18), &AF_M, ab, acol, T_RAISE);

            /* Bipolar meter at right */
            int32_t mx0 = x0 + 192;
            cv_line(mx0, ry + 3, mx0, ry + 15, T_MID);
            int32_t bw = (int32_t)dst->amt * 24 / 63;
            if (bw > 0)
                cv_rect(mx0 + 1, ry + 5, bw, 7, T_THEME);
            else if (bw < 0)
                cv_rect(mx0 + bw, ry + 5, -bw, 7, T_REC);

            row++;
        }
    } else if (count == 2) {
        int32_t card_h = 54;
        for (uint32_t i = 0; i < 2; i++) {
            uint32_t m = assigned[i];
            macro_t *mc = &loom_macros[m];
            int32_t y0 = 5 + (int32_t)i * 60;

            cv_rrect(x0, y0, w, card_h, 3, T_RAISE, T_SURF);

            /* Header: Knob + VAL + gauge */
            char kb[12];
            kb[0] = 'K'; kb[1] = 'N'; kb[2] = 'O'; kb[3] = 'B'; kb[4] = ' '; kb[5] = (char)('1' + m); kb[6] = 0;
            cv_rrect(x0 + 6, y0 + 3, 50, 14, 2, T_THEME, T_RAISE);
            cv_text_in(x0 + 6, y0 + 3 + CAP_IN(S, 14), 50, &AF_S, kb, T_INK, T_THEME);

            cv_text_on(x0 + 62, y0 + 3 + CAP_IN(S, 14), &AF_S, "VAL:", T_MID, T_RAISE);
            char vb[8];
            fmt_int(vb, mc->val);
            cv_text_on(x0 + 88, y0 + 3 + CAP_IN(S, 14), &AF_S, vb, c, T_RAISE);

            int32_t gx = x0 + 114, gw = 106;
            cv_rrect(gx, y0 + 5, gw, 9, 2, T_MID, T_RAISE);
            int32_t fill_w = (int32_t)mc->val * (gw - 2) / 127;
            if (fill_w > 0)
                cv_rect(gx + 1, y0 + 6, fill_w, 7, c);

            cv_rect(x0 + 6, y0 + 19, w - 12, 1, T_SURF);

            /* Destinations */
            uint32_t row = 0;
            for (uint32_t d = 0; d < NMACRO_DESTS && row < 2; d++) {
                macro_dest_t *dst = &mc->dest[d];
                if (dst->dest == 0) continue;

                int32_t dy = y0 + 22 + (int32_t)row * 15;
                cv_text_on(x0 + 8, dy + CAP_IN(S, 13), &AF_S, macro_trk_name(dst->trk), T_ACCENT, T_RAISE);
                cv_text_on(x0 + 28, dy + CAP_IN(S, 13), &AF_S, "->", T_MID, T_RAISE);
                cv_text_on(x0 + 42, dy + CAP_IN(S, 13), &AF_S, macro_dest_name(dst->trk, dst->dest), T_TEXT, T_RAISE);

                char ab[16];
                int32_t pct = (int32_t)dst->amt * 100 / 63;
                if (dst->amt > 0) { ab[0] = '+'; fmt_int(ab + 1, pct); } else { fmt_int(ab, pct); }
                str_cpy(ab + str_len(ab), "%", sizeof ab - str_len(ab));
                uint16_t acol = dst->amt > 0 ? T_THEME : (dst->amt < 0 ? T_REC : T_MID);
                cv_text_on(x0 + 96, dy + CAP_IN(S, 13), &AF_S, ab, acol, T_RAISE);

                int32_t mx0 = x0 + 180;
                cv_line(mx0, dy + 2, mx0, dy + 11, T_MID);
                int32_t bw = (int32_t)dst->amt * 36 / 63;
                if (bw > 0) cv_rect(mx0 + 1, dy + 3, bw, 6, T_THEME);
                else if (bw < 0) cv_rect(mx0 + bw, dy + 3, -bw, 6, T_REC);

                row++;
            }
        }
    } else if (count == 3) {
        int32_t card_h = 36;
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t m = assigned[i];
            macro_t *mc = &loom_macros[m];
            int32_t y0 = 3 + (int32_t)i * 40;

            cv_rrect(x0, y0, w, card_h, 3, T_RAISE, T_SURF);

            /* Header: K%d pill + gauge */
            char kb[8];
            kb[0] = 'K'; kb[1] = (char)('1' + m); kb[2] = 0;
            cv_rrect(x0 + 4, y0 + 3, 24, 14, 2, T_THEME, T_RAISE);
            cv_text_in(x0 + 4, y0 + 3 + CAP_IN(S, 14), 24, &AF_S, kb, T_INK, T_THEME);

            int32_t gx = x0 + 32, gw = 40;
            cv_rrect(gx, y0 + 5, gw, 9, 2, T_MID, T_RAISE);
            int32_t fill_w = (int32_t)mc->val * (gw - 2) / 127;
            if (fill_w > 0)
                cv_rect(gx + 1, y0 + 6, fill_w, 7, c);

            /* Destinations */
            int32_t xpos = x0 + 78;
            uint32_t d_drawn = 0;
            for (uint32_t d = 0; d < NMACRO_DESTS; d++) {
                macro_dest_t *dst = &mc->dest[d];
                if (dst->dest == 0) continue;

                int32_t dy = (d_drawn == 0) ? (y0 + 3) : (y0 + 19);
                int32_t cur_x = (d_drawn == 0) ? xpos : (x0 + 6 + (int32_t)(d_drawn - 1) * 110);
                if (d_drawn >= 3) break;

                char sb[32];
                str_cpy(sb, macro_trk_name(dst->trk), sizeof sb);
                str_cpy(sb + str_len(sb), ":", sizeof sb - str_len(sb));
                str_cpy(sb + str_len(sb), macro_dest_name(dst->trk, dst->dest), sizeof sb - str_len(sb));
                str_cpy(sb + str_len(sb), " ", sizeof sb - str_len(sb));
                int32_t pct = (int32_t)dst->amt * 100 / 63;
                if (dst->amt > 0) {
                    str_cpy(sb + str_len(sb), "+", sizeof sb - str_len(sb));
                    fmt_int(sb + str_len(sb), pct);
                } else {
                    fmt_int(sb + str_len(sb), pct);
                }
                str_cpy(sb + str_len(sb), "%", sizeof sb - str_len(sb));

                uint16_t acol = dst->amt > 0 ? T_THEME : (dst->amt < 0 ? T_REC : T_MID);
                cv_text_on(cur_x, dy + CAP_IN(S, 14), &AF_S, sb, acol, T_RAISE);
                d_drawn++;
            }
        }
    } else { /* count == 4 */
        int32_t card_h = 26;
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t m = assigned[i];
            macro_t *mc = &loom_macros[m];
            int32_t y0 = 4 + (int32_t)i * 29;

            cv_rrect(x0, y0, w, card_h, 3, T_RAISE, T_SURF);

            /* Pill K1..K4 */
            char kb[8];
            kb[0] = 'K'; kb[1] = (char)('1' + m); kb[2] = 0;
            cv_rrect(x0 + 4, y0 + 3, 24, 20, 2, T_THEME, T_RAISE);
            cv_text_in(x0 + 4, y0 + 3 + CAP_IN(M, 20), 24, &AF_M, kb, T_INK, T_THEME);

            /* Mini value bar */
            int32_t gx = x0 + 32, gw = 28;
            cv_rrect(gx, y0 + 7, gw, 12, 2, T_MID, T_RAISE);
            int32_t fill_w = (int32_t)mc->val * (gw - 2) / 127;
            if (fill_w > 0)
                cv_rect(gx + 1, y0 + 8, fill_w, 10, c);

            /* Inline destinations */
            int32_t dx = x0 + 66;
            for (uint32_t d = 0; d < NMACRO_DESTS; d++) {
                macro_dest_t *dst = &mc->dest[d];
                if (dst->dest == 0) continue;

                char sb[32];
                str_cpy(sb, macro_trk_name(dst->trk), sizeof sb);
                str_cpy(sb + str_len(sb), ":", sizeof sb - str_len(sb));
                str_cpy(sb + str_len(sb), macro_dest_name(dst->trk, dst->dest), sizeof sb - str_len(sb));
                str_cpy(sb + str_len(sb), " ", sizeof sb - str_len(sb));
                int32_t pct = (int32_t)dst->amt * 100 / 63;
                if (dst->amt > 0) {
                    str_cpy(sb + str_len(sb), "+", sizeof sb - str_len(sb));
                    fmt_int(sb + str_len(sb), pct);
                } else {
                    fmt_int(sb + str_len(sb), pct);
                }
                str_cpy(sb + str_len(sb), "%", sizeof sb - str_len(sb));

                uint16_t acol = dst->amt > 0 ? T_THEME : (dst->amt < 0 ? T_REC : T_MID);
                dx = cv_text_on(dx, y0 + CAP_IN(S, card_h), &AF_S, sb, acol, T_RAISE) + 8;
            }
        }
    }
}

static uint32_t steps_hash(const track_t *t)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NSTEP; i++) {
        const step_t *st = &seq_steps(t)[i];
        h = (h ^ (st->note[0] + st->n * 128u + st->time * 1024u + st->flags * 4096u + st->note[1] * 65536u)) *
            16777619u;
        h = (h ^ (st->hit | (uint32_t)st->acc << 8 | (uint32_t)st->note[2] << 16 | (uint32_t)st->note[3] << 24)) * 16777619u;
        h = (h ^ (st->vel | (uint32_t)st->probability << 8)) * 16777619u;   /* (1.5.1: DETAIL, the marks) */
    }
    return h;
}

static uint32_t str_hash(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* One shared, reduced sample display. Decode only in the UI/main loop with a
 * private IMA state, 512 samples per frame; no audio voice state is touched.
 * Cache follows the sample zone, selected note and user-slot generation. */
#define SAMPLE_WAVE_COLS 96u
static struct {
    int16_t lo[SAMPLE_WAVE_COLS], hi[SAMPLE_WAVE_COLS];
    const smp_zone_t *zone;
    uint32_t key, gen, pos;
    int32_t pred, index, peak;
    uint8_t ready;
} sample_wave;

static uint32_t sample_wave_zone(const track_t *t)
{
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL, i, zi = 0xFFFFu;
    int is_grain = (ENGINES[t->eng_req % NENGINES] == &ENG_GRAIN);
    if (si < SMP_NSETS) {
        const smp_set_t *set = &SMP_SETS[si];
        if (is_grain) {
            zi = set->nz ? set->z0 : 0xFFFFu;
        } else {
            for (i = 0; i < set->nz; i++)
                if (last_note >= SMP_ZONES[set->z0 + i].lo && last_note <= SMP_ZONES[set->z0 + i].hi) { zi = set->z0 + i; break; }
            if (zi == 0xFFFFu && set->nz) zi = set->z0;
        }
    } else {
        uint32_t k = si - SMP_NSETS;
        if (is_grain) {
            zi = usr_nz[k] ? (0x8000u | (k << 5)) : 0xFFFFu;
        } else {
            for (i = 0; i < usr_nz[k]; i++)
                if (last_note >= usr_zone[k][i].lo && last_note <= usr_zone[k][i].hi) { zi = 0x8000u | k << 5 | i; break; }
            if (zi == 0xFFFFu && usr_nz[k]) zi = 0x8000u | k << 5;
        }
    }
    return zi;
}

static void sample_wave_tick(const track_t *t)
{
    int is_grain = (ENGINES[t->eng_req % NENGINES] == &ENG_GRAIN);
    uint32_t zi = sample_wave_zone(t);
    /* Waveform depends on sample selection (SRC) and zone, not on every individual note */
    uint32_t key = (is_grain ? 65536u : 0u) | zi;
    uint32_t i;
    if (key != sample_wave.key || sample_wave.gen != smp_user_gen || !sample_wave.zone) {
        sample_wave.key = key; sample_wave.gen = smp_user_gen;
        sample_wave.zone = zi == 0xFFFFu ? 0 : smp_zone(zi);
        sample_wave.pos = 0; sample_wave.pred = sample_wave.index = 0;
        sample_wave.peak = 1; sample_wave.ready = 0;
        for (i = 0; i < SAMPLE_WAVE_COLS; i++) sample_wave.lo[i] = sample_wave.hi[i] = 0;
    }
    const smp_zone_t *z = sample_wave.zone;
    if (!z || !z->n || sample_wave.ready) return;
    for (i = 0; i < 16384u && sample_wave.pos < z->n; i++) {
        uint32_t pos = sample_wave.pos, b = SMP_DATA[z->off + (pos >> 1)];
        uint32_t code = (pos & 1u) ? b >> 4 : b & 15u;
        int32_t step = IMA_STEP[sample_wave.index], d = step >> 3;
        if (code & 4u) d += step;
        if (code & 2u) d += step >> 1;
        if (code & 1u) d += step >> 2;
        sample_wave.pred = clamp(sample_wave.pred + ((code & 8u) ? -d : d), -32768, 32767);
        sample_wave.index = clamp(sample_wave.index + IMA_IDX[code & 7u], 0, 88);
        uint32_t col = pos * SAMPLE_WAVE_COLS / z->n;
        int32_t v = sample_wave.pred, a = v < 0 ? -v : v;
        if (v < sample_wave.lo[col]) sample_wave.lo[col] = (int16_t)v;
        if (v > sample_wave.hi[col]) sample_wave.hi[col] = (int16_t)v;
        if (a > sample_wave.peak) sample_wave.peak = a;
        sample_wave.pos++;
    }
    sample_wave.ready = (sample_wave.pos == z->n);
}

static void graph_sample(uint16_t c)
{
    uint32_t i;
    const smp_zone_t *z = sample_wave.zone;
    cv_rect(PANEL_X0, 48, PANEL_W, 1, T_RAISE);
    if (!z || !z->n) return;
    if (!sample_wave.ready) {
        panel_note("LOADING", "WAVEFORM", 0);
        return;
    }

    /* Waveform */
    for (i = 0; i < SAMPLE_WAVE_COLS; i++) {
        int32_t x = 12 + (int32_t)i * 216 / (SAMPLE_WAVE_COLS - 1u);
        cv_line(x, 48 - sample_wave.hi[i] * 38 / sample_wave.peak,
                x, 48 - sample_wave.lo[i] * 38 / sample_wave.peak, c);
    }

    /* Start (Green) & End (Red) Trim Markers */
    uint32_t start_pct = (uint32_t)clamp(TSEL->p[P_E5], 0, 100);
    uint32_t end_pct = (uint32_t)clamp(TSEL->p[P_E6], 0, 100);
    int32_t sx = 12 + (int32_t)start_pct * 216 / 100;
    int32_t ex = 12 + (int32_t)end_pct * 216 / 100;
    if (ex < sx) ex = sx;

    /* Shaded active playback range */
    if (ex > sx) {
        cv_rect(sx, 10, ex - sx, 76, T_TINT);
    }

    /* Loop markers if looped */
    if (z->looped && (TSEL->p[P_E1] % 3u == 2u)) {
        int32_t left = 12 + (int32_t)(z->ls * 216u / z->n), right = 12 + (int32_t)(z->le * 216u / z->n);
        cv_line(left, 10, left, 86, T_MID); cv_line(right, 10, right, 86, T_MID);
    }

    /* Green Start Line */
    cv_line(sx, 4, sx, 92, 0x07E0);
    cv_rect(sx - 1, 4, 3, 4, 0x07E0);

    /* Red End Line */
    cv_line(ex, 4, ex, 92, 0xF800);
    cv_rect(ex - 1, 4, 3, 4, 0xF800);

    /* Active voices playhead */
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &TSEL->v[i];
        if (v->active && v->ph[0] < z->n) {
            int32_t px = 12 + (int32_t)((uint64_t)v->ph[0] * 216u / z->n);
            if (px >= 12 && px <= 228) {
                cv_line(px, 6, px, 90, 0xFFFF);
                cv_rect(px - 1, 6, 3, 4, 0xFFFF);
            }
        }
    }
}

static void graph_grain(const track_t *t, uint16_t c)
{
    uint32_t i;
    const smp_zone_t *z = sample_wave.zone;
    if (!z && sample_wave_zone(t) != 0xFFFFu)
        z = smp_zone(sample_wave_zone(t));
    gr_part_t *P = gr_of(t);
    int32_t pos_pct = t->p[P_E1];
    int32_t size_pct = t->p[P_E2];
    int32_t spry_pct = t->p[P_E5];

    /* Parameter lines & bounds: Position (cx), Start (x0), Stop (x1) */
    int32_t cx = 12 + pos_pct * 216 / 127;
    int32_t sw = spry_pct * 108 / 127;
    int32_t x0 = cx - sw, x1 = cx + sw;
    if (x0 < 12) x0 = 12;
    if (x1 > 228) x1 = 228;

    /* Base display area */
    cv_rect(PANEL_X0, 50, PANEL_W, 1, T_RAISE);

    /* 1. Shaded spray range window (remains rock-solid based on POS & SPRY) */
    if (x1 > x0 && spry_pct > 0) {
        cv_rect(x0, 18, x1 - x0, 64, T_TINT);
    }

    /* 2. Compute active grain highlight intensities per waveform column */
    uint8_t hi_fwd[SAMPLE_WAVE_COLS] = {0};
    uint8_t hi_rev[SAMPLE_WAVE_COLS] = {0};
    uint32_t n_active = 0;
    if (z && z->n > 0) {
        for (i = 0; i < GR_NG; i++) {
            const gr_grain_t *g = &P->g[i];
            if (!g->owner) continue;
            n_active++;
            int32_t col = (int32_t)((uint64_t)g->pos * SAMPLE_WAVE_COLS / z->n);
            if (col < 0) col = 0;
            if (col >= (int32_t)SAMPLE_WAVE_COLS) col = SAMPLE_WAVE_COLS - 1;

            /* Window envelope amplitude: 0..32767 -> 0..255 */
            int32_t amp = gr_win(g->wph);
            uint32_t w = (uint32_t)(amp * 255 >> 15);
            if (w > 255) w = 255;

            uint8_t *map = g->rev ? hi_rev : hi_fwd;
            if (w > map[col]) map[col] = (uint8_t)w;
            if (col > 0) {
                uint32_t side = (w * 170u) >> 8;
                if (side > map[col - 1]) map[col - 1] = (uint8_t)side;
            }
            if (col + 1 < (int32_t)SAMPLE_WAVE_COLS) {
                uint32_t side = (w * 170u) >> 8;
                if (side > map[col + 1]) map[col + 1] = (uint8_t)side;
            }
        }
    }

    /* 3. Soft colored highlight area moving across waveform */
    for (i = 0; i < SAMPLE_WAVE_COLS; i++) {
        int32_t x = 12 + (int32_t)i * 216 / (SAMPLE_WAVE_COLS - 1u);
        if (hi_rev[i] > 32) {
            uint16_t shade = ux_mix(T_ACCENT, T_SURF, (uint32_t)hi_rev[i] * 60u / 255u);
            cv_rect(x, 18, 2, 64, shade);
        } else if (hi_fwd[i] > 32) {
            uint16_t shade = ux_mix(c, T_SURF, (uint32_t)hi_fwd[i] * 60u / 255u);
            cv_rect(x, 18, 2, 64, shade);
        }
    }

    /* 4. Draw sample waveform preview (columns colored by spray and grain playback) */
    if (sample_wave.ready && sample_wave.peak > 0) {
        for (i = 0; i < SAMPLE_WAVE_COLS; i++) {
            int32_t x = 12 + (int32_t)i * 216 / (SAMPLE_WAVE_COLS - 1u);
            int32_t y1 = 50 - sample_wave.hi[i] * 32 / sample_wave.peak;
            int32_t y2 = 50 - sample_wave.lo[i] * 32 / sample_wave.peak;
            if (y2 <= y1) y2 = y1 + 1;

            uint16_t col_c;
            if (hi_rev[i] > 24) {
                /* Active reverse grain: Electric Cyan glowing to white */
                col_c = ux_mix(T_ACCENT, T_TEXT, hi_rev[i] >> 1);
            } else if (hi_fwd[i] > 24) {
                /* Active forward grain: Vivid Neon Theme color glowing to white */
                col_c = ux_mix(c, T_TEXT, hi_fwd[i] >> 1);
            } else if (x >= x0 && x <= x1) {
                /* Inside spray window: tinted theme color */
                col_c = ux_mix(c, T_SURF, 45);
            } else {
                /* Outside spray: dimmed / subtle muted outline */
                col_c = ux_mix(T_MID, T_SURF, 30);
            }
            cv_line(x, y1, x, y2, col_c);
        }
    }

    /* 5. Draw Start, Stop, and Position parameter lines (clean, on top, no handles) */
    if (spry_pct > 0) {
        /* Start line (left edge of spray window) */
        cv_line(x0, 18, x0, 82, T_MID);
        /* Stop line (right edge of spray window) */
        cv_line(x1, 18, x1, 82, T_MID);
    }
    /* Position line (center scan position POS) */
    cv_line(cx, 16, cx, 84, T_ACCENT);

    /* 6. Footer statistics overlay */
    char buf[24];
    const char *src_name = SMP_ALL_NAMES[(uint32_t)t->p[P_E0] % SMP_NALL];
    cv_text(12, 94, &AF_S, src_name, T_THEME);

    str_cpy(buf, "GRAINS: ", sizeof buf);
    fmt_int(buf + 8, n_active);
    cv_text(92, 94, &AF_S, buf, n_active ? T_ACCENT : T_MID);

    uint32_t ms = 10u + (uint32_t)size_pct * 380u / 127u;
    fmt_int(buf, ms);
    str_cpy(buf + str_len(buf), " MS", sizeof buf - str_len(buf));
    cv_text_r(228, 94, &AF_S, buf, T_MID, T_SURF);
}

static void graph_scrub(const track_t *t, uint16_t c)
{
    scrb_part_t *sp = scrb_of(t);
    const slc_src_t *s = slc_get(clamp(t->p[P_E4], 0, SLC_NSRC - 1));
    (void)c;
    if (!sp || !s || !s->len) {
        panel_alert(ICON_X_NOFILE, "NO AUDIO");
        return;
    }

    /* Reel rotation angle: 16-bit phase shift per sample (counterclockwise) */
    uint32_t rot = 0u - (uint32_t)sp->pos;

    /* Left Tape Reel (Center x=46, y=40, Radius R=36: diameter 72px) */
    const int32_t xl = 46, xr = 194, yc = 40, r = 36;
    for (int dr = 0; dr < 4; dr++) {
        cv_circle_ring(xl, yc, r - dr, T_THEME);
    }
    cv_circle_ring(xl, yc, 8, T_SURF);
    cv_rect(xl - 3, yc - 3, 7, 7, T_THEME);
    for (int k = 0; k < 3; k++) {
        uint32_t ang = rot + (uint32_t)k * 0x55555555u;
        int32_t dx = (sine_i(ang + 0x40000000u) * (r - 5)) >> 15;
        int32_t dy = (sine_i(ang) * (r - 5)) >> 15;
        cv_line(xl, yc, xl + dx, yc + dy, T_ACCENT);
    }

    /* Right Tape Reel (Center x=194, y=40, Radius R=36: diameter 72px) */
    for (int dr = 0; dr < 4; dr++) {
        cv_circle_ring(xr, yc, r - dr, T_THEME);
    }
    cv_circle_ring(xr, yc, 8, T_SURF);
    cv_rect(xr - 3, yc - 3, 7, 7, T_THEME);
    for (int k = 0; k < 3; k++) {
        uint32_t ang = rot + (uint32_t)k * 0x55555555u;
        int32_t dx = (sine_i(ang + 0x40000000u) * (r - 5)) >> 15;
        int32_t dy = (sine_i(ang) * (r - 5)) >> 15;
        cv_line(xr, yc, xr + dx, yc + dy, T_ACCENT);
    }

    /* 1px hot pink strip of tape coming out bottom of left reel into bottom of right reel (y = yc + r = 76) */
    cv_line(xl, 76, xr, 76, T_THEME);

    /* Normalized playback & target positions */
    uint32_t max_smp = s->len > 1u ? s->len - 1u : 1u;
    uint32_t pos_smp = sp->pos > 0 ? (uint32_t)(sp->pos >> 16) : 0u;
    if (pos_smp > max_smp) pos_smp = max_smp;
    int32_t px = xl + (int32_t)((pos_smp * (uint32_t)(xr - xl)) / max_smp);

    uint32_t tgt_smp = sp->target_pos > 0 ? (uint32_t)(sp->target_pos >> 16) : 0u;
    if (tgt_smp > max_smp) tgt_smp = max_smp;
    int32_t tx = xl + (int32_t)((tgt_smp * (uint32_t)(xr - xl)) / max_smp);

    /* Tension lag bar along the tape strip when scrubbing */
    if (tx != px && sp->touch_timer > 0) {
        int32_t x0 = tx < px ? tx : px;
        int32_t x1 = tx > px ? tx : px;
        cv_rect(x0, 75, x1 - x0, 3, T_TINT);
        /* Target indicator caret */
        cv_line(tx, 68, tx, 84, T_ACCENT);
        cv_rect(tx - 1, 75, 3, 3, T_ACCENT);
    }

    /* Scrub playhead centered on the 1px bottom tape strip (y=76) */
    cv_line(px, 66, px, 86, 0xFFFF);
    cv_rect(px - 1, 75, 3, 3, 0xFFFF);

    /* Center area between reels: live speed & source sample name */
    char spd_str[16];
    if (scrb_abs32(sp->velocity) > 655) {
        int spd_tenths = (int)((sp->velocity * 10) >> 16);
        char sign = spd_tenths < 0 ? '-' : '+';
        if (spd_tenths < 0) spd_tenths = -spd_tenths;
        str_cpy(spd_str, "SPD ", sizeof spd_str);
        spd_str[4] = sign;
        spd_str[5] = (char)('0' + (spd_tenths / 10));
        spd_str[6] = '.';
        spd_str[7] = (char)('0' + (spd_tenths % 10));
        spd_str[8] = 'x';
        spd_str[9] = 0;
    } else {
        str_cpy(spd_str, "STOP", sizeof spd_str);
    }
    cv_text_in(84, 24, 72, &AF_M, spd_str, T_THEME, T_SURF);
    cv_text_in(84, 42, 72, &AF_S, N_SLC_SRC[clamp(t->p[P_E4], 0, SLC_NSRC - 1)], T_MID, T_SURF);
    if (t->p[P_E5]) {
        cv_text_in(84, 56, 72, &AF_S, "[LOOP]", T_ACCENT, T_SURF);
    }

    /* Footer text at y=88: Motor status left, Position percent right */
    char mot_str[24];
    str_cpy(mot_str, "MOTOR ", sizeof mot_str);
    if (t->p[P_E1] == 0) {
        str_cpy(mot_str + 6, song.playing ? "PLAY 1.0x" : "STOP", sizeof mot_str - 6);
    } else {
        int mval = t->p[P_E1];
        int a = mval < 0 ? -mval : mval;
        mot_str[6] = mval < 0 ? '-' : '+';
        fmt_int(mot_str + 7, a / 10);
        int len = str_len(mot_str);
        mot_str[len] = '.';
        mot_str[len + 1] = (char)('0' + (a % 10));
        mot_str[len + 2] = 'x';
        mot_str[len + 3] = 0;
    }
    cv_text(16, 88, &AF_S, mot_str, T_MID);

    char pos_str[16];
    str_cpy(pos_str, t->p[P_E5] ? "L " : "P ", sizeof pos_str);
    fmt_int(pos_str + 2, (int)((pos_smp * 100u) / max_smp));
    str_cpy(pos_str + str_len(pos_str), "%", sizeof pos_str - str_len(pos_str));
    cv_text_r(224, 88, &AF_S, pos_str, T_THEME, T_SURF);
}

/* WAVETABLE: 3D Isometric Wavetable Stack + Foreground Morph Visualizer */
static void graph_wavetable(const track_t *t, uint16_t c)
{
    wt_part_t *wp = wt_of(t);
    uint32_t src = (uint32_t)clamp(t->p[P_E0], 0, WT_NSRC - 1u);
    int32_t pos_scaled = ((int32_t)t->p[P_E1] * 7 * 256) / 127;
    int cur_frame = pos_scaled >> 8;
    if (cur_frame > 7) cur_frame = 7;
    int32_t warp = clamp(t->p[P_E2], -64, 63);

    /* Top info banner */
    char buf[32];
    str_cpy(buf, "WT: ", sizeof buf);
    str_cpy(buf + str_len(buf), N_WT_SRC[src], sizeof buf - str_len(buf));
    cv_text(PANEL_X0 + 4, 10, &AF_S, buf, c);

    char pbuf[32], nbuf[8];
    str_cpy(pbuf, "POS ", sizeof pbuf);
    fmt_int(nbuf, (t->p[P_E1] * 100) / 127);
    str_cpy(pbuf + str_len(pbuf), nbuf, sizeof pbuf - str_len(pbuf));
    str_cpy(pbuf + str_len(pbuf), "%", sizeof pbuf - str_len(pbuf));
    cv_text_r(PANEL_X0 + PANEL_W - 4, 10, &AF_S, pbuf, T_MID, T_SURF);

    /* 3D Isometric Wavetable Stack: 5 perspective slices */
    const int32_t stack_frames[5] = {0, 2, 4, 6, 7};
    const int32_t slice_w = 110;
    const int32_t slice_amp = 9;

    for (int k = 4; k >= 0; k--) {
        int f = stack_frames[k];
        int32_t ox = PANEL_X0 + 16 + k * 18;
        int32_t oy = 26 + (4 - k) * 9;
        int diff = f - cur_frame;
        if (diff < 0) diff = -diff;
        int is_active = (diff <= 1);
        uint16_t col = is_active ? T_ACCENT : T_RAISE;

        int32_t prev_y = oy;
        for (int x = 0; x < slice_w; x += 3) {
            uint32_t s_idx = ((uint32_t)x * WT_FRAME_LEN) / (uint32_t)slice_w;
            int32_t val = wp ? wp->table[f][s_idx & WT_FRAME_MASK] : 0;
            int32_t y = oy - (val * slice_amp) / 32768;
            if (x > 0)
                cv_line(ox + x - 3, prev_y, ox + x, y, col);
            prev_y = y;
        }

        if (is_active) {
            cv_rect(ox + slice_w + 3, oy - 2, 4, 4, T_ACCENT);
        }
    }

    /* Foreground Active Morphed Single-Cycle Waveform */
    const int32_t fg_x0 = PANEL_X0 + 8;
    const int32_t fg_w = PANEL_W - 16;
    const int32_t fg_cy = 76;
    const int32_t fg_amp = 20;

    cv_rect(fg_x0, fg_cy, fg_w, 1, T_RAISE);

    int frame0 = cur_frame;
    int frame1 = (frame0 < (int)(WT_NFRAMES - 1u)) ? frame0 + 1 : (int)(WT_NFRAMES - 1u);
    int32_t pos_frac = pos_scaled & 0xFF;

    int32_t py = fg_cy;
    for (int x = 0; x < fg_w; x++) {
        uint32_t p_q16 = ((uint32_t)x * 65535u) / (uint32_t)fg_w;
        p_q16 = wt_warp_phase_q16(p_q16, warp);
        uint32_t i0 = (p_q16 >> 7) & WT_FRAME_MASK;
        uint32_t i1 = (i0 + 1u) & WT_FRAME_MASK;
        int32_t f_frac = (int32_t)(p_q16 & 127u);

        int32_t s0 = wp ? (wp->table[frame0][i0] + (((wp->table[frame0][i1] - wp->table[frame0][i0]) * f_frac) >> 7)) : 0;
        int32_t s1 = wp ? (wp->table[frame1][i0] + (((wp->table[frame1][i1] - wp->table[frame1][i0]) * f_frac) >> 7)) : 0;
        int32_t s = s0 + (((s1 - s0) * pos_frac) >> 8);

        int32_t y = fg_cy - ((s * fg_amp) >> 15);
        if (x > 0)
            cv_line_t(fg_x0 + x - 1, py, fg_x0 + x, y, c, 2);
        py = y;
    }
}

#if FELUCCA_SLICE
/* SLICES (ui_slice.c): the waveform from the marker before the selected one to the one after it (the selected
 * slice tinted, its marker the accent, outside the slices DIM), under it the whole material with every marker and
 * the view; then the selected slice's length and the source */
static void graph_slices(void)
{
    uint32_t a, b, len, n = slice_count(), j = slice_sel(), i, src, div, ma, mb;
    char t[20], u[12];
    if (!slice_src(&src, &div) || !n) {             /* (it plays a sine: eng_slice.c) */
        panel_alert(ICON_X_NOFILE, "SAMPLE NOT FOUND");
        return;
    }
    slice_view(&a, &b, &len);
    /* Loom: Show full sample waveform, all slice points, and active playheads */
    a = 0;
    b = len > 0 ? len : 1;
    slice_env(a, b);
    ma = slice_mark(0);
    mb = slice_mark(n);
#define SPX(p) (12 + (int32_t)((uint32_t)((p) - a) * SP_COLS / (b - a)))   /* p in [a, b] */
    if (j < n) {                                     /* the selected slice */
        uint32_t s0 = slice_mark(j), s1 = slice_mark(j + 1u);
        int32_t x0 = SPX(s0 > a ? s0 : a), x1 = SPX(s1 < b ? s1 : b);
        if (x1 > x0)
            cv_rect(x0, 6, x1 - x0, 80, T_TINT);
    }
    for (i = 0; i < SP_COLS; i++) {
        uint32_t p = a + (uint32_t)((uint32_t)i * (b - a) / SP_COLS);
        cv_line(12 + (int32_t)i, 46 - sp.hi[i] * 38 / 64, 12 + (int32_t)i, 46 - sp.lo[i] * 38 / 64,
                p >= ma && p < mb ? T_THEME : T_DIM);
    }
    for (i = 0; i <= n; i++) {                       /* the markers in the view (the selected one last: on top) */
        uint32_t p = slice_mark(i);
        if (i != j && p >= a && p <= b)
            cv_rect(SPX(p), 4, 1, 84, T_MID);
    }
    {
        uint32_t p = slice_mark(j);
        if (p >= a && p <= b)
            cv_rect(SPX(p) > 226 ? 226 : SPX(p), 2, 2, 88, T_ACCENT);
    }
    /* Active sounding slice playhead */
    for (uint32_t vi = 0; vi < NVOICE; vi++) {
        const voice_t *v = &TSEL->v[vi];
        if (v->active && v->ph[0] >= a && v->ph[0] <= b) {
            int32_t px = SPX(v->ph[0]);
            if (px >= 12 && px <= 228) {
                cv_line(px, 4, px, 88, 0xFFFF);
                cv_rect(px - 1, 4, 3, 4, 0xFFFF);
            }
        }
    }
#undef SPX
    cv_rrect(12, 96, (int32_t)SP_COLS, 3, 1, T_RAISE, T_SURF);   /* the whole: every marker, the view */
    for (i = 0; i <= n; i++)
        cv_rect(12 + (int32_t)((uint32_t)slice_mark(i) * (SP_COLS - 1u) / len), 94, 1, 7, i == j ? T_ACCENT : T_MID);
    {
        int32_t x0 = 12 + (int32_t)((uint32_t)a * SP_COLS / len), x1 = 12 + (int32_t)((uint32_t)b * SP_COLS / len);
        cv_rect(x0, 103, x1 - x0 < 2 ? 2 : x1 - x0, 2, T_THEME);
    }
    str_cpy(t, "LEN ", sizeof t);
    slice_time(u, j < n ? slice_mark(j + 1u) - slice_mark(j) : len - mb);
    str_cpy(t + 4, u, sizeof t - 4);
    str_cpy(t + str_len(t), " S", sizeof t - str_len(t));
    cv_text(12, 104, &AF_M, t, T_MID);                  /* the slice's detail (M): its capitals rows 108 .. 118 */
    cv_text_r(228, 104, &AF_M, N_SLC_SRC[src], src && src != SLC_SRC_PIANO ? T_THEME : T_DIM, T_SURF);
}
#endif

/* WHEEL: the nine drawbars as rounded bars over RAISE slots (the bars of the knob just turned: the accent),
 * their footages under them */
static void graph_wheel(const track_t *t, uint16_t c)
{
    uint32_t k;
    static const char *const names[9] = {"16", "5.3", "8", "4", "2.7", "2", "1.6", "1.3", "1"};
    for (k = 0; k < 9u; k++) {
        int32_t x = 11 + (int32_t)k * 25, level = drw_level(t->p, k);
        if (t->p[P_E4] && k == 8u) level = 0; /* DSP's percussion cancels the 1-foot bar. */
        int hot = ui.hot_t && cur_page()->id[0] == P_E0 &&
                  (ui.hot_col == 0u || ui.hot_col == (k < 2u ? 1u : k < 4u ? 2u : 3u));
        cv_rrect(x + 7, 2, 4, 73, 2, T_RAISE, T_SURF);
        if (level) cv_rrect(x + 3, 75 - level * 8, 12, level * 8, 3, hot ? T_ACCENT : c, T_SURF);
        else cv_rrect(x + 3, 73, 12, 2, 1, hot ? T_ACCENT : T_DIM, T_SURF);
        GFX_HOOK_ALIGN(x + 7, 0, x + 11, 0, AL_H, "wheel footage under its drawbar");
        cv_text_in(x + 3, 82, 12, &AF_S, names[k], T_MID, T_SURF);   /* under the bar, by its ink */
    }
}
/* The FM charts' parts: an operator box 21 x 17 (rows 23 px apart, columns 24), junction dots, Manhattan routes */
#define FM_BH 17
static void fm_dot(int32_t x, int32_t y, uint16_t c) { cv_rect(x - 1, y - 1, 3, 3, c); }
/* a Manhattan route, 1 px: down from (x0, y0) to the jog row jy, across to x1, down to y1 */
static void fm_route(int32_t x0, int32_t y0, int32_t x1, int32_t jy, int32_t y1, uint16_t c)
{
    cv_rect(x0, y0, 1, jy - y0, c);
    cv_rect(x0 < x1 ? x0 : x1, jy, (x0 < x1 ? x1 - x0 : x0 - x1) + 1, 1, c);
    cv_rect(x1, jy, 1, y1 - jy, c);
}
/* a feedback loop on the operator box at (x, y): out of its right side, up, back in on top with an arrow */
static void fm_loop(int32_t x, int32_t y, uint16_t c)
{
    cv_rect(x + 11, y + 5, 5, 1, c);
    cv_rect(x + 15, y - 4, 1, 9, c);
    cv_rect(x, y - 4, 15, 1, c);
    cv_rect(x, y - 4, 1, 4, c);
    cv_line(x - 2, y - 3, x - 1, y - 2, c); cv_line(x + 2, y - 3, x + 1, y - 2, c);
}
/* the output bus under the carriers (the leftmost at cl, the rightmost at cr) at y bus, an arrow out on the right */
static void fm_bus(int32_t cl, int32_t cr, int32_t bus)
{
    cv_rect(cl, bus, cr + 24 - cl, 1, T_MID);
    cv_line(cr + 20, bus - 3, cr + 23, bus, T_MID); cv_line(cr + 20, bus + 3, cr + 23, bus, T_MID);
}

#ifndef FM6_CHART_HOOK
#define FM6_CHART_HOOK(kind, a, b) ((void)0)   /* ui_render.c (host tests): the chart's parts one by one (its lint) */
#endif
enum { FMH_BOX, FMH_ROUTE, FMH_CAR, FMH_BUS, FMH_FB, FMH_LABEL, FMH_END };
/* FM6's 32 algorithms as charts. FM6_CELL: per operator 1..6 its cell, the column in 24 px steps (bits 0..2) and
 * the row above the output bus (bits 4..5; row 0 = the carriers). The routes, the carriers and the feedback
 * operator are fm6_core.c's FM6_ALG itself (fm6_routes, fm6_car_ops, fm6_fb_op). ui_test.c checks the cells
 * against them (a modulator one row above what it modulates, the carriers on the bottom row and only they, one
 * operator per cell) and the routes against the 32 algorithms written out; ui_render.c lints every chart as drawn
 * (no route through a box or touching another route, no two boxes overlapping, all inside the panel). The cells:
 * a search for the fewest and shortest bends, the operators numbered left to right. */
static const uint8_t FM6_CELL[32][6] = {
    {0x00, 0x10, 0x01, 0x11, 0x21, 0x31}, {0x00, 0x10, 0x02, 0x12, 0x22, 0x32}, {0x00, 0x10, 0x20, 0x01, 0x11, 0x21},
    {0x00, 0x10, 0x20, 0x01, 0x11, 0x21}, {0x00, 0x10, 0x01, 0x11, 0x02, 0x12}, {0x00, 0x10, 0x01, 0x11, 0x02, 0x12},
    {0x00, 0x10, 0x01, 0x11, 0x12, 0x22}, {0x00, 0x10, 0x01, 0x12, 0x11, 0x21}, {0x00, 0x10, 0x02, 0x12, 0x13, 0x23},
    {0x00, 0x10, 0x20, 0x01, 0x11, 0x12}, {0x00, 0x10, 0x20, 0x01, 0x11, 0x12}, {0x00, 0x10, 0x03, 0x12, 0x13, 0x14},
    {0x00, 0x10, 0x02, 0x11, 0x12, 0x13}, {0x00, 0x10, 0x01, 0x11, 0x20, 0x21}, {0x00, 0x10, 0x02, 0x12, 0x21, 0x22},
    {0x01, 0x10, 0x11, 0x21, 0x12, 0x22}, {0x01, 0x12, 0x10, 0x20, 0x11, 0x21}, {0x01, 0x10, 0x12, 0x11, 0x21, 0x31},
    {0x00, 0x10, 0x20, 0x01, 0x02, 0x11}, {0x00, 0x01, 0x10, 0x02, 0x12, 0x13}, {0x00, 0x01, 0x10, 0x02, 0x03, 0x12},
    {0x00, 0x10, 0x01, 0x02, 0x03, 0x12}, {0x00, 0x01, 0x11, 0x02, 0x03, 0x12}, {0x00, 0x01, 0x02, 0x03, 0x04, 0x13},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x13}, {0x00, 0x01, 0x11, 0x02, 0x12, 0x13}, {0x00, 0x01, 0x11, 0x03, 0x13, 0x14},
    {0x00, 0x10, 0x01, 0x11, 0x21, 0x02}, {0x00, 0x01, 0x02, 0x12, 0x03, 0x13}, {0x00, 0x01, 0x02, 0x12, 0x22, 0x03},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x14}, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05}};
/* algorithm a's routes: m[i] bit j: operator j + 1 modulates operator i + 1. FM6_ALG runs the operators sixth
 * first, each reading a bus (or none) and writing one (or adding to it): who wrote a bus modulates its reader */
static void fm6_routes(uint32_t a, uint8_t *m)
{
    const uint8_t *f = FM6_ALG[a & 31u];
    uint8_t bus[4] = {0, 0, 0, 0};
    uint32_t k;
    for (k = 0; k < 6u; k++) {
        uint32_t op = 5u - k, in = (f[k] >> 4) & 3u, o = f[k] & 3u;
        m[op] = in ? bus[in] : 0u;
        bus[o] = (uint8_t)(((f[k] & FM6_OADD) ? bus[o] : 0u) | 1u << op);
    }
}
/* the operator with feedback in algorithm a (0..5: operator 1..6) */
static uint32_t fm6_fb_op(uint32_t a)
{
    uint32_t k;
    for (k = 0; k < 5u && (FM6_ALG[a & 31u][5u - k] & 0xC0u) != 0xC0u; k++)
        ;
    return k;
}
/* the carriers of algorithm a: bit k operator k + 1 (fm6_carriers counts the sixth first) */
static uint32_t fm6_car_ops(uint32_t a)
{
    uint32_t c = fm6_carriers(a), r = 0, k;
    for (k = 0; k < 6u; k++)
        r |= (c >> (5u - k) & 1u) << k;
    return r;
}
/* the algorithm FM6 plays on track t (0..31): ALG, or the patch's at PAT */
static uint32_t fm6_alg_of(const track_t *t)
{
    return t->p[P_E0] >= 1 && t->p[P_E0] <= 32 ? (uint32_t)t->p[P_E0] - 1u : fm6_patch[(t - trk) % NTRK][FP_ALG] & 31u;
}
/* FM6's EDIT pages: the algorithm, drawn as graph_fm draws DIGITAL's, "ALG 05" top left; SLOT on a bank voice (B1..B32,
 * 1.4.1): its name top right. Carriers THEME with INK
 * numerals, modulators RAISE with THEME ones; an operator at output level 0: a DIM numeral (a carrier on RAISE).
 * Routes THEME, the feedback loop MID (DIM at feedback 0), the output bus MID. The knob just turned in ACCENT:
 * ALG and PTCH the label, FB the loop, MLVL the routes, MRAT MEG VMOD the modulators, DTUN the carriers.
 * sel < 6 (the OPERATOR pages, ui_fm6op.c): that operator's box ACCENT with an INK numeral, its frequency top right */
static void graph_fm6(const track_t *t, uint16_t c, uint32_t sel)
{
    uint32_t alg = fm6_alg_of(t), k, j, car = fm6_car_ops(alg), top = 0, hi = 0, fbop = fm6_fb_op(alg), hotset = 0;
    uint32_t hot = ui.hot_t ? (uint32_t)cur_page()->id[ui.hot_col & 3u] : 0u;
    const uint8_t *pt = fm6_patch[(t - trk) % NTRK];
    int32_t x[6], y[6], bus, cl = 240, cr = 0, fb = clamp(pt[FP_FB] + t->p[P_E1], 0, 7);
    uint8_t m[6], fan[6] = {0, 0, 0, 0, 0, 0};
    uint16_t rc = hot == P_E2 ? T_ACCENT : c;
    char b[7] = {'A', 'L', 'G', ' ', (char)('0' + (alg + 1u) / 10u), (char)('0' + (alg + 1u) % 10u), 0};
    if (hot == P_E3 || hot == P_E4 || hot == P_E5)
        hotset = 63u & ~car;
    else if (hot == P_E6)
        hotset = car;
    fm6_routes(alg, m);
    for (k = 0; k < 6u; k++) {
        uint32_t col = FM6_CELL[alg][k] & 7u, row = FM6_CELL[alg][k] >> 4;
        hi = col > hi ? col : hi;
        top = row > top ? row : top;
        for (j = 0; j < 6u; j++)
            fan[j] += (uint8_t)(m[k] >> j & 1u);
    }
    bus = 50 + (27 + 23 * (int32_t)top) / 2;         /* the chart centred on the 100 px scale */
    for (k = 0; k < 6u; k++) {
        x[k] = 120 + (int32_t)(FM6_CELL[alg][k] & 7u) * 24 - (int32_t)hi * 12;
        y[k] = bus - 6 - FM_BH - 23 * (int32_t)(FM6_CELL[alg][k] >> 4);
        if (car >> k & 1u) { cl = x[k] < cl ? x[k] : cl; cr = x[k] > cr ? x[k] : cr; }
    }
    for (k = 0; k < 6u; k++) {                       /* into k: its sources down to its jog row, across, down */
        for (j = 0; j < 6u; j++)
            if (m[k] >> j & 1u) {
                FM6_CHART_HOOK(FMH_ROUTE, j, k);
                fm_route(x[j], y[j] + FM_BH, x[k], y[k] - 3, y[k], rc);
                if (fan[j] > 1u) fm_dot(x[j], y[k] - 3, rc);          /* one operator modulating several */
                if (m[k] & (m[k] - 1u)) fm_dot(x[k], y[k] - 3, rc);   /* several modulating one */
            }
        if (car >> k & 1u) {
            FM6_CHART_HOOK(FMH_CAR, k, 0);
            cv_rect(x[k], y[k] + FM_BH, 1, bus - y[k] - FM_BH, T_MID);
            if (x[k] != cl) fm_dot(x[k], bus, T_MID);
        }
    }
    FM6_CHART_HOOK(FMH_BUS, 0, 0);
    fm_bus(cl, cr, bus);
    FM6_CHART_HOOK(FMH_FB, fbop, 0);
    fm_loop(x[fbop], y[fbop], hot == P_E1 ? T_ACCENT : fb ? T_MID : T_DIM);
    for (k = 0; k < 6u; k++) {
        uint32_t on = pt[(5u - k) * FP_OP + FP_OL] != 0, fill_c = (car >> k & 1u) && on;
        uint16_t f = hotset >> k & 1u ? T_ACCENT : c, fill = k == sel ? T_ACCENT : fill_c ? f : T_RAISE;
        char n[2] = {(char)('1' + k), 0};
        FM6_CHART_HOOK(FMH_BOX, k, 0);
        cv_rrect(x[k] - 10, y[k], 21, FM_BH, 3, fill, T_SURF);
        GFX_HOOK_ALIGN(x[k] - 10, y[k], x[k] + 11, y[k] + FM_BH, AL_HV, "FM6 operator number in its box");
        cv_text_in(x[k] - 10, y[k] + CAP_IN(S, FM_BH), 21, &AF_S, n, fill_c || k == sel ? T_INK : on ? f : T_DIM, fill);
    }
    FM6_CHART_HOOK(FMH_LABEL, 0, 0);
    cv_text(10, 4 - GOY, &AF_S, b, hot == P_E0 || hot == P_E7 ? T_ACCENT : T_MID);
    if (sel < 6u) {                                  /* the operator picked: "OP3 x1.50" */
        char fq[16] = {'O', 'P', (char)('1' + sel), ' ', 0};
        fop_freq(pt + (5u - sel) * FP_OP, fq + 4);
        cv_text_r(228, 4 - GOY, &AF_S, fq, T_TEXT, T_SURF);
    } else if (fm6_bank_slot(fm6_slot[(t - trk) % NTRK]) >= 0) {   /* SLOT B1..B32: the bank voice's name */
        char nm[11];
        for (k = 0; k < 10u; k++)
            nm[k] = (char)pt[FP_NAME + k];
        for (nm[10] = 0; k && nm[k - 1] == ' '; k--)
            nm[k - 1] = 0;
        cv_text_r(228, 4 - GOY, &AF_S, nm, T_TEXT, T_SURF);
    }
    FM6_CHART_HOOK(FMH_END, alg, 0);
}

/* OP ENV (ui_fm6op.c): the picked operator's envelope as the patch has it: from L4 to L1 at R1, L2 at R2, L3 at R3, held
 * there while the key is down (a RAISE mark where it is let go), back to L4 at R4. A stage's width grows as its rate
 * falls (4..48 px: 99 is a step, 0 the longest), the levels 0..99 linearly (dB). The curve THEME, the stage KNOB 3 / 4
 * edit (STAGE) ACCENT with a dot at its target; "OP3" top left, the stage's rate and level top right */
static void graph_fm6env(const track_t *t, uint16_t c)
{
    static const char *const STG[4] = {"ATK", "DEC", "SUS", "REL"};
    const uint8_t *op = fop_bytes((uint32_t)(t - trk));
    int32_t top = 24, bot = 88, x[6], y[6], k;
    uint32_t sg = fop_stg & 3u;
    char b[24] = {'O', 'P', (char)('1' + fop_op % 6u), 0};
#define FEGY(l) (bot - (int32_t)(l) * (bot - top) / 99)
#define FEGW(r) (4 + (99 - (int32_t)(r)) * (99 - (int32_t)(r)) * 44 / 9801)
    x[0] = 12;
    y[0] = FEGY(op[FP_L1 + 3]);
    for (k = 0; k < 3; k++) {
        x[k + 1] = x[k] + FEGW(op[FP_R1 + k]);
        y[k + 1] = FEGY(op[FP_L1 + k]);
    }
    x[5] = 228;
    x[4] = x[5] - FEGW(op[FP_R1 + 3]);
    y[4] = y[3];
    y[5] = y[0];
    cv_rect(PANEL_X0, bot + 2, PANEL_W, 1, T_RAISE);
    cv_rect(x[4], top, 1, bot - top + 1, T_RAISE);   /* the key let go */
    for (k = 0; k < 5; k++)                          /* (3: the hold, no stage of its own) */
        cv_line_t(x[k], y[k], x[k + 1], y[k + 1], k != 3 && (uint32_t)(k < 3 ? k : 3) == sg ? T_ACCENT : c, 2);
    k = sg < 3u ? (int32_t)sg + 1 : 5;
    fm_dot(x[k], y[k], T_ACCENT);
#undef FEGY
#undef FEGW
    cv_text(10, 4 - GOY, &AF_S, b, T_MID);
    str_cpy(b, STG[sg], sizeof b);
    str_cpy(b + str_len(b), "  R ", 5);
    fmt_int(b + str_len(b), op[FP_R1 + sg]);
    str_cpy(b + str_len(b), "  L ", 5);
    fmt_int(b + str_len(b), op[FP_L1 + sg]);
    cv_text_r(228, 4 - GOY, &AF_S, b, T_TEXT, T_SURF);
}

#if FELUCCA_FM4
/* DIGITAL's eight algorithms as FM charts, read from src/eng_digital.c's switch (alg) (ui_test.c checks
 * these tables against it). FM_CELL: per operator 1..4 its grid cell, the column in 24 px steps (bits 0..2)
 * and the row above the output bus (bits 4..5; row 0 = the carriers). FM_MOD: per destination operator d a
 * nibble at bit 4 d of the operators modulating it (bit k = operator k + 1). Operator 4 feeds itself (FB). */
static const uint8_t FM_CELL[8][4] = {
    {0x00, 0x10, 0x20, 0x30}, {0x01, 0x11, 0x20, 0x22}, {0x01, 0x10, 0x20, 0x12}, {0x01, 0x12, 0x10, 0x20},
    {0x00, 0x10, 0x02, 0x12}, {0x00, 0x02, 0x04, 0x12}, {0x00, 0x02, 0x04, 0x14}, {0x00, 0x02, 0x04, 0x06}};
static const uint16_t FM_MOD[8] = {0x0842, 0x00C2, 0x004A, 0x0806, 0x0802, 0x0888, 0x0800, 0x0000};
/* Modulators above what they modulate, the carriers on the bottom row over the output bus (an arrow out).
 * Carriers THEME with INK numerals, modulators RAISE with THEME ones; an operator at LEVEL 0: DIM numeral.
 * Routes by IDX: DIM at 0, MID, THEME from 64; op 4's feedback loop MID (DIM at FB 0). The knob just turned
 * (an operator's ratio or level, IDX, FB): ACCENT. Junction dots where routes split or merge. */
static void graph_fm(const track_t *t, uint16_t c)
{
    uint32_t alg = (uint32_t)t->p[P_E0] & 7u, k, j, mods = FM_MOD[alg], car = 0, hop = 9;
    uint32_t hot = ui.hot_t ? (uint32_t)cur_page()->id[ui.hot_col & 3u] : 0u;
    int32_t x[4], y[4], lo = 7, hi = 0, top = 0, bus, cl = 240, cr = 0, idx = t->p[P_E4];
    uint16_t ec = hot == P_E4 ? T_ACCENT : !idx ? T_DIM : idx < 64 ? T_MID : c;
    if (hot >= P_E1 && hot <= P_E3) hop = hot - P_E1 + 1u;
    else if (hot >= P_FM1_ATK && hot < P_E0) hop = (hot - P_FM1_ATK) / 5u;
    for (k = 0; k < 4u; k++) {
        int32_t col = FM_CELL[alg][k] & 7, row = FM_CELL[alg][k] >> 4;
        lo = col < lo ? col : lo; hi = col > hi ? col : hi; top = row > top ? row : top;
    }
    bus = 50 + (27 + 23 * top) / 2;                  /* the chart centred on the 100 px scale */
    for (k = 0; k < 4u; k++) {
        x[k] = 120 + (FM_CELL[alg][k] & 7) * 24 - (lo + hi) * 12;
        y[k] = bus - 6 - FM_BH - 23 * (FM_CELL[alg][k] >> 4);
        if (FM_CELL[alg][k] < 0x10) { car |= 1u << k; cl = x[k] < cl ? x[k] : cl; cr = x[k] > cr ? x[k] : cr; }
    }
    for (k = 0; k < 4u; k++) {                       /* into k: sources down to its jog row, across, down */
        uint32_t m = (mods >> (4u * k)) & 15u;
        for (j = 0; j < 4u; j++) if (m >> j & 1u) {
            uint32_t fan = mods & (0x1111u << j);
            fm_route(x[j], y[j] + FM_BH, x[k], y[k] - 3, y[k], ec);
            if (fan & (fan - 1u)) fm_dot(x[j], y[k] - 3, ec);   /* one operator modulating several */
        }
        if (m & (m - 1u)) fm_dot(x[k], y[k] - 3, ec);           /* several modulating one */
        if (car >> k & 1u) {
            cv_rect(x[k], y[k] + FM_BH, 1, bus - y[k] - FM_BH, T_MID);
            if (x[k] != cl) fm_dot(x[k], bus, T_MID);
        }
    }
    fm_bus(cl, cr, bus);                             /* the output bus and its arrow */
    fm_loop(x[3], y[3], hot == P_E6 ? T_ACCENT : t->p[P_E6] ? T_MID : T_DIM);   /* op 4's feedback */
    for (k = 0; k < 4u; k++) {
        char b[2] = {(char)('1' + k), 0};
        uint32_t on = t->p[P_FM1_LEVEL + k * 5u] != 0, fill_c = (car >> k & 1u) && on;
        uint16_t f = k == hop ? T_ACCENT : c, fill = fill_c ? f : T_RAISE;
        cv_rrect(x[k] - 10, y[k], 21, FM_BH, 3, fill, T_SURF);
        GFX_HOOK_ALIGN(x[k] - 10, y[k], x[k] + 11, y[k] + FM_BH, AL_HV, "DIGITAL operator number in its box");
        cv_text_in(x[k] - 10, y[k] + CAP_IN(S, FM_BH), 21, &AF_S, b, fill_c ? T_INK : on ? f : T_DIM, fill);
    }
}
#endif
/* Save-bank validity is expensive (CRC/import). Refresh it once per redraw or
 * twice per second, rather than parsing four projects on every UI frame. */
static char graph_pname[4][13];                       /* .. and the slots' names ("" none) */
static uint16_t graph_plen[4][NTRK];                  /* .. each track's pattern length (project_info: 1/48 quarters) */
static uint32_t graph_pden[4][NTRK];                  /* .. and its picture for SONG (project.c proj_density: 2 bits a cell) */
static uint8_t graph_pcell[4][NTRK];                  /* .. in this many cells (min(LEN, 16)) */
static uint32_t graph_pname_sig;
static int graph_project_used(uint32_t slot)
{
    static uint32_t ms, frame;
    static uint8_t mask, ready;
    if (!ready || fm1_ms - ms >= 500u || (ui.force && frame != ui.frame)) {
        uint32_t i; mask = 0;
        for (i = 0; i < 4u; i++)
            mask |= (uint8_t)((project_info(i, graph_pname[i], graph_plen[i], graph_pden[i], graph_pcell[i]) != 0) << i);
        graph_pname_sig = fnv(fnv(2166136261u, graph_pname, sizeof graph_pname), graph_plen, sizeof graph_plen);
        graph_pname_sig = fnv(fnv(graph_pname_sig, graph_pden, sizeof graph_pden), graph_pcell, sizeof graph_pcell);
        ready = 1; ms = fm1_ms; frame = ui.frame;
    }
    return (mask >> (slot & 3u)) & 1u;
}
static const char *graph_project_name(uint32_t slot)  /* (after graph_project_used) */
{
    return graph_pname[slot & 3u];
}

/* SONG's timeline (graph_song): the sections left to right, each as wide as it plays: REPS x its clock track's pattern
 * (song_chain.c chain_clock: the first track not silent there; project_info: that track's LEN x DIV in its slot, what
 * chain_tick counts; a slot not saved, or every track silent: one bar). A short song is stretched over the strip
 * (fitted); a section under SG_MINW gets SG_MINW and the rest is shared out again; when the sections would get under
 * SG_FITBAR px a bar the song scrolls at SG_BAR px a bar, the view following the cursor's section and, playing, the
 * playing one. + ADD's slot (count < CHAIN_ROWS) at the end. Content px: 0 .. the width. Four lanes, T1 at the top */
#define SG_X0 24                                       /* the strip: x 24 .. 234 (the lanes' cushions left of it) */
#define SG_W 210
#define SG_Y 3                                         /* the lanes: y 3 .. 88 */
#define SG_LH 20                                       /* a lane's height (its letter, the pattern's picture), SG_LG between two */
#define SG_LG 2
#define SG_H (NTRK * (SG_LH + SG_LG) - SG_LG)
#define SG_LY(k) (SG_Y + (int32_t)(k) * (SG_LH + SG_LG))   /* lane k's top */
#define SG_GAP 2                                       /* between two blocks (in a block's width) */
#define SG_MINW 16                                     /* a block's least width, its gap included: a letter in it */
#define SG_ADDW 20                                     /* + ADD's slot */
#define SG_BAR 6                                       /* scrolled: px a bar */
#define SG_FITBAR 3                                    /* fitted: at least this many px a bar */
#define SG_BARU 192u                                   /* a 4/4 bar in 1/48 quarters */
#define SG_RY (SG_Y + SG_H + 3)                        /* the ruler's line (92); its ticks and numbers under it */
typedef struct {
    uint32_t n, add, fit;                              /* the rows, + ADD shown, fitted (no scrolling) */
    uint32_t u0[CHAIN_ROWS + 1u];                      /* each row's start in 1/48 quarters; u0[n]: the song's length */
    int32_t x[CHAIN_ROWS + 2u];                        /* each row's left edge (content px); x[n]: + ADD's; x[n + 1]: the end */
} song_lay_t;
static struct { int32_t off; uint32_t frame; uint8_t init, cur, play, last; } sgv;   /* the view (scrolled) */
/* a * b / c without a 64-bit divide: a and c halved alike until a * b fits */
static uint32_t sg_muldiv(uint32_t a, uint32_t b, uint32_t c)
{
    while (b && a > 0xFFFFFFFFu / b) { a >>= 1; c >>= 1; }
    return c ? a * b / c : 0u;
}
static uint32_t song_unit(const chain_row_t *r)        /* one pass of a section: its clock track's (1/48 quarters) */
{
    uint32_t k = chain_clock(r), s = r->slot[k];
    return s < 4u && graph_project_used(s) && graph_plen[s][k] ? graph_plen[s][k] : SG_BARU;
}
static void song_layout(song_lay_t *L)
{
    uint32_t i, n = chain_config.count <= CHAIN_ROWS ? chain_config.count : CHAIN_ROWS, forced = 0, nf = 0, uf = 0, chg = 1;
    uint32_t wb;
    L->n = n;
    L->add = n < CHAIN_ROWS;
    L->u0[0] = 0;
    for (i = 0; i < n; i++)
        L->u0[i + 1u] = L->u0[i] + chain_config.row[i].repeat * song_unit(&chain_config.row[i]);
    wb = SG_W - (L->add ? SG_ADDW : 0u);
    while (chg) {                                      /* fitted: shared by length, SG_MINW the least */
        chg = 0; nf = 0; uf = 0;
        for (i = 0; i < n; i++)
            if ((forced >> i) & 1u) nf++;
            else uf += L->u0[i + 1u] - L->u0[i];
        if (nf * SG_MINW > wb)
            break;
        for (i = 0; i < n; i++)
            if (!((forced >> i) & 1u) && sg_muldiv(L->u0[i + 1u] - L->u0[i], wb - nf * SG_MINW, uf) < SG_MINW) {
                forced |= 1u << i;
                chg = 1;
            }
    }
    L->fit = nf * SG_MINW <= wb && (!uf || sg_muldiv(SG_BARU, wb - nf * SG_MINW, uf) >= SG_FITBAR);
    if (L->fit) {
        uint32_t nb = 0, ub = 0;
        for (i = 0; i <= n; i++) {
            L->x[i] = (int32_t)(nb * SG_MINW + (uf ? sg_muldiv(ub, wb - nf * SG_MINW, uf) : 0u));
            if (i < n && ((forced >> i) & 1u)) nb++;
            else if (i < n) ub += L->u0[i + 1u] - L->u0[i];
        }
    } else {
        L->x[0] = 0;
        for (i = 0; i < n; i++) {
            uint32_t w = ((L->u0[i + 1u] - L->u0[i]) * SG_BAR + SG_BARU / 2u) / SG_BARU;
            L->x[i + 1u] = L->x[i] + (int32_t)(w > SG_MINW ? w : SG_MINW);
        }
    }
    L->x[n + 1u] = L->x[n] + (L->add ? SG_ADDW : 0);
}
static int32_t song_ux(const song_lay_t *L, uint32_t u)   /* 1/48 quarters -> content px (the blocks' scale) */
{
    uint32_t i = 0;
    while (i + 1u < L->n && u >= L->u0[i + 1u])
        i++;
    if (!L->n)
        return 0;
    return L->x[i] + (int32_t)sg_muldiv(u - L->u0[i], (uint32_t)(L->x[i + 1u] - L->x[i]), L->u0[i + 1u] - L->u0[i]);
}
static uint32_t song_cur(void) { return ui.song_row < chain_config.count ? ui.song_row : chain_config.count; }
/* the playhead, content px: the playing section's repeats played and its clock track's step in its pattern (-1: none) */
static int32_t song_playx(const song_lay_t *L)
{
    uint32_t r = chain.row, len, idx, reps;
    const track_t *t = &trk[chain.clk % NTRK];
    if (!chain.running || r >= L->n)
        return -1;
    len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    idx = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_idx % len;
    reps = chain.config.row[r].repeat ? chain.config.row[r].repeat : 1u;
    return L->x[r] + (int32_t)sg_muldiv((reps - (chain.remaining <= reps ? chain.remaining : reps)) * len + idx,
                                        (uint32_t)(L->x[r + 1u] - L->x[r] - SG_GAP), reps * len);
}
/* once a frame: the view toward the cursor's block and the playing one (both when they fit, else the one that moved
 * last), a block's margin round them; halving the way with MENU > ANIM ON, at once on a redraw forced, a gap in
 * the frames or ANIM OFF. Returns the view's left edge */
static int32_t song_follow(const song_lay_t *L)
{
    uint32_t c = song_cur(), p = chain.running && chain.row < L->n ? chain.row : 0xFFu;
    int32_t a = L->x[c], b = c < L->n ? L->x[c + 1u] - SG_GAP : L->x[c] + SG_ADDW, tgt = sgv.off, d, m, end;
    int fresh = !sgv.init || ui.frame != sgv.frame + 1u, snap = fresh || ui.force || (ui_prefs & PREF_ANIM_OFF);
    if (sgv.init && sgv.frame == ui.frame)
        return sgv.off;
    if (fresh) {                                     /* the page (re)entered: the playing row first */
        sgv.off = 0;
        sgv.last = p != 0xFFu;
    } else if (p != sgv.play) {
        sgv.last = 1;
    } else if (c != sgv.cur) {
        sgv.last = 0;
    }
    if (p != 0xFFu) {
        int32_t pa = L->x[p], pb = L->x[p + 1u] - SG_GAP;
        if ((pb > b ? pb : b) - (pa < a ? pa : a) <= SG_W) {
            a = pa < a ? pa : a;
            b = pb > b ? pb : b;
        } else if (sgv.last) {
            a = pa;
            b = pb;
        }
    }
    m = (SG_W - (b - a)) / 2;
    m = m > SG_MINW ? SG_MINW : m < 0 ? 0 : m;
    if (a - m < tgt) tgt = a - m;
    if (b + m > tgt + SG_W) tgt = b + m - SG_W;
    end = L->x[L->n + 1u] - SG_W;
    tgt = L->fit || end < 0 ? 0 : tgt < 0 ? 0 : tgt > end ? end : tgt;
    d = tgt - sgv.off;
    sgv.off = snap ? tgt : sgv.off + (d / 3 ? d / 3 : d > 0 ? 1 : d < 0 ? -1 : 0);
    sgv.cur = (uint8_t)c;
    sgv.play = (uint8_t)p;
    sgv.frame = ui.frame;
    sgv.init = 1;
    return sgv.off;
}

/* MENU > LARGE's strip under the tall cards (ui.c LK_TALL): HOME's scope, ENV's ADSR, LFO's wave,
 * MIXER's tracks drawn small; on the other pages the page's title and number (their charts need the whole panel) */
enum { SK_NONE, SK_SCOPE, SK_ADSR, SK_LFO, SK_TRK, SK_TITLE };
static uint32_t strip_kind(void)
{
    uint32_t g;
    if (large_kind() != LK_TALL)
        return SK_NONE;
    if (ui.home)
        return SK_SCOPE;
    g = cur_page()->graph;
    return g == GR_ADSR ? SK_ADSR : g == GR_LFO ? SK_LFO : g == GR_TRK ? SK_TRK : SK_TITLE;
}
/* the page's title and its number in its family ("ENV DEST 2/2", EDIT: the engine's "OSC 1/2"); the piano roll: its
 * scale ("C MIN"); ti holds 20 (the footer, the strip) */
static void page_title(char *ti)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i, n = 0, k = 0;
    const char *pt = pg->scope == SC_ENGINE ? e->page_title[pg->id[0] != P_E0] : 0;   /* EDIT: the engine's */
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == pg->fam && page_visible(i)) {
            n++;
            if (i == ui.page)
                k = n;
        }
    str_cpy(ti, pt ? pt : (pg->fam != FAM_SEQ && grid_on() && !detail_on()) ? "GRID" : pg->title, 14);
    if (n > 1) {
        str_cpy(ti + str_len(ti), " ", 20 - str_len(ti));
        fmt_int(ti + str_len(ti), (int32_t)k);
        str_cpy(ti + str_len(ti), "/", 20 - str_len(ti));
        fmt_int(ti + str_len(ti), (int32_t)n);
    }
    if (pg->fam != FAM_SEQ && pg->graph == GR_ROLL && !drum_track(t) && !detail_on()) {   /* the piano roll: its tinted rows' scale ("C MIN") */
        str_cpy(ti, N_NOTE[(uint32_t)t->p[P_ROOT] % 12u], 4);
        str_cpy(ti + str_len(ti), " ", 4);
        str_cpy(ti + str_len(ti), N_SCALE[clamp(t->p[P_SCALE], 0, (int32_t)(sizeof N_SCALE / sizeof N_SCALE[0]) - 1)], 8);
    }
}
/* the strip's title: L centred (M when L lacks a glyph or is too wide) */
static void graph_title(void)
{
    char ti[20];
    const aafont_t *f = &AF_L;
    int32_t y;
    page_title(ti);
    if (!large_face_has(ti) || text_w(f, ti) > 220)
        f = &AF_M;
    y = f == &AF_L ? CAP_IN(L, LG_H_GRAPH) : CAP_IN(M, LG_H_GRAPH);
    GFX_HOOK_ALIGN(0, 0, 240, LG_H_GRAPH, AL_HV, "large strip title centred");
    cv_text_in(0, y, 240, f, ti, T_THEME, T_SURF);
}

static uint32_t graph_signature(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint32_t h = 2166136261u, i;
    if (ui.home) {
        if (ENGINES[t->eng_req % NENGINES] == &ENG_GRAIN) {
            gr_part_t *P = gr_of(t);
            uint32_t gi;
            h ^= sample_wave.pos * 13u + sample_wave.key;
            for (gi = 0; gi < GR_NG; gi++)
                if (P->g[gi].owner)
                    h ^= P->g[gi].pos + (P->g[gi].wph >> 16) * 31u;
            for (i = 0; i < 4; i++)
                h = (h ^ (uint32_t)t->p[ENG_GRAIN.knob[i]]) * 16777619u;
        } else if (ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE) {
            h ^= sample_wave.pos * 13u + sample_wave.key;
        }
        return h ^ (ui.frame / 2u);                  /* scope / grain / sample: redraw every other frame */
    }
    h ^= (uint32_t)pg->graph * 131u + TSEL->eng_req + song.sel * 7777u + ui.page * 1291u;
    if (strip_kind() == SK_TITLE)                    /* MENU > LARGE: the page's title (its engine, its number) */
        return h ^ 0x5A17u;
    if (pg->graph == GR_NONE) h ^= ui.frame / 2u;
    if (pg->graph == GR_ARP)                        /* redraw when the arp moves, not every other frame */
        h ^= (uint32_t)arp_viz_pos * 31u + (uint32_t)arp_viz_len * 7919u + (uint32_t)arp_viz_cur * 131u;
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint32_t)t->p[i]) * 16777619u;
    h ^= (uint32_t)TSEL->preset * 7u + (uint32_t)song.g[G_SLOT] * 13u + TSEL->user * 257u + up_gen * 7919u + ui.uslot * 104729u +
         ui.ppick * 1299709u;
    if (pg->graph == GR_CHORD) {                     /* the last chord played */
        h = (h ^ (chord_last[song.sel].root + 131u * chord_last[song.sel].mask)) * 16777619u;
        for (i = 0; i < CHORD_MAX; i++)
            h = (h ^ chord_last[song.sel].note[i]) * 16777619u;
        h ^= (uint32_t)t->engine * 389u;             /* (MONO and kits follow the sounding engine) */
    }
    if (pg->graph == GR_SONG) {
        h ^= ui.song_row * 40503u + chain_config.count * 7919u + ui.song_trk * 3571u;
        h = fnv(h, chain_config.row, sizeof chain_config.row);
        h ^= chain.running ? (chain.row + 1u) * 104729u + chain.remaining * 1299709u : 0u;
        for (i = 0; i < 4u; i++) h ^= (uint32_t)graph_project_used(i) << (24u + i);
        h += graph_pname_sig;
        {                                            /* the timeline: its view (following, ui_graph.c song_follow), the playhead */
            song_lay_t L;
            song_layout(&L);
            h = (h ^ (uint32_t)song_follow(&L)) * 16777619u;
            h = (h ^ (uint32_t)(song_playx(&L) + 1)) * 16777619u;
            h ^= (ui_prefs & PREF_LARGE) ? 0x2545F491u : 0u;
        }
    }
    if (pg->graph == GR_MOD) {
        uint32_t m = pg->id[0] % NMACROS;
        const macro_t *mc = &loom_macros[m];
        uint32_t d;
        h ^= (macro_ui_line + 1u) * 40503u + (m + 1u) * 7919u + mc->src * 131u + mc->val * 17u;
        for (d = 0; d < NMACRO_DESTS; d++) {
            h ^= (mc->dest[d].trk * 59u + mc->dest[d].dest * 31u + (uint32_t)(uint8_t)mc->dest[d].amt) * (d + 1u) * 101u;
        }
    }
    if (pg->graph == GR_MACRO) {
        h ^= 0x9E3779B9u;
        for (uint32_t m = 0; m < NMACROS; m++) {
            const macro_t *mc = &loom_macros[m];
            h ^= (m + 1u) * 7919u + mc->src * 131u + mc->val * 17u;
            for (uint32_t d = 0; d < NMACRO_DESTS; d++) {
                h ^= (mc->dest[d].trk * 59u + mc->dest[d].dest * 31u + (uint32_t)(uint8_t)mc->dest[d].amt) * (d + 1u) * 101u;
            }
        }
    }
    if (pg->graph == GR_SLCR && t->p[P_SLCR])        /* the SLICER's step playing */
        h ^= (sl[song.sel].idx + 1u) * 2654435761u;
    if (pg->graph == GR_SLOTS) {                     /* (a checksum over each slot) */
        for (i = 0; i < 4u; i++)
            h ^= (uint32_t)graph_project_used(i) << (20u + i);
        h += graph_pname_sig;
    }
    if (pg->graph == GR_EVENTS)                      /* AUTOMATION: the rows, the one selected, + ADD's place, PLAY,
                                                      * QUANTIZE */
        h = (h ^ ev_sig()) * 16777619u;
#if FELUCCA_SLICE
    if (pg->graph == GR_SLICES && slice_page_ok()) h ^= slice_sig();
#endif
    if (pg->scope == SC_ENGINE && (ENGINES[t->eng_req % NENGINES] == &ENG_WHEEL || t->eng_req % NENGINES == ENGI_FM6 ||
                                   (FELUCCA_FM4 && t->eng_req % NENGINES == ENGI_DIGITAL)))
        h ^= (ui.hot_t ? ui.hot_col + 1u : 0u) * 65537u;
    if ((pg->scope == SC_ENGINE || fop_page(pg->graph)) && t->eng_req % NENGINES == ENGI_FM6)   /* the patch (PAT's algorithm,
                                                     * levels, FB; the operator pages: the operator, the stage) */
        h ^= (fm6_pgen[(t - trk) % NTRK] + 1u) * 2246822519u + (fop_op + 1u) * 40503u + (fop_stg + 1u) * 7919u;
    if ((ui.home || pg->scope == SC_ENGINE) && ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE) {
        h ^= sample_wave.pos * 13u + sample_wave.key + (uint32_t)t->p[P_E5] * 7u + (uint32_t)t->p[P_E6] * 19u;
        for (uint32_t vi = 0; vi < NVOICE; vi++)
            if (t->v[vi].active) h ^= t->v[vi].ph[0] * 31u;
    }
    if ((ui.home || pg->scope == SC_ENGINE) && ENGINES[t->eng_req % NENGINES] == &ENG_SCRUB) {
        scrb_part_t *sp = scrb_of(t);
        if (sp) h ^= (uint32_t)(sp->pos >> 8) + ((uint32_t)sp->velocity << 4);
    }
#if FELUCCA_SLICE
    if ((ui.home || pg->scope == SC_ENGINE || pg->graph == GR_SLICES) && ENGINES[t->eng_req % NENGINES] == &ENG_SLICE) {
        for (uint32_t vi = 0; vi < NVOICE; vi++)
            if (t->v[vi].active) h ^= t->v[vi].ph[0] * 31u;
    }
#endif
    if (pg->graph == GR_SIDECHAIN) {
        h ^= (uint32_t)song.sc[0] * 31u + (uint32_t)song.sc[1] * 71u +
             (uint32_t)song.sc[2] * 127u + (uint32_t)song.sc[3] * 257u;
        h ^= (uint32_t)(fx_sidechain_env() >> 7) * 1009u;
    }
    if (pg->graph == GR_DELAY) {
        h ^= (uint32_t)song.g[G_DTIME] * 31u + (uint32_t)song.g[G_DFDBK] * 71u +
             (uint32_t)song.g[G_DCOLOR] * 127u + (uint32_t)song.g[G_DMIX] * 257u +
             (ui.frame / 2u);
    }
    if (pg->graph == GR_REVERB) {
        h ^= (uint32_t)song.g[G_RTYPE] * 31u + (uint32_t)song.g[G_RSIZE] * 71u +
             (uint32_t)song.g[G_RDAMP] * 127u + (uint32_t)song.g[G_RTONE] * 257u +
             (song.g[G_RTYPE] == 3 ? (ui.frame / 4u) : 0u);
    }
    if (pg->graph == GR_CHORUS) {
        h ^= (uint32_t)song.g[G_CRATE] * 31u + (uint32_t)song.g[G_CDEPTH] * 71u +
             (uint32_t)song.g[G_CTONE] * 127u + (uint32_t)song.g[G_CVOICE] * 257u +
             (ui.frame / 2u);
    }
    if ((ui.home || pg->scope == SC_ENGINE) && ENGINES[t->eng_req % NENGINES] == &ENG_WAVETABLE) {
        h ^= (uint32_t)t->p[P_E0] * 7u + (uint32_t)t->p[P_E1] * 13u + (uint32_t)t->p[P_E2] * 31u;
    }
    if ((ui.home || pg->scope == SC_ENGINE) && ENGINES[t->eng_req % NENGINES] == &ENG_GRAIN) {
        gr_part_t *P = gr_of(t);
        uint32_t gi;
        h ^= sample_wave.pos * 13u + sample_wave.key;
        for (gi = 0; gi < GR_NG; gi++)
            if (P->g[gi].owner)
                h ^= P->g[gi].pos + (P->g[gi].wph >> 16) * 31u;
    }
    if (pg->graph == GR_ROLL) {
        uint32_t ph = song.playing ? t->seq_idx : 0xFFFFu;
        if (ph / 16u != ui.bank)
            ph = 0xFFFFu;                            /* the roll shows the cursor's bank only */
        h ^= steps_hash(t) + ph * 31u + ui.cursor * 7919u + ui.lane * 104723u + ui.bank * 613u +
             drum_mute[trk_index(t) % NPART] * 2654435761u;
        {                                            /* the parameter locks' marks (the roll, the grid) */
            uint64_t lk = motion_lock_steps(trk_index(t));
            h = (h ^ (uint32_t)lk ^ (uint32_t)(lk >> 32) * 2654435761u) * 16777619u;
        }
        if (detail_on())                             /* DETAIL: QUANTIZE (NUDGE's DIM), the lane named in the accent */
            h = (h ^ (0xD1u + (uint32_t)t->p[P_SQNT] * 31u + (ui.hot_t ? ui.hot_col + 1u : 0u) * 977u)) * 2654435761u;
        else if (!drum_track(t)) {                   /* the roll: its view and the keys held */
            pr_follow(t);
            h = (h ^ (proll.lo + 1u)) * 16777619u;
            h = (h ^ pr_held()) * 16777619u;
        }
    }
    if (pg->graph == GR_STEPS) {
        uint32_t ph = song.playing ? t->seq_idx : 0xFFFFu;
        h ^= steps_hash(t) + ph * 31u + ui.cursor * 7919u + ui.lane * 104723u +
             drum_mute[trk_index(t) % NPART] * 2654435761u;
        {
            uint64_t lk = motion_lock_steps(trk_index(t));
            h = (h ^ (uint32_t)lk ^ (uint32_t)(lk >> 32) * 2654435761u) * 16777619u;
        }
    }
    return h;
}
/* 4-letter engine tags of the preset list */
static const char *eng_abbr(const char *name)
{
    static const char *const A[][2] = {{"ANALOG", "ANLG"},
                                       {"SUBTRACT", "SUBT"},
#if FELUCCA_FM4
                                       {"DIGITAL", "DGTL"},
#endif
                                       {"PHASE", "PHAS"}, {"LOFI", "LOFI"},
                                       {"SAMPLE", "SMPL"}, {"VOICE", "VOCL"}, {"TRIO", "TRIO"}, {"WHEEL", "WHEL"},
                                       {"GRAIN", "GRAN"}, {"PHYS", "PHYS"}, {"DRUM", "DRUM"},
                                       {"NOISE", "NOIS"}, {"FM6", "FM6"}, {"SLICE", "SLCE"},
                                       {"WAVETABLE", "WAVE"}};
    uint32_t i;
    for (i = 0; i < sizeof A / sizeof A[0]; i++)
        if (str_eq(name, A[i][0]))
            return A[i][1];
    return name;
}

/* a list row (17 px), in M as every list's (PRESETS, USER, PHRASES, PROJECT, AUTOMATION, MOD, the MENU): the selected
 * one a THEME bar with INK text; tag at x 14 (the widest, DRUM, to x 57), free text from x LIST_NX to x1 */
#define LIST_Y(k) (3 + 17 * (int32_t)(k))
#define LIST_TY CAP_IN(M, 16)                           /* a row's text: its capitals rows 2 .. 12 of the row */
#define LIST_NX 64
static void list_row(int32_t y, int sel, const char *tag, uint16_t tc, const char *name, uint16_t nc, int32_t x1)
{
    uint16_t bg = sel ? T_THEME : T_SURF;
    if (sel)
        cv_rrect(6, y, 228, 16, 4, T_THEME, T_SURF);
    GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
    cv_text_on(14, y + LIST_TY, &AF_M, tag, sel ? T_INK : tc, bg);
    if (name[0])
        GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
    cv_free_text(LIST_NX, y + LIST_TY, &AF_M, name, sel ? T_INK : nc, bg, x1 - LIST_NX);
}
/* an empty list: a title and a hint, centred */
static void note_line(int32_t y, const char *s, uint16_t fg)   /* centred S; "[K2] ADD PATTERN": a key hint */
{
    uint32_t n;
    int32_t id = kc_tag(s, &n);
    if (id < 0) {
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H, "panel note centred");
        cv_text_in(0, y, 240, &AF_S, s, fg, T_SURF);
        return;
    }
    s += n;
    while (*s == ' ')
        s++;
    GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "panel note hint centred");
    cv_text_on(cv_keycap(HALF_UP(240 - kh_ink((uint32_t)id, s)), y + 1, (uint32_t)id, T_KEY, T_INK, T_SURF) + KH_GAP, y,
               &AF_S, s, fg, T_SURF);
}
static void panel_note(const char *a, const char *b, const char *c)
{
    GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H, "panel note centred");
    cv_text_in(0, 30, 240, &AF_M, a, T_TEXT, T_SURF);
    if (b) note_line(60, b, T_MID);
    if (c) note_line(82, c, T_DIM);
}
/* the same title led by an icon (16 px, the accent's: the header message's, ui_draw.c draw_head) in the middle of
 * the panel: the title's capitals centred up and down in it, the icon's ink on them; across, the pair centred by
 * its ink */
#define PA_GAP 6                                        /* icon ink -> title ink (setup_title's) */
static void panel_alert(uint32_t id, const char *a)
{
    int32_t bi[4], bt[4], x, y = CAP_IN(M, (int32_t)graph_h());
    icon_ink(16, id, bi);
    text_ink(&AF_M, a, bt);
    x = HALF_UP(240 - (bi[2] - bi[0] + PA_GAP + bt[2] - bt[0])) - bi[0];
    GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "panel alert centred");
    GFX_HOOK_ALIGN(0, y + AF_M_CAP_Y, 0, y + AF_M_CAP_Y + AF_M_CAP_H, AL_V | AL_PASS, "panel alert icon on its title's line");
    cv_icon_in(x, y + AF_M_CAP_Y, 0, AF_M_CAP_H, 16, id, T_ACCENT, T_SURF);
    GFX_HOOK_ALIGN(0, 0, 0, (int32_t)graph_h(), AL_V | AL_PASS, "panel alert title in the panel's middle");
    cv_text_on(x + bi[2] + PA_GAP - bt[0], y, &AF_M, a, T_TEXT, T_SURF);
}

/* preset browser: the global list (every engine), the current one selected; tag DIM, name TEXT,
 * favourites starred (the accent), the selected row's suggested pattern at its right */
static void graph_browse(void)
{
    uint32_t total, cur = preset_pos(&total), e, k;
    int32_t row;
    if (!total) {
        char b[24];
        if (list_mode() == 1u) {
            panel_note("NO FAVORITES", "LIST ALL TO ADD SOUNDS", 0);
            return;
        }
        str_cpy(b, "NO ", sizeof b);                    /* a category with no sound (OTHER: tag your own) */
        str_cpy(b + 3, list_name(list_mode()), sizeof b - 3);
        str_cpy(b + str_len(b), " SOUNDS", sizeof b - str_len(b));
        panel_note(b, "[K4] ANOTHER LIST", "TAG YOURS WHEN SAVING");
        return;
    }
    for (row = -3; row <= 3; row++) {
        int32_t y = LIST_Y(row + 3), x1 = 212;
        char tag[6], nm[13], pt[4], pn[13];
        uint32_t index = preset_visible(cur, total, (uint32_t)(row + 3));
        int sel = index == cur;
        int32_t hint = sel ? preset_pat_hint() : -1;    /* the suggested pattern */
        if (index >= total) continue;
        e = preset_at(index, &k);
        if (e == NENGINES) {                             /* user preset: "U07" and its name */
            up_slot_label(tag, k);
            up_name(k, nm);
        } else {
            str_cpy(tag, eng_abbr(ENGINES[e]->name), sizeof tag);
            str_cpy(nm, ENGINES[e]->presets[k].name, sizeof nm);
        }
        if (hint >= 0) {
            pat_label((uint32_t)hint, pt, pn);
            x1 = 206 - text_w(&AF_M, pt) - 6;
        }
        list_row(y, sel, tag, T_DIM, nm, T_TEXT, x1);
        if (hint >= 0) {
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_r(206, y + LIST_TY, &AF_M, pt, T_INK, T_THEME);
        }
        if (favorite_has(e, k)) {
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row star centred up/down");
            cv_icon_in(214, y, 0, 16, 12, ICON_X_STAR, sel ? T_INK : T_ACCENT, sel ? T_THEME : T_SURF);
        }
    }
}
/* the EDIT layer (ui_layer.c): the sound loaded, as the browser's selected row: "03" (its place in KNOB 2's list,
 * the engine's sounds) or "U07", the name, the star of a favourite */
static void engine_sound_row(int32_t y)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    uint32_t total, cur = eng_list_pos(&total), u = user_of(TSEL);
    char tag[6], nm[13];
    int fav = preset_favorite();
    if (u < UP_SLOTS) {
        up_slot_label(tag, u);
        up_name(u, nm);
    } else {
        tag[0] = (char)('0' + (cur + 1u) / 10u % 10u);
        tag[1] = (char)('0' + (cur + 1u) % 10u);
        tag[2] = 0;
        str_cpy(nm, e->npresets ? e->presets[TSEL->preset % e->npresets].name : "", sizeof nm);
    }
    list_row(y, 1, tag, T_DIM, nm, T_TEXT, fav ? 212 : 232);
    if (fav) {
        GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row star centred up/down");
        cv_icon_in(214, y, 0, 16, 12, ICON_X_STAR, T_INK, T_THEME);
    }
    (void)total;
}
/* user preset slots around the selected one: "U07  NAME" / EMPTY */
static void graph_user(void)
{
    int32_t row, first = clamp((int32_t)ui.uslot - 3, 0, UP_SLOTS - 7);
    for (row = 0; row < 7; row++) {
        uint32_t k = (uint32_t)(first + row);
        char tag[4], nm[13];
        int used = up_used(k);
        up_slot_label(tag, k);
        if (used)
            up_name(k, nm);
        else
            str_cpy(nm, "--", sizeof nm);
        list_row(LIST_Y(row), k == ui.uslot, tag, T_MID, nm, used ? T_TEXT : T_DIM, 232);
    }
}
/* SAVE > PHRASES: the pattern list around the one picked ("03  MELODY", "U07  MY BASS") */
static void graph_pats(void)
{
    uint32_t n = pat_count(), cur = pat_pick();
    int32_t row, first = clamp((int32_t)cur - 3, 0, (int32_t)n > 7 ? (int32_t)n - 7 : 0);
    for (row = 0; row < 7 && (uint32_t)(first + row) < n; row++) {
        uint32_t k = (uint32_t)(first + row);
        char tag[4], nm[13];
        pat_label(k, tag, nm);
        list_row(LIST_Y(row), k == cur, tag, T_MID, nm, T_TEXT, 232);
    }
}
/* SAVE > TOOLS: explains the picked action, or prompts to pick one */
static void graph_tools(void)
{
    uint32_t c = act_col();
    if (c == 1u)
        panel_note("CLEAR SEQUENCE", "[OCT+] CLEAR STEPS", "ERASES NOTES ON ACTIVE TRACK");
    else if (c == 2u)
        panel_note("INITIALIZE SOUND", "[OCT+] RESET SOUND", "RESTORES ENGINE DEFAULT SOUND");
    else if (c == 3u)
        panel_note("SAVE AS BOOT INIT", "[OCT+] SAVE INIT", "SAVES SESSION AS STARTUP STATE");
    else if (c == 4u)
        panel_note("RESET BOOT INIT", "[OCT+] FACTORY RESET", "RESTORES CLEAN FACTORY STARTUP");
    else
        panel_note("SESSION TOOLS", "TURN KNOB 1-4 TO PICK", "[OCT+] EXECUTE SELECTED ACTION");
}
/* the AUTOMATION rows' columns (M): the kind at 14 (LOCK AUTO STEP PLAY, to x 54), the step / PLAY's ON at EV_SX, the
 * name / QUANTIZE's ON at EV_NX (QUANTIZE ends at x 86), the value right at 226 */
#define EV_SX 60
#define EV_NX 92
/* a row's value (M) and its unit (S, on the value's baseline, as a card's) ending at x 226; the value's left x */
static int32_t ev_value(int32_t y, const char *v, const char *unit, uint16_t fg, uint16_t bg)
{
    int32_t x = 226;
    GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
    if (unit && unit[0]) {
        GFX_HOOK_ALIGN(0, 0, 0, y + LIST_TY + AF_M.asc, AL_B, "list row unit on its value's baseline");
        x = cv_text_r(x, y + LIST_TY + AF_M.asc - AF_S.asc, &AF_S, unit, fg, bg) - 2;
    }
    return cv_text_r(x, y + LIST_TY, &AF_M, v, fg, bg);
}
/* SEQ > AUTOMATION (ui_events.c): the track's rows around the one selected: PLAY first (ON / OFF and the counts),
 * QUANTIZE (ON / OFF and the steps nudged), then a row each: the kind (LOCK in the accent, AUTO; STEP: a step's CHANCE,
 * RATCH, NUDGE or VEL, a NUDGE or VEL that does not play DIM), the step, the name, the value at the right; the last row
 * + ADD (LOCK, CHANCE, RATCH, NUDGE or VEL: what OCT+ adds) */
static void graph_events(void)
{
    const track_t *t = TSEL;
    uint16_t rw[EV_ROWS];
    uint32_t n = ev_list(rw), cur = ev_row(n), k;
    int32_t row, first = clamp((int32_t)cur - 3, 0, (int32_t)n > 7 ? (int32_t)n - 7 : 0);
    for (row = 0; row < 7 && (uint32_t)(first + row) < n; row++) {
        int32_t y = LIST_Y(row);
        int sel;
        uint16_t bg, c;
        k = (uint32_t)(first + row);
        c = rw[k];
        sel = k == cur;
        bg = sel ? T_THEME : T_SURF;
        if (sel)
            cv_rrect(6, y, 228, 16, 4, T_THEME, T_SURF);
        if (c >> 8 == EVK_ADD) {
            ev_fix();
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(14, y + LIST_TY, &AF_M, ui.ev_id == EV_CHANCE ? "+ ADD CHANCE" : ui.ev_id == EV_RATCH ?
                       "+ ADD RATCH" : ui.ev_id == EV_NUDGE ? "+ ADD NUDGE" : ui.ev_id == EV_VEL ?
                       (ui.ev_vel ? "+ HIDE VEL" : "+ SHOW VEL") : "+ ADD LOCK", sel ? T_INK : T_DIM, bg);
            break;
        }
        if (c >> 8 == EVK_TOP) {                     /* PLAY ON / OFF, the counts: "EVENT 3  LOCK 2" */
            char b[24];
            str_cpy(b, "EVENT ", sizeof b);
            fmt_int(b + str_len(b), (int32_t)(motion_count(t) - motion_lock_count(t)));
            str_cpy(b + str_len(b), "  LOCK ", sizeof b - str_len(b));
            fmt_int(b + str_len(b), (int32_t)motion_lock_count(t));
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(14, y + LIST_TY, &AF_M, "PLAY", sel ? T_INK : T_MID, bg);
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(EV_SX, y + LIST_TY, &AF_M, motion_enabled(t) ? "ON" : "OFF",
                       sel ? T_INK : motion_enabled(t) ? T_TEXT : T_DIM, bg);
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_r(226, y + LIST_TY, &AF_M, b, sel ? T_INK : T_MID, bg);
            continue;
        }
        if (c >> 8 == EVK_QNT) {                     /* QUANTIZE ON / OFF (1.2), the steps nudged: "NUDGED 3" */
            char b[16];
            str_cpy(b, "NUDGED ", sizeof b);
            fmt_int(b + str_len(b), (int32_t)ev_nudged(t));
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(14, y + LIST_TY, &AF_M, "QUANTIZE", sel ? T_INK : T_MID, bg);
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(EV_NX, y + LIST_TY, &AF_M, t->p[P_SQNT] ? "ON" : "OFF", sel ? T_INK : T_TEXT, bg);
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_r(226, y + LIST_TY, &AF_M, b, sel ? T_INK : T_MID, bg);
            continue;
        }
        if (c >> 8 != EVK_REC) {                     /* a step's CHANCE / RATCH / NUDGE: "STEP 05 CHANCE  50%" */
            uint32_t s = c & 0xFFu, kd = c >> 8;
            int32_t v = ev_sval(&t->step[s], kd);
            uint16_t vc = (kd == EVK_NUDGE && !ev_nudge_plays(t, &t->step[s])) ||   /* (QUANTIZE ON ..) */
                          (kd == EVK_VEL && !ev_vel_plays(&t->step[s])) ? T_DIM : T_TEXT;
            char b[8];
            b[0] = (char)('0' + (s + 1u) / 10u);
            b[1] = (char)('0' + (s + 1u) % 10u);
            b[2] = 0;
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(14, y + LIST_TY, &AF_M, "STEP", sel ? T_INK : T_MID, bg);
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(EV_SX, y + LIST_TY, &AF_M, b, sel ? T_INK : T_MID, bg);
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(EV_NX, y + LIST_TY, &AF_M, kd == EVK_CHANCE ? "CHANCE" : kd == EVK_RATCH ? "RATCH" :
                       kd == EVK_VEL ? "VEL" : "NUDGE", sel ? T_INK : vc, bg);
            if (kd == EVK_CHANCE || kd == EVK_VEL) {
                fmt_int(b, v);
            } else if (kd == EVK_RATCH) {
                b[0] = 'x';
                b[1] = (char)('0' + v);
                b[2] = 0;
            } else {                                 /* "+3" "/16" */
                ev_nudge_fmt(b, v);
            }
            ev_value(y, b, kd == EVK_CHANCE ? "%" : kd == EVK_NUDGE ? "/16" : "", sel ? T_INK : vc, bg);
            continue;
        }
        {
            const motion_event_t *e = &motion.event[c & 0xFFu];
            uint32_t id = MOTION_ID(e), lock = (e->param & MOTION_LOCK) != 0u;
            char b[16], nm[12];
            const char *unit;
            const param_desc_t *d = track_desc(t, id);
            int32_t x;
            b[0] = (char)('0' + ((e->place & 63u) + 1u) / 10u);
            b[1] = (char)('0' + ((e->place & 63u) + 1u) % 10u);
            b[2] = 0;
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(14, y + LIST_TY, &AF_M, lock ? "LOCK" : "AUTO", sel ? T_INK : lock ? T_ACCENT : T_MID, bg);
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_text_on(EV_SX, y + LIST_TY, &AF_M, b, sel ? T_INK : T_MID, bg);
            param_format(d, e->value, b, &unit);
            ev_name(t, id, nm);
            x = ev_value(y, b, unit, sel ? T_INK : T_TEXT, bg);
            GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
            cv_free_text(EV_NX, y + LIST_TY, &AF_M, nm, sel ? T_INK : T_TEXT, bg, x - 8 - EV_NX);
        }
    }
}
/* project slots: the name (none: USED) / EMPTY, the selected one filled */
static void graph_slots(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t y = 12 + (int32_t)i * 26;
        char b[4];
        int sel = (int32_t)i + 1 == song.g[G_SLOT], used = graph_project_used(i);
        const char *n = graph_project_name(i);
        b[0] = (char)('A' + i);
        b[1] = 0;
        list_row(y, sel, b, T_MID, !used ? "--" : n[0] ? n : "USED", used ? T_TEXT : T_DIM, 232);
    }
}
/* MIXER page: four SURF columns, one under each card: the track number on its cushion (in the accent:
 * the selected track) with a REC / ARM / MUTE badge (P_MUTE, KNOB 1), the sound's short name (a MUTE badge
 * when armed and muted), the LEVEL knob (dB inside) with the output meter beside it, then the PAN and REV
 * knobs with their values. Knobs: a 270 degree ring, the track RAISE, the value arc THEME (the knob just
 * turned: ACCENT; muted: DIM); PAN from 12 o'clock. Each column is its own canvas with its own signature:
 * while the transport runs only the meters move. */
#define TS_MY 39                                     /* meter slot: column rows 39 .. 70 */
#define TS_MH 32
#define KB_X 4                                       /* LEVEL knob: box 4..37 x 37..70, centre (21, 54) */
#define KB_Y 37
#define KS_Y 86                                      /* PAN / REV knobs: boxes 86..105, centres x 15 and 42 */
static struct {
    uint32_t col[NTRK];
    uint8_t meter[NTRK];
} ts;

static int32_t meter_px(int32_t a)                   /* |sample| (Q15) -> px: 60 dB over the slot */
{
    int32_t lg = 0, v;
    if (a < 64)
        return 0;
    while ((a >> lg) > 1)
        lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);             /* 8 log2(a): 48 (-54 dB) .. 128 (+6 dB) */
    return clamp((v - 48) * (TS_MH - 2) / 80, 0, TS_MH - 2);
}

/* a knob's ring: 270 degrees (7:30 .. 4:30 o'clock), 2 px, anti-aliased, from the quadrant mask of its size
 * (tools/gen_aa_keycaps.py: coverage and angle; the other three quadrants mirror it, no trigonometry here).
 * Box top-left (x, y), outer radius r. Angles: 1/1024 turn from 12 o'clock, clockwise, -384 .. 384; the value
 * arc is lo .. hi (in vc), the rest of the ring the track (tr); bg lies under the ring */
#define KA_END 384
static void knob_arc(int32_t x, int32_t y, int32_t r, const uint8_t *cov, const uint8_t *ang, int32_t lo, int32_t hi,
                     uint16_t tr, uint16_t vc, uint16_t bg)
{
    const uint16_t *rt = ramp(tr, bg), *rv = ramp(vc, bg);
    int32_t i, j, m;
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t k = (uint32_t)(j * r + i), a = (cov[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
            int32_t q = ang[k];
            if (!a)
                continue;
            for (m = 0; m < 4; m++) {                /* top right, bottom right, bottom left, top left */
                int32_t s = m == 0 ? q : m == 1 ? 512 - q : m == 2 ? q - 512 : -q;
                int32_t px = m < 2 ? x + r + i : x + r - 1 - i, py = (m == 0 || m == 3 ? y + r - 1 - j : y + r + j) + cv_oy;
                if (s < -KA_END || s > KA_END || (uint32_t)px >= cv_w || (uint32_t)py >= cv_h)
                    continue;
                cv_px[(uint32_t)py * cv_w + (uint32_t)px] = (s >= lo && s <= hi ? rv : rt)[a];
            }
        }
}
/* value v of lo..hi as an arc: from the start, or (bipolar, lo < 0) from 12 o'clock with a 1 px nub at 0 */
static void knob(int32_t x, int32_t y, int32_t r, const uint8_t *cov, const uint8_t *ang, int32_t v, int32_t lo,
                 int32_t hi, uint16_t vc)
{
    int32_t a0, a1;
    if (lo < 0) {
        int32_t s = v * KA_END / (v < 0 ? -lo : hi);
        a0 = (s < 0 ? s : 0) - 8;
        a1 = (s > 0 ? s : 0) + 8;
    } else {
        a0 = -KA_END;
        a1 = v > lo ? -KA_END + (v - lo) * 2 * KA_END / (hi - lo) : -KA_END - 1;
    }
    knob_arc(x, y, r, cov, ang, a0, a1, T_RAISE, vc, T_SURF);
}

static uint32_t trk_level(uint32_t c) { return (uint32_t)trk[c].p[P_LEVEL] & 127u; }   /* LEVEL 0..127 */

static void trk_short_name(uint32_t c, char *b)      /* the track's sound, b holds 13 */
{
    const track_t *t = &trk[c];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    if (e == &ENG_DRUM) {
        int32_t kit = clamp(t->p[P_E0], 0, DK_COUNT - 1);
        str_cpy(b, N_DRUM_KIT[kit], 13);
    } else if (e == &ENG_WAVETABLE) {
        str_cpy(b, "WAVE", 13);
    } else if (e == &ENG_ANALOG) {
        str_cpy(b, "SUB", 13);
    } else if (e == &ENG_SAMPLE) {
        str_cpy(b, "SMPL", 13);
    } else if (e == &ENG_FORMANT) {
        str_cpy(b, "VOICE", 13);
    } else {
        str_cpy(b, e->name, 13);
    }
    if (b[0] == 'A' && b[1] == 'N' && (b[2] == 'A' || b[2] == 'L')) {
        str_cpy(b, "ANLG", 13);
    }
}

/* MENU > LARGE: the four tracks in the strip under the tall cards (57 x LG_H_GRAPH each): the cushion (the selected
 * one the accent), REC / ARM / MUTE as on the full mixer, the sound's name, LEVEL as a gauge (THEME: the cards'),
 * the output meter under it (MID); a muted track DIM */
#define TRK_COL_W 45
#define TRK_COL_X(c) ((c) < 4 ? (2 + 47 * (int32_t)(c)) : 193)
#define TSS_GY 40                                    /* the LEVEL gauge, 3 px */
#define TSS_MY 48                                    /* the meter, 3 px */
static void track_strip(uint32_t c, uint32_t sel, uint32_t st, uint32_t mute, uint32_t arm, uint32_t hot, uint32_t lvl,
                        const char *b)
{
    int32_t gw = TRK_COL_W - 8, m = ts.meter[c] * gw / (TS_MH - 2), fx = (int32_t)lvl * gw / 127;
    uint8_t eng_s = eng_idx(trk[c].eng_req);
    uint16_t tc_s = (!ux.mono && !ux.light && eng_s < NENGINES) ? ENG_NEON[eng_s].theme : T_THEME;
    uint16_t vc = mute ? T_DIM : tc_s;
    cv_begin(TRK_COL_W, LG_H_GRAPH, T_BG);
    cv_rrect(0, 0, TRK_COL_W, LG_H_GRAPH, 5, T_SURF, T_BG);
    cv_icon_on(3, 4, 16, trk_icon(c, sel), sel ? T_ACCENT : tc_s, T_SURF);
    if (st)
        GFX_HOOK_ALIGN(0, 4, 0, 20, AL_V, "mixer badge on the cushion's line");
    if (st == 1u || st == 2u)
        cv_keycap(TRK_COL_W - 4 - kc_w(st == 1u ? KC_REC : KC_ARM), 5, st == 1u ? KC_REC : KC_ARM, st == 1u ? T_REC : T_ACCENT,
                  T_INK, T_SURF);
    else if (st)
        cv_keycap(TRK_COL_W - 4 - kc_w(KC_MUTE), 5, KC_MUTE, hot == 4u ? T_ACCENT : T_KEY, T_INK, T_SURF);
    if (mute && arm)                                 /* armed and muted: MUTE in place of the name */
        GFX_HOOK_ALIGN(0, 22 + AF_S_CAP_Y, 0, 22 + AF_S_CAP_Y + AF_S_CAP_H, AL_V, "mixer MUTE badge on the name's line");
    if (mute && arm)
        cv_keycap(3, 22 + AF_S_CAP_Y + HALF_UP(AF_S_CAP_H - KC_H), KC_MUTE, hot == 4u ? T_ACCENT : T_KEY, T_INK, T_SURF);
    else
        cv_free_text(3, 22, &AF_S, b, mute ? T_DIM : sel ? T_TEXT : T_MID, T_SURF, TRK_COL_W - 6);
    cv_rrect(4, TSS_GY, gw, 3, 1, ux.style ? T_LINE : T_BG, T_SURF);
    cv_rrect(4, TSS_GY, fx < 3 ? 3 : fx, 3, 1, hot == 1u ? T_ACCENT : vc, T_BG);
    cv_rrect(4, TSS_MY, gw, 3, 1, T_RAISE, T_SURF);
    if (m)
        cv_rrect(4, TSS_MY, m < 3 ? 3 : m, 3, 1, T_MID, T_RAISE);
    cv_blit((uint32_t)TRK_COL_X(c), LG_Y_GRAPH);
}
static void draw_tracks(void)
{
    uint32_t c, sk = strip_kind() == SK_TRK;
    if (ui.force) {
        lcd_fill(0, graph_y(), 240, graph_h(), T_BG);
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
    }
    /* Visible vertical divider line between tracks 1-4 and drum track */
    lcd_fill(190, graph_y() + 4, 1, graph_h() - 8, T_DIM);
    for (c = 0; c < NTRK; c++) {
        track_t *t = &trk[c];
        uint32_t sel = c == song.sel, lvl = trk_level(c), mute = t->p[P_MUTE] != 0;
        uint32_t arm = (song.rec >> c) & 1u, st = arm ? (song.playing ? 1u : 2u) : mute ? 3u : 0u, sig;
        uint32_t hot = sel && ui.hot_t ? ui.hot_col + 1u : 0u;
        int32_t pk = t->peak, m, pan = clamp(t->p[P_PAN], -64, 63), rv = clamp(t->p[P_REV], 0, 127);
        uint8_t eng = eng_idx(trk[c].eng_req);
        uint16_t tc = (!ux.mono && !ux.light && eng < NENGINES) ? ENG_NEON[eng].theme : T_THEME;
        uint16_t vc = mute ? T_DIM : tc;
        char b[16];
        t->peak = 0;
        trk_short_name(c, b);
        m = mute ? 0 : meter_px(pk);
        if (m < ts.meter[c] - 1)
            m = ts.meter[c] - 1;                     /* falls ~2 dB a frame */
        ts.meter[c] = (uint8_t)(m < 0 ? 0 : m);
        sig = str_hash(1u + sel + st * 2u + (mute && arm) * 16u + hot * 32u, b) + lvl * 7919u +
              ts.meter[c] * 131u + (uint32_t)(pan + 128) * 104729u + (uint32_t)rv * 1299709u + mute * 3u + sk * 0x9E37u;
        if (!ui.force && sig == ts.col[c])
            continue;
        ts.col[c] = sig;
        if (sk) {
            track_strip(c, sel, st, mute, arm, hot, lvl, b);
            continue;
        }
        cv_begin(TRK_COL_W, H_GRAPH, T_BG);
        cv_rrect(0, 0, TRK_COL_W, H_GRAPH, 5, T_SURF, T_BG);
        cv_icon_on(3, 5, 16, trk_icon(c, sel), sel ? T_ACCENT : tc, T_SURF);
        if (st)
            GFX_HOOK_ALIGN(0, 5, 0, 21, AL_V, "mixer badge on the cushion's line");
        if (st == 1u || st == 2u)
            cv_keycap(TRK_COL_W - 4 - kc_w(st == 1u ? KC_REC : KC_ARM), 6, st == 1u ? KC_REC : KC_ARM, st == 1u ? T_REC : T_ACCENT,
                      T_INK, T_SURF);
        else if (st)
            cv_keycap(TRK_COL_W - 4 - kc_w(KC_MUTE), 6, KC_MUTE, hot == 4u ? T_ACCENT : T_KEY, T_INK, T_SURF);
        if (mute && arm)
            cv_keycap(3, 21 + AF_S_CAP_Y + HALF_UP(AF_S_CAP_H - KC_H), KC_MUTE, hot == 4u ? T_ACCENT : T_KEY, T_INK, T_SURF);
        else
            cv_free_text(3, 21, &AF_S, b, mute ? T_DIM : sel ? T_TEXT : T_MID, T_SURF, TRK_COL_W - 6);
        {   /* LEVEL */
            char v[8];
            knob(6, KB_Y, KNOB_BIG_R, KNOB_BIG_COV, KNOB_BIG_ANG, (int32_t)lvl, 0, 127, hot == 1u ? T_ACCENT : vc);
            if (lvl) {
                int32_t d = LEVEL_DB_X10[lvl];
                fmt_int(v, (d + (d < 0 ? -5 : 5)) / 10);
                cv_text_in(6, KB_Y + KNOB_BIG_R + 5, 2 * KNOB_BIG_R, &AF_S, "dB", T_DIM, T_SURF);
            } else {
                str_cpy(v, "OFF", sizeof v);
            }
            cv_text_in(6, KB_Y + CAP_IN(S, 2 * KNOB_BIG_R), 2 * KNOB_BIG_R, &AF_S, v, hot == 1u ? T_ACCENT : lvl ? vc : T_DIM, T_SURF);
            cv_rrect(39, TS_MY, 3, TS_MH, 1, T_RAISE, T_SURF);
            if (ts.meter[c])
                cv_rrect(39, TS_MY + TS_MH - 1 - ts.meter[c], 3, ts.meter[c] + 1, ts.meter[c] >= 4 ? 1 : 0, T_MID, T_RAISE);
        }
        {   /* PAN & REV */
            char v[8];
            uint16_t pc = hot == 2u ? T_ACCENT : vc, rc = hot == 3u ? T_ACCENT : vc;
            cv_text_in(11 - KNOB_SMALL_R, 72, 2 * KNOB_SMALL_R, &AF_S, "PAN", T_DIM, T_SURF);
            cv_text_in(33 - KNOB_SMALL_R, 72, 2 * KNOB_SMALL_R, &AF_S, "REV", T_DIM, T_SURF);
            knob(11 - KNOB_SMALL_R, KS_Y, KNOB_SMALL_R, KNOB_SMALL_COV, KNOB_SMALL_ANG, pan, -64, 63, pc);
            knob(33 - KNOB_SMALL_R, KS_Y, KNOB_SMALL_R, KNOB_SMALL_COV, KNOB_SMALL_ANG, rv, 0, 127, rc);
            v[0] = '+';
            fmt_int(pan > 0 ? v + 1 : v, pan * 100 / 64);
            cv_text_in(11 - KNOB_SMALL_R, KS_Y + 17, 2 * KNOB_SMALL_R, &AF_S, v, pc, T_SURF);
            fmt_int(v, (rv * 100 + 63) / 127);
            cv_text_in(33 - KNOB_SMALL_R, KS_Y + 17, 2 * KNOB_SMALL_R, &AF_S, v, rc, T_SURF);
        }
        cv_blit((uint32_t)TRK_COL_X(c), Y_GRAPH);
    }
}
/* HOME > TRACKS (1.3, Discussions #112 / #134: MENU > DISPLAY > HOME TRACKS): the four tracks as rows in the panel, one
 * under the other (29 px, 2 apart), in place of the scope; the cards above stay HOME's four (the selected track's). A row:
 * the track's cushion at the left; on its first line the engine's icon and the sound's name (MENU > LARGE: the name in M,
 * no icon), a MUTE badge at its right when muted; under it the 16 steps of the page of the track's pattern that plays
 * (stopped: the selected track's cursor page, the others' first): a note a bar (accented: taller, the accent), a TIE after
 * a note the bar carried on from the one before, DRUM hits a pad (two hits or more: two pads), an empty step or a REST a
 * stub (a TIE with nothing to carry: brighter); the step playing on a raised cell. LEN > 16: a pip per page right of the
 * steps (the page shown THEME). At the right the track's output meter. The selected track's row is the selection (THEME,
 * its ink INK, as a list's selected row); a muted track's steps DIM. Each row is its own canvas with its own signature
 * (ts.col, the MIXER's: the two never show together); a meter that moved alone redraws its slot only. LINE: the rows on
 * BG, 1 px rules between them */
#define TR_H 22                                      /* a row; rows TR_PITCH apart from Y_GRAPH, 3 .. 237 across */
#define TR_PITCH 24
#define TR_W 234
#define TR_NY(lg) ((lg) ? 1 : 2)                     /* the name line's capitals from row 2 (LARGE, M: 1) */
#define TR_SB 20                                     /* the steps: bars standing on row 20 */
#define TR_BW 9                                      /* a step's bar, 11 px apart, 4 more between groups of 4 */
#define TR_PX 216                                    /* the page pips */
#define TR_MX 226                                    /* the meter: 4 x 16 from row 3 */
#define TR_MY 3
#define TR_MH 16
static int32_t tr_bx(uint32_t i) { return 24 + (int32_t)i * 11 + (int32_t)(i / 4u) * 4; }
static void tr_meter(int32_t x, int32_t y, uint32_t c, uint16_t slot, uint16_t fill, uint16_t under)
{
    int32_t m = ts.meter[c] * TR_MH / (TS_MH - 2);
    cv_rrect(x, y, 4, TR_MH, 2, slot, under);
    if (m)
        cv_rrect(x, y + TR_MH - m, 4, m, m >= 4 ? 2 : 0, fill, slot);
}
static void home_track_row(uint32_t c)
{
    track_t *t = &trk[c];
    const step_t *s = seq_steps(t);
    uint32_t sel = c == song.sel, mute = t->p[P_MUTE] != 0, large = (ui_prefs & PREF_LARGE) != 0u;
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), np = (len + 15u) / 16u, pg, p0, ph, i, sig, carry = 0;
    int32_t y = Y_GRAPH + (int32_t)c * TR_PITCH, m, x, xr = 222, ch = large ? AF_M_CAP_H : AF_S_CAP_H;
    uint16_t bg = sel ? T_THEME : T_SURF, half = ux_mix(T_THEME, T_INK, 35);
    uint16_t cn = sel ? (mute ? ux_mix(T_THEME, T_INK, 55) : T_INK) : mute ? T_DIM : T_THEME;   /* a note */
    uint16_t ca = sel || mute ? cn : T_ACCENT, cs = sel ? half : T_RAISE;   /* accented; a stub */
    uint16_t cp = sel ? ux_mix(T_THEME, T_INK, 20) : T_RAISE, cpl = sel ? T_INK : mute ? T_MID : T_TEXT;   /* playing */
    char b[16];
    pg = song.playing ? t->seq_idx / 16u : sel ? ui.cursor / 16u : 0u;
    if (pg >= np)
        pg = np - 1u;
    p0 = pg * 16u;
    ph = song.playing && t->seq_idx / 16u == pg ? t->seq_idx % 16u : 0xFFu;
    trk_short_name(c, b);
    m = mute ? 0 : meter_px(t->peak);
    t->peak = 0;
    if (m < ts.meter[c] - 1)
        m = ts.meter[c] - 1;                         /* (falls as the MIXER's) */
    sig = str_hash(1u + sel + mute * 2u + large * 4u + t->eng_req * 8u, b) + steps_hash(t) * 31u + pg * 7919u +
          ph * 104729u + len * 1299709u;
    if (!ui.force && sig == ts.col[c]) {
        if (m != ts.meter[c]) {                      /* the meter alone: its slot */
            ts.meter[c] = (uint8_t)m;
            cv_begin(4, TR_MH, bg);
            tr_meter(0, 0, c, cs, sel ? T_INK : T_MID, bg);
            cv_blit(3 + TR_MX, (uint32_t)(y + TR_MY));
        }
        return;
    }
    ts.col[c] = sig;
    ts.meter[c] = (uint8_t)m;
    cv_begin(TR_W, TR_H, T_BG);
    cv_rrect(0, 0, TR_W, TR_H, 3, bg, T_BG);
    uint8_t eng_r = eng_idx(trk[c].eng_req);
    uint16_t tc_r = (!ux.mono && !ux.light && eng_r < NENGINES) ? ENG_NEON[eng_r].theme : T_MID;
    cv_icon_mid(4, TR_H / 2, 16, trk_icon(c, 1), sel ? T_INK : mute ? T_DIM : tc_r, bg);
    if (mute) {                                      /* MUTE: its badge at the name line's end */
        xr -= kc_w(KC_MUTE);
        GFX_HOOK_ALIGN(0, TR_NY(large), 0, TR_NY(large) + ch, AL_V, "tracks row badge on the name's line");
        cv_keycap(xr, TR_NY(large) + (ch - KC_H) / 2, KC_MUTE, sel ? T_INK : T_KEY, sel ? T_THEME : T_INK, bg);
        xr -= 6;
    }
    x = 24;
    if (FELUCCA_ICONS && !large) {                   /* the engine's icon on the name's capitals */
        uint32_t ei = engine_icon(ENGINES[t->eng_req % NENGINES]->name);
        if (ei != ICON_NONE)
            GFX_HOOK_ALIGN(0, TR_NY(large), 0, TR_NY(large) + ch, AL_V, "tracks row icon on the name's line");
        x += cv_icon_in(x, TR_NY(large), 0, ch, 12, ei, sel ? T_INK : T_MID, bg) + 5;
    }
    cv_free_text(x, TR_NY(large) - (large ? AF_M_CAP_Y : AF_S_CAP_Y), large ? &AF_M : &AF_S, b, sel ? T_INK : mute ? T_DIM : T_TEXT,
                 bg, xr - x);
    for (i = p0; i-- > 0u;)                          /* a TIE at the page's start: carried from a note before it? */
        if (s[i].time != ST_TIE) {
            carry = s[i].time == ST_NOTE && s[i].n;
            break;
        }
    for (i = 0; i < 16u && p0 + i < len; i++) {
        const step_t *st = &s[p0 + i];
        int32_t bx = tr_bx(i);
        uint16_t under = i == ph ? cp : bg;
        if (i == ph)                                 /* the step playing: a raised cell */
            cv_rrect(bx - 1, TR_SB - 9, TR_BW + 2, 10, 1, cp, bg);
        if (st->time == ST_TIE && carry) {           /* carried on: joined to the bar before */
            int32_t x0 = i ? tr_bx(i - 1u) + TR_BW : bx - 2;
            cv_rect(x0, TR_SB - 5, bx + 2 - x0, 5, i == ph ? cpl : cn);
            cv_rrect(bx, TR_SB - 5, TR_BW, 5, 1, i == ph ? cpl : cn, under);
        } else if (st->time == ST_NOTE && st->n) {   /* notes: a bar, accented taller */
            int acc = (st->flags & SF_ACCENT) != 0u;
            int32_t bh = acc ? 7 : 5;
            cv_rrect(bx, TR_SB - bh, TR_BW, bh, 1, i == ph ? cpl : acc ? ca : cn, under);
            carry = 1;
            continue;
        } else if (st->time == ST_NOTE && st->hit) { /* DRUM hits: a pad, two for two hits or more */
            uint16_t pc = i == ph ? cpl : (st->acc & st->hit) ? ca : cn;
            cv_rrect(bx, TR_SB - 3, TR_BW, 3, 1, pc, under);
            if (st->hit & (st->hit - 1u))
                cv_rrect(bx, TR_SB - 7, TR_BW, 3, 1, pc, under);
        } else {                                     /* empty, a REST, a TIE carrying nothing */
            cv_rect(bx, TR_SB - 2, TR_BW, 2, i == ph ? (sel ? T_INK : T_MID) : st->time == ST_TIE ? (sel ? T_INK : T_MID) : cs);
        }
        carry = st->time == ST_TIE && carry;
    }
    if (np > 1u)                                     /* LEN > 16: the pages, top to bottom */
        for (i = 0; i < np; i++)
            cv_rect(TR_PX, TR_SB - 3 * (int32_t)(np - i) + 1, 3, 2, i == pg ? (sel ? T_INK : T_THEME) : cs);
    tr_meter(TR_MX, TR_MY, c, cs, sel ? T_INK : T_MID, bg);
    cv_blit(3, (uint32_t)y);
}
static void draw_home_tracks(void)
{
    uint32_t c;
    if (ui.force) {                                  /* the BG round the rows only, they cover the rest (#181) */
        lcd_fill(0, Y_GRAPH, 3, H_GRAPH, T_BG);
        lcd_fill(3 + TR_W, Y_GRAPH, 240 - 3 - TR_W, H_GRAPH, T_BG);
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
        for (c = 1; c < NTRK; c++) {                 /* the 2 rows between two rows; LINE: a rule on the first */
            uint32_t y = (uint32_t)(Y_GRAPH + (int32_t)c * TR_PITCH - 2);
            if (ux.style) {
                lcd_fill(3, y, TR_W, 1, T_RULE);
                GFX_HOOK_RULE(3, y, TR_W, 1);
            }
            lcd_fill(3, ux.style ? y + 1u : y, TR_W, ux.style ? 1u : 2u, T_BG);
        }
    }
    for (c = 0; c < NTRK; c++)
        home_track_row(c);
}
/* oscilloscope of the output, triggered on a rising zero crossing: a RAISE centre line, the trace 2 px (in silence
 * a flat line on it; MENU > SCREEN OFF keeps it from staying on the panel for hours) */
static void graph_scope(uint16_t c)
{
    static int16_t snap[SCOPE_N] __attribute__((section(".pool")));
    uint32_t w = scope_w, i, trig = 0;
    int32_t cy = (int32_t)cv_h / 2, py = cy, x, peak = 1500, a = cv_h == H_GRAPH ? 46 : cy - 6;   /* (LARGE: the strip) */
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak)
            peak = snap[i];
        else if (-snap[i] > peak)
            peak = -snap[i];
    }
    for (i = 1; i < SCOPE_N - 240u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) {
            trig = i;
            break;
        }
    cv_rect(PANEL_X0, cy, PANEL_W, 1, T_RAISE);
    for (x = 0; x < PANEL_W; x++) {
        int32_t y = cy - snap[trig + (uint32_t)x] * a / peak;    /* auto-scaled */
        if (x)
            cv_line_t(PANEL_X0 - 1 + x, py, PANEL_X0 + x, y, c, 2);
        py = y;
    }
}

/* SONG is a playing order of the four stored project patterns. Letters match
 * PROJECT A..D; loading a project still restores its sound, SONG borrows steps. */
/* SONG as a timeline (song_layout), four tall lanes (T1 at the top, each led by its track's cushion, the selected
 * lane's the accent's) over a thin bar ruler: in a lane, the sections that play the same slot one after the other
 * make one block (RAISE, the slot's letter top-left; a slot not saved only outlined, DIM). In a block the picture of
 * the track's pattern in that slot (project.c proj_density, read with the slot: graph_pden): a bar a cell, as tall as
 * the cell is busy (a mean over its steps: rests 0, ties 1, notes / hits 2, accents 3; an empty cell a faint
 * baseline), the pattern looped over the section as it plays and a notch between the section's passes (its REPS). A
 * silent section ("-") is an empty lane with a faint line. The sections' boundaries are faint lines across the lanes
 * (seen between the blocks). The cell selected (KNOB 1 the section, KNOB 2 the lane) is THEME, its bars and letter
 * INK (a slot not saved: outlined THEME 2 px; "-": a THEME outline in the gap); + ADD's slot dashed after the last
 * section. Playing: the playhead across the lanes down to the ruler (ACCENT; INK on THEME), the playing section's
 * blocks filled up to it (RAISE -> ACCENT). The ruler: a tick a bar, longer every 4 with the bar's number. Under it
 * one line (M: the selected cell's detail): the selected cell's project name (NOT SAVED, SILENT; on + ADD the hint, S as
 * every key hint) and, right, the song's length in bars where it fits. MENU > LARGE: the letters in M too (the ruler's
 * numbers stay S) */
#define SG_PAD 3                                       /* a block's inner margin: the letter's, the picture's */
#define SG_PH (SG_LH - 2 * SG_PAD)                     /* the picture's height (14 px: 3 levels) */
#define SG_TX 5                                        /* the lanes' cushions (16 px) */
#define SG_IY 119                                      /* the line under the ruler: its capitals end on row 118 */
static void sg_dash(int32_t bx, int32_t bw, int32_t lo, int32_t hi, uint16_t c)   /* + ADD: dashed, clipped lo .. hi */
{
    int32_t k;
    for (k = 0; k < bw; k++)
        if (!((k >> 1) & 1) && bx + k >= lo && bx + k < hi) {
            cv_rect(bx + k, SG_Y, 1, 1, c);
            cv_rect(bx + k, SG_Y + SG_H - 1, 1, 1, c);
        }
    for (k = 0; k < SG_H; k++)
        if (!((k >> 1) & 1)) {
            if (bx >= lo) cv_rect(bx, SG_Y + k, 1, 1, c);
            if (bx + bw <= hi) cv_rect(bx + bw - 1, SG_Y + k, 1, 1, c);
        }
}
/* an outline of w x h at x, y, n px thick, clipped to lo .. hi (its ends only where not clipped) */
static void sg_frame(int32_t bx, int32_t bw, int32_t y, int32_t h, uint32_t n, int32_t lo, int32_t hi, uint16_t c)
{
    int32_t x0 = bx > lo ? bx : lo, x1 = bx + bw < hi ? bx + bw : hi;
    uint32_t k;
    if (x1 <= x0)
        return;
    for (k = 0; k < n; k++) {
        cv_rect(x0, y + (int32_t)k, x1 - x0, 1, c);
        cv_rect(x0, y + h - 1 - (int32_t)k, x1 - x0, 1, c);
        if (x0 == bx) cv_rect(bx + (int32_t)k, y, 1, h, c);
        if (x1 == bx + bw) cv_rect(bx + bw - 1 - (int32_t)k, y, 1, h, c);
    }
}
/* one lane's drawing state: the screen spans of the cell selected and of the playing section's part played */
typedef struct { int32_t c0, c1, p0, p1; uint16_t pf; } sg_fill_t;
static uint16_t sg_fill_at(const sg_fill_t *F, int32_t x)   /* the block's fill under column x */
{
    return x >= F->c0 && x < F->c1 ? T_THEME : x >= F->p0 && x < F->p1 ? F->pf : T_RAISE;
}
/* the slot's letter at x (MENU > LARGE: M), its capitals SG_PAD under the lane's top; 0: no room before x1 */
static int32_t sg_letter(int32_t x, int32_t x1, int32_t y, uint32_t s, uint16_t ink, uint16_t bg)
{
    const aafont_t *f = (ui_prefs & PREF_LARGE) ? &AF_M : &AF_S;
    char a[2] = {(char)('A' + s), 0};
    int32_t w = text_w(f, a);
    if (x + w + SG_PAD > x1)
        return 0;
    cv_text_on(x, y + SG_PAD - (f == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y), f, a, ink, bg);
    return w;
}
/* the picture of slot s's track k over sections i .. j - 1 (columns x0 .. x1 on screen), but for the letters'
 * columns (skip0 .. skip1, skip2 .. skip3) */
static void sg_picture(const song_lay_t *L, int32_t off, uint32_t k, uint32_t i, uint32_t j, uint32_t s, int32_t x0,
                       int32_t x1, const sg_fill_t *F, const int32_t *skip)
{
    int32_t y = SG_LY(k), yb = y + SG_PAD + SG_PH, x;
    uint32_t q, plen = graph_plen[s & 3u][k], n = graph_pcell[s & 3u][k], den = graph_pden[s & 3u][k];
    if (!plen || !n)
        return;
    for (q = i; q < j; q++) {
        int32_t sx = SG_X0 + L->x[q] - off, wt = L->x[q + 1u] - L->x[q] - SG_GAP, a, b;
        uint32_t reps = chain_config.row[q].repeat ? chain_config.row[q].repeat : 1u, unit = song_unit(&chain_config.row[q]);
        uint32_t tot = reps * unit, pc = 0xFFFFFFFFu, pl = 0xFFFFFFFFu, pp = 0xFFFFFFFFu, gaps, notch;
        if (wt < 1)
            continue;
        a = sx > x0 ? sx : x0;
        b = q + 1u == j ? sx + wt : sx + wt + SG_GAP;  /* (inside a run: on to the next section) */
        b = b < x1 ? b : x1;
        gaps = sg_muldiv((uint32_t)wt, plen, tot * n) >= 3u;   /* a cell 3 px or more: a gap between two */
        notch = reps > 1u && (uint32_t)wt / reps >= 4u;        /* the passes far enough apart: a notch between */
        for (x = sx; x < b; x++) {
            uint32_t r = (uint32_t)(x - sx), u = sg_muldiv(r < (uint32_t)wt ? r : (uint32_t)wt - 1u, tot, (uint32_t)wt);
            uint32_t pos = u % plen, cell = pos * n / plen, pass = u / unit, h;
            int edge = cell != pc || u / plen != pl, cut = notch && pass != pp && x > sx;
            uint16_t fill, bar;
            pc = cell;
            pl = u / plen;
            pp = pass;
            if (x < a || (x >= skip[0] && x < skip[1]) || (x >= skip[2] && x < skip[3]))
                continue;
            fill = sg_fill_at(F, x);
            bar = fill == T_THEME ? T_INK : T_THEME;
            if (cut) {                                 /* a pass begins: the notch */
                cv_rect(x, y + SG_PAD, 1, SG_PH, T_SURF);
                continue;
            }
            if (gaps && edge && x > sx)
                continue;
            h = (den >> (2u * cell)) & 3u;
            if (h)
                cv_rect(x, yb - (int32_t)(h * SG_PH / 3u), 1, (int32_t)(h * SG_PH / 3u), bar);
            else
                cv_rect(x, yb - 1, 1, 1, ux_mix(fill, bar, 30));
        }
    }
}
/* lane k's block of sections i .. j - 1 on slot s; c: the cursor's section when it is in this lane, else past it;
 * sp: the playhead on screen (-100: none) */
static void sg_block(const song_lay_t *L, int32_t off, uint32_t k, uint32_t i, uint32_t j, uint32_t s, uint32_t c,
                     int32_t sp)
{
    int32_t hi = SG_X0 + SG_W, y = SG_LY(k), bx = SG_X0 + L->x[i] - off, bw = L->x[j] - L->x[i] - SG_GAP, lx, w;
    int32_t x0 = bx > SG_X0 ? bx : SG_X0, x1 = bx + bw < hi ? bx + bw : hi, skip[4] = {0, 0, 0, 0};
    int saved = graph_project_used(s), cur = c >= i && c < j;
    sg_fill_t F = {0, 0, 0, 0, 0};
    uint32_t p = chain.running && chain.row >= i && chain.row < j ? chain.row : 0xFFu;
    if (x1 <= x0)
        return;
    if (!saved) {                                      /* not saved: an outline, the letter DIM */
        if (cur) {
            int32_t cx = SG_X0 + L->x[c] - off, cw = L->x[c + 1u] - L->x[c] - SG_GAP;
            sg_frame(bx, bw, y, SG_LH, 1, SG_X0, hi, T_DIM);
            sg_frame(cx, cw, y, SG_LH, 2, SG_X0, hi, T_THEME);
            sg_letter((cx > SG_X0 ? cx : SG_X0) + SG_PAD, x1, y, s, T_THEME, T_SURF);
            return;
        }
        sg_frame(bx, bw, y, SG_LH, 1, SG_X0, hi, T_DIM);
        sg_letter(x0 + SG_PAD, x1, y, s, T_DIM, T_SURF);
        return;
    }
    cv_rrect(x0, y, x1 - x0, SG_LH, x0 != bx || x1 != bx + bw ? 0 : 2, T_RAISE, T_SURF);
    if (p != 0xFFu && sp >= SG_X0) {                   /* playing here: the part played */
        int32_t a = SG_X0 + L->x[p] - off;
        F.pf = ux_mix(T_RAISE, T_ACCENT, 35);
        F.p0 = a > x0 ? a : x0;
        F.p1 = sp + 2 < x1 ? sp + 2 : x1;              /* (its right corners under the playhead) */
        if (F.p1 > F.p0) {                             /* (no cell of its own: the letter may cross its end) */
            int32_t e = F.p0 == bx && ux.style != ST_LINE && F.p1 - F.p0 > 2 ? 2 : 0;   /* the block's rounded end */
            cv_rect(F.p0 + e, y, F.p1 - F.p0 - e, SG_LH, F.pf);
            if (e) {
                cv_rect(F.p0, y + 2, 1, SG_LH - 4, F.pf);
                cv_rect(F.p0 + 1, y + 1, 1, SG_LH - 2, F.pf);
            }
        }
    }
    if (cur) {                                         /* the cell selected */
        int32_t cx = SG_X0 + L->x[c] - off, cw = L->x[c + 1u] - L->x[c] - SG_GAP;
        F.c0 = cx > SG_X0 ? cx : SG_X0;
        F.c1 = cx + cw < hi ? cx + cw : hi;
        if (F.c1 > F.c0)
            cv_rrect(F.c0, y, F.c1 - F.c0, SG_LH, j - i == 1u && F.c0 == cx && F.c1 == cx + cw ? 2 : 0, T_THEME, T_SURF);
    }
    lx = x0 + SG_PAD;                                  /* the letters: the block's, the cell's (when it starts later) */
    if (!(F.c1 > F.c0 && lx >= F.c0 && lx < F.c1) && (w = sg_letter(lx, F.c1 > F.c0 && F.c0 > lx ? F.c0 : x1, y, s, T_TEXT, sg_fill_at(&F, lx)))) {
        skip[0] = lx - 1;
        skip[1] = lx + w + 2;
    }
    if (F.c1 > F.c0 && (w = sg_letter(F.c0 + SG_PAD, F.c1, y, s, T_INK, T_THEME))) {
        skip[2] = F.c0 + SG_PAD - 1;
        skip[3] = F.c0 + SG_PAD + w + 2;
    }
    sg_picture(L, off, k, i, j, s, x0, x1, &F, skip);
}
static void graph_song(void)
{
    song_lay_t L;
    const aafont_t *fl = &AF_M;                       /* the line: the selected cell's detail and the length, M */
    int32_t off, px, sp, hi = SG_X0 + SG_W, bx, x0, ly = SG_IY - (AF_M_CAP_Y + AF_M_CAP_H);
    uint32_t i, j, k, c = song_cur(), lane = ui.song_trk % NTRK, total, bars;
    char b[24], a[12];
    song_layout(&L);
    off = song_follow(&L);
    px = song_playx(&L);
    sp = px >= 0 ? SG_X0 + px - off : -100;
    total = L.u0[L.n];
    bars = (total + SG_BARU - 1u) / SG_BARU;
    for (k = 0; k < NTRK; k++)                        /* the lanes' cushions */
        cv_trk(SG_TX, SG_LY(k) + (SG_LH - 16) / 2, 16, k, k == lane ? T_ACCENT : T_MID, T_SURF);
    for (i = 1; i < L.n; i++) {                       /* the sections' boundaries, faint, behind the blocks */
        int32_t x = SG_X0 + L.x[i] - off - 1;
        if (x >= SG_X0 && x < hi)
            cv_rect(x, SG_Y, 1, SG_H, T_LINE);
    }
    for (k = 0; k < NTRK; k++)                        /* the lanes: a block per run of one slot, "-" a faint line */
        for (i = 0; i < L.n; i = j) {
            uint32_t s = chain_config.row[i].slot[k];
            for (j = i + 1u; j < L.n && chain_config.row[j].slot[k] == s; j++)
                ;
            if (s < 4u) {
                sg_block(&L, off, k, i, j, s, k == lane ? c : CHAIN_ROWS, sp);
            } else {
                int32_t sx = SG_X0 + L.x[i] - off, ex = SG_X0 + L.x[j] - off - SG_GAP;
                sx = sx > SG_X0 ? sx : SG_X0;
                ex = ex < hi ? ex : hi;
                if (ex > sx)
                    cv_rect(sx, SG_LY(k) + SG_LH / 2, ex - sx, 1, T_LINE);
                if (k == lane && c >= i && c < j)      /* the cursor on a silent cell: its outline */
                    sg_frame(SG_X0 + L.x[c] - off, L.x[c + 1u] - L.x[c] - SG_GAP, SG_LY(k), SG_LH, 1, SG_X0, hi, T_THEME);
            }
        }
    if (L.add) {                                       /* + ADD */
        bx = SG_X0 + L.x[L.n] - off;
        x0 = bx > SG_X0 ? bx : SG_X0;
        if ((bx + SG_ADDW < hi ? bx + SG_ADDW : hi) > x0) {
            int32_t x1 = bx + SG_ADDW < hi ? bx + SG_ADDW : hi;
            sg_dash(bx, SG_ADDW, SG_X0, hi, c == L.n ? T_THEME : T_DIM);
            if (text_w(fl, "+") <= x1 - x0 - 4) {
                GFX_HOOK_ALIGN(x0, SG_Y, x1, SG_Y + SG_H, AL_HV, "song + ADD centred");
                if (fl == &AF_M) cv_text_in(x0, SG_Y + CAP_IN(M, SG_H), x1 - x0, &AF_M, "+", c == L.n ? T_THEME : T_DIM, T_SURF);
                else cv_text_in(x0, SG_Y + CAP_IN(S, SG_H), x1 - x0, &AF_S, "+", c == L.n ? T_THEME : T_DIM, T_SURF);
            }
        }
    }
    if (L.n) {                                         /* the ruler: a tick a bar, every 4th longer with its number */
        int32_t r0 = SG_X0 + L.x[0] - off, r1 = SG_X0 + L.x[L.n] - off - SG_GAP, last = -100;
        int32_t lx[24];                                /* the numbers, drawn after the ticks */
        uint32_t bar, lb[24], nl = 0;
        r0 = r0 > SG_X0 ? r0 : SG_X0;
        r1 = r1 < hi ? r1 : hi;
        if (r1 > r0) cv_rect(r0, SG_RY, r1 - r0, 1, T_LINE);
        if (sp >= SG_X0 && sp < hi - 1) {              /* the playhead, across the lanes (INK on the cell selected) */
            for (k = 0; k < NTRK; k++) {
                uint32_t s = c < L.n ? chain_config.row[c].slot[k] : CHAIN_SILENT;
                int on = k == lane && s < 4u && graph_project_used(s) && sp >= SG_X0 + L.x[c] - off &&
                         sp + 2 <= SG_X0 + L.x[c + 1u] - off - SG_GAP;
                cv_rect(sp, SG_LY(k), 2, SG_LH + (k + 1u < NTRK ? SG_LG : 0), on ? T_INK : T_ACCENT);
            }
            cv_rect(sp, SG_Y + SG_H, 2, SG_RY - SG_Y - SG_H + 6, T_ACCENT);
        }
        for (i = 0; i < L.n; i++) {
            uint32_t u1 = L.u0[i + 1u], w = (uint32_t)(L.x[i + 1u] - L.x[i]);
            bx = SG_X0 + L.x[i] - off;
            if (bx >= hi || bx + (int32_t)w < SG_X0)
                continue;
            bar = (L.u0[i] + SG_BARU - 1u) / SG_BARU;  /* (from the view's left edge) */
            if (bx < SG_X0) {
                uint32_t uv = L.u0[i] + sg_muldiv((uint32_t)(SG_X0 - bx), u1 - L.u0[i], w);
                bar = uv / SG_BARU > bar ? uv / SG_BARU : bar;
            }
            for (; bar * SG_BARU < u1 || (i + 1u == L.n && bar * SG_BARU == u1); bar++) {
                int32_t x = SG_X0 + song_ux(&L, bar * SG_BARU) - off, lg = !(bar % 4u);
                if (i + 1u == L.n && bar * SG_BARU == u1) x = r1 - 1;   /* (the song's end: its last section's edge) */
                if (x > hi - 1)
                    break;
                if (x < SG_X0)
                    continue;
                cv_rect(x, SG_RY + 1, 1, lg ? 5 : 2, lg ? T_MID : T_DIM);
                if (lg && bar * SG_BARU < total && nl < 24u) {
                    lx[nl] = x;
                    lb[nl++] = bar + 1u;
                }
            }
        }
        for (k = 0; k < nl; k++) {                     /* each number right of its tick, where it has room */
            int32_t w;
            fmt_int(a, (int32_t)lb[k]);
            w = text_w(&AF_S, a);
            if (lx[k] + 3 > last + 4 && lx[k] + 3 + w <= hi && (sp + 2 <= lx[k] + 2 || sp > lx[k] + 4 + w))
                last = cv_text_on(lx[k] + 3, SG_RY + 4 - AF_S_CAP_Y, &AF_S, a, T_DIM, T_SURF);
        }
    }
    /* one line: the song's length right (where it fits), the selected cell's project left */
    x0 = hi;
    if (L.n) {
        fmt_int(b, (int32_t)bars);
        str_cpy(b + str_len(b), bars == 1u ? " BAR" : " BARS", 6);
        x0 = hi - text_w(fl, b);
        cv_text_on(x0, ly, fl, b, T_MID, T_SURF);
    }
    if (c < L.n) {
        uint32_t s = chain_config.row[c].slot[lane];
        if (s >= 4u)
            cv_free_text(SG_X0, ly, fl, "SILENT", T_DIM, T_SURF, x0 - 10 - SG_X0);
        else if (!graph_project_used(s))
            cv_free_text(SG_X0, ly, fl, "NOT SAVED", T_DIM, T_SURF, x0 - 10 - SG_X0);
        else if (graph_project_name(s)[0])
            cv_free_text(SG_X0, ly, fl, graph_project_name(s), T_MID, T_SURF, x0 - 10 - SG_X0);
    } else {                                           /* + ADD: how */
        for (i = 0; i < 4u && !graph_project_used(i); i++)
            ;
        cv_free_hint(SG_X0, ly + AF_M_CAP_Y + AF_M_CAP_H - AF_S_CAP_Y - AF_S_CAP_H,   /* (a key hint: S) */
                     i < 4u ? "[K3] ADD SECTION" : "[SAVE] PROJECT A-D", T_MID, T_SURF, x0 - 10 - SG_X0);
    }
}
static void draw_graph(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint16_t c = ACC;
    uint32_t sig;
    if (!ui.home && pg->graph == GR_TRK) {
        draw_tracks();
        return;
    }
    if (ui.home && home_tracks()) {                  /* MENU > HOME TRACKS (1.3) */
        draw_home_tracks();
        return;
    }
    if ((ENGINES[t->eng_req % NENGINES] == &ENG_GRAIN) || (ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE)) sample_wave_tick(t);
    sig = graph_signature();
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, graph_h(), T_BG);
    cv_rrect(3, 0, 234, graph_h(), 5, T_SURF, T_BG);   /* the panel (MENU > LARGE: the strip) */
    cv_bg = T_SURF;                                  /* (text drawn with cv_text lands on it) */
    cv_oy = GOY;                                     /* graphs on a 100 px scale */
    if (strip_kind() != SK_NONE) {                   /* MENU > LARGE: the strip */
        uint32_t k = strip_kind();
        cv_oy = k == SK_ADSR || k == SK_LFO ? 3 : 0;
        graph_ht = LG_H_GRAPH - 6;
        if (k == SK_SCOPE) graph_scope(c);
        else if (k == SK_ADSR) graph_adsr(t, c);
        else if (k == SK_LFO) graph_lfo(t, c);
        else graph_title();
        graph_ht = 100;
    } else if (ui.home) {
        cv_oy = 0;
        if (ENGINES[t->eng_req % NENGINES] == &ENG_GRAIN)
            graph_grain(t, c);
        else if (ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE)
            graph_sample(c);
        else if (ENGINES[t->eng_req % NENGINES] == &ENG_SCRUB)
            graph_scrub(t, c);
        else if (ENGINES[t->eng_req % NENGINES] == &ENG_WAVETABLE)
            graph_wavetable(t, c);
#if FELUCCA_SLICE
        else if (ENGINES[t->eng_req % NENGINES] == &ENG_SLICE)
            graph_slices();
#endif
        else
            graph_scope(c);
    } else {
        switch (pg->graph) {
        case GR_ADSR:
            graph_adsr(t, c);
            break;
        case GR_LFO:
            graph_lfo(t, c);
            break;
        case GR_ARP:
            cv_oy = 0;
            graph_arp(t, c);
            break;
        case GR_ROLL:
            cv_oy = 0;
            if (detail_on())                          /* 1.5.1 (#199) */
                graph_detail(t, c);
            else
                graph_loom_seq_grid(t, c);
            break;
        case GR_STEPS:
            cv_oy = 0;
            graph_steps(t, c);
            break;
        case GR_SCALE:
            graph_scale(t, c);
            break;
        case GR_CHORD:
            cv_oy = 0;
            graph_chord(t, c);
            break;
        case GR_PROG:
            cv_oy = 0;
            graph_prog(t, c);
            break;
        case GR_FX:
            graph_fx(t, c);
            break;
        case GR_SLCR:
            graph_slicer(t, c);
            break;
        case GR_INS:
            graph_insert(t, c);
            break;
        case GR_SIDECHAIN:
            graph_sidechain(c);
            break;
        case GR_DELAY:
            cv_oy = 0;
            graph_delay(c);
            break;
        case GR_REVERB:
            cv_oy = 0;
            graph_reverb(c);
            break;
        case GR_CHORUS:
            cv_oy = 0;
            graph_chorus(c);
            break;
        case GR_MOD:
            cv_oy = 0;
            graph_mod(t, c);
            break;
        case GR_MACRO:
            cv_oy = 0;
            graph_macro_perf(t, c);
            break;
        case GR_BROWSE:
            cv_oy = 0;
            graph_browse();
            break;
        case GR_SLOTS:
            cv_oy = 0;
            graph_slots();
            break;
        case GR_USER:
            cv_oy = 0;
            graph_user();
            break;
        case GR_SONG:
            cv_oy = 0;
            graph_song();
            break;
        case GR_PATS:
            cv_oy = 0;
            graph_pats();
            break;
        case GR_EVENTS:
            cv_oy = 0;
            graph_events();
            break;
        case GR_TOOLS:
            cv_oy = 0;
            graph_tools();
            break;
#if FELUCCA_SLICE
        case GR_SLICES:
            cv_oy = 0;
            graph_slices();
            break;
#endif
        case GR_FMOP:
        case GR_FMOP2:
            if (fop_ok()) graph_fm6(t, c, fop_op % 6u);   /* the chart, the operator picked */
            break;
        case GR_FMENV:
            if (fop_ok()) graph_fm6env(t, c);
            break;
        default:
            if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_WHEEL) graph_wheel(t, c);
            else if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE) graph_sample(c);
            else if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_GRAIN) graph_grain(t, c);
            else if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_SCRUB) graph_scrub(t, c);
            else if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_WAVETABLE) { cv_oy = 0; graph_wavetable(t, c); }
#if FELUCCA_SLICE
            else if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_SLICE) graph_slices();
#endif
            else if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_ANALOG) { cv_oy = 0; graph_subtract(t, c); }
            else if (pg->scope == SC_ENGINE && t->eng_req % NENGINES == ENGI_FM6) graph_fm6(t, c, 6u);   /* EDIT 1 and 2 */
#if FELUCCA_FM4
            else if ((pg->scope == SC_ENGINE || pg->id[0] == P_FM1_LEVEL) && t->eng_req % NENGINES == ENGI_DIGITAL)
                graph_fm(t, c);                      /* (OP LEVEL too: the levels on the chart) */
#endif
            else { cv_oy = 0; graph_scope(c); }
            break;
        }
    }
    cv_oy = 0;
    cv_blit(0, graph_y());
}
