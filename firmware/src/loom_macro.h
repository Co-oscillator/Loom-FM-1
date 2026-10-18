/* SPDX-License-Identifier: GPL-3.0-only
 * Loom FM-1: Macro and Modulation Matrix Architecture
 */
#ifndef LOOM_MACRO_H
#define LOOM_MACRO_H

#include <stdint.h>

enum {
    MS_OFF,
    MS_KNOB,
    MS_LFO,
    MS_ENV,
    MS_VEL,
    MS_KEY,
    MS_RAND,
    MS_MODW,
    MS_AT,
    MS_EXPR,
    MS_SH,
    MS_SLEW,
    MS_N
};

#define NMACROS 4u
#define NMACRO_DESTS 3u

/* Macro destination:
 * trk:  0: T1, 1: T2, 2: T3, 3: T4, 4: DRM
 * dest: 0: OFF, 1: PITCH, 2: CUT, 3: AMP, 4: PAN, 5: DLY, 6: REV, 7..14: E0..E7 (or LN0..LN7)
 * amt:  -64 .. +63 (-100% .. +100%)
 */
typedef struct {
    uint8_t trk;
    uint8_t dest;
    int8_t  amt;
} macro_dest_t;

typedef struct {
    uint8_t src;  /* MS_OFF, MS_KNOB, MS_LFO, MS_ENV, ... */
    uint8_t val;  /* 0..127 */
    macro_dest_t dest[NMACRO_DESTS];
} macro_t;

extern macro_t loom_macros[NMACROS];
extern uint8_t macro_ui_line;

void macro_init(void);
void macro_sync_to_tracks(void);
void macro_sync_from_tracks(void);
const char *macro_trk_name(uint8_t trk_idx);
const char *macro_dest_name(uint8_t trk_idx, uint8_t dest_id);
uint32_t macro_dest_icon(uint8_t trk_idx, uint8_t dest_id);
int macro_is_assigned(uint32_t m);

#endif /* LOOM_MACRO_H */
