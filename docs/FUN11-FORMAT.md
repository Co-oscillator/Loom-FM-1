# FUN11 format and migration spec

Status: **ready to implement** (the four legacy suites are green as of v1.8.4.3).
FUN11 is the release that puts the dedicated DRUM track into SONG and widens the
project format to five tracks. This document is the contract for that change.

## 1. What exists today (FUN10, v1.8.4.3)

| Item | Today | Notes |
|---|---|---|
| Tracks | `NTRK = 5`, `NPART = 4` | track 4 is the dedicated DRUM track |
| SONG chain | `CHAIN_NTRK = 4`, `chain_row_t.slot[4]`, `CHAIN_SILENT = 4` | DRUM is not in SONG |
| Chain block | `chain_config_t` = 84 bytes (count, 3 reserved, 16 rows x 5 bytes) | rows are `{slot[4], repeat}` |
| Project | FUN10 `"FUNA"`, 3840 bytes, `b[63] = 5` | four full tracks + a compact 33-parameter drum block |
| Motion | 128 records, 3-byte events `{place, param, value}` | `place` = track in bits 6..7, step in bits 0..5 |
| ARP | mode/rate/oct/gate/swing/prob/hold/order are `P_*` | the 16-step ARP PAT is RAM only |
| Backup | one 3840-byte storage object | versioned by the project magic |

The compact drum block stores: LEVEL, PAN, MUTE, DIST, REV, DLY, CHOR, SLEN,
SDIV, SSWING, SGATE, SHUMAN, E0..E7, LN0..LN7, ITYPE, IA, IB, IC, IMIX. The
DRUM engine's other parameters are inert (the hit is the envelope) and load at
their defaults.

## 2. FUN11 goals

1. The DRUM track is the fifth SONG lane: its slots, repeats and silent state
   are editable and play back with the other four.
2. Motion (automation and parameter locks) works on all five tracks.
3. ARP PAT patterns persist with the project.
4. A larger pattern bank and SONG rows beyond 16 (with queue-at-next-bar) are
   designed in, even if their UI ships later.
5. INFO can advertise the format/capabilities; backups keep working.

## 3. Format changes

### 3.1 New project magic and size

- Magic: `FUNB` (`0x46554E42`), referred to as **FUN11**.
- Size: `3840` bytes (the storage object stays a whole slot).
- Header byte 63: `parts = 6` (five tracks + the dedicated drum block marker).
  FUN10 keeps `parts = 5`; FUN6..FUN9 keep their four-track values.
- `PROJ_MAGIC` becomes FUN11 for writes; FUN10 becomes read-only.

Rationale: the size stays 3840, so the flash slots and the backup object do not
move. Only the layout inside the object changes, and the magic makes the
difference unambiguous.

### 3.2 SONG chain: 4 -> 5 lanes

```
#define CHAIN_NTRK 5
#define CHAIN_SILENT 5
typedef struct { uint8_t slot[CHAIN_NTRK], repeat; } chain_row_t;   /* 6 bytes */
typedef struct { uint8_t count, rsv[3]; chain_row_t row[CHAIN_ROWS]; } chain_config_t;
```

- `chain_config_t` grows from 84 to 100 bytes (+16).
- `chain_valid` accepts slots `0..4` and `CHAIN_SILENT` (5); the DRUM lane's
  slot is a project slot (A..D) like the others.
- `song_chain.c` arms five tracks; the chain source array becomes `[5]`.
- SONG UI: five lanes; the fifth row's label is `DRUM`.
- `chain_from_v9` fills the fifth slot with `CHAIN_SILENT`.

### 3.3 Motion: 2 -> 3 track bits

The current `place` byte cannot name track 4. FUN11 widens the record:

```
typedef struct { uint16_t place; uint8_t param; int8_t value; } motion_event_t;  /* 4 bytes */
```

- `place = track << 6 | step` (track 0..4, step 0..63).
- RAM grows by 128 bytes (128 records x 1 byte).
- The stored motion block grows from 388 to 516 bytes (+128).
- FUN10's 3-byte records convert on load: `place16 = place8` (track 0..3), the
  rest unchanged. A FUN10 record can never name track 4, so no ambiguity.
- `MOTION_LOCK` stays bit 7 of `param`; `MOTION_ID` stays `param & 0x7F`.
- The `NO LOCKS ON DRUM` guard in `lock_turn` and the `k >= NPART` guard in
  `motion_put` are removed; track 4 becomes a normal target.

Budget: the FUN10 layout has 221 spare bytes at `PROJ_FM6_OFF`. The chain (+16)
and motion (+128) use 144, leaving 77 for ARP PAT (see 3.4). If a future field
needs more, move the FM6 patches or reduce the reserved tail, not the slot size.

### 3.4 ARP PAT persistence

- Store the 16-step `arp_pat[16]` of each of the four melodic tracks (64 bytes).
  The DRUM track has no ARP, so it stores none.
- The pattern bytes keep their current meaning: bit 7 = play, low 4 bits =
  note offset (0..15, centred on 8).
- The existing `P_AMODE`, `P_ARATE`, `P_AOCT`, `P_AGATE`, `P_ASWING`,
  `P_APROB`, `P_AHOLD`, `P_AORDER` parameters already persist.
- `arp_rnd_pat` (the RNDP dice) is **not** persisted; a fresh selection redraws
  it, as today.

Budget: 64 of the 77 spare bytes. Keep 13 bytes for alignment/rounding.

### 3.5 Pattern bank and SONG length (design)

- Pattern bank: 32 factory/user slots today; FUN11 reserves the numbering for
  64 (tag `01..64` and `U01..U64`). The storage format already addresses a
  bank by index; the limit is in `NPATTERNS`/`up_pat_count` and the UI labels.
- SONG rows: `CHAIN_ROWS` 16 -> 32. A row is 6 bytes, so 16 more rows add 96
  bytes. This does **not** fit beside the motion/ARP growth in the 3840-byte
  object. Ship 16 rows in FUN11 and schedule 32 rows for a later format
  (`FUNC`) or a second storage object. Queue-at-next-bar is a playback feature
  and does not change the format.

### 3.6 INFO capability tags

Add a capability byte set (read-only, derived from the firmware):
`FUN11`, `5TRK`, `ARPPAT`, `MOT5`. MENU > INFO shows the format name and the
tags; the web updater can compare them. Do not store capabilities in the
project: they are firmware properties, not user data.

### 3.7 Backup

- The backup object keeps the 3840-byte slot size; it stores a FUN11 object.
- A backup of a FUN10 project is converted on restore (section 4).
- The backup header carries the same magic; a FUN11 firmware refuses a future
  magic it does not know, as today.

## 4. Migration matrix

| Source | On FUN11 load |
|---|---|
| FUN11 | as stored |
| FUN10 (`parts = 5`) | chain 4 -> 5 lanes, fifth `CHAIN_SILENT`; motion 3 -> 4 bytes, track 0..3; ARP PAT defaults (`0x80` per step) |
| FUN9 / FUN8 / FUN7 | as today's FUN10 path, then the FUN11 conversion |
| FUN6 and older | as today, then the FUN11 conversion |
| FUN5..FUN1 | as today (four tracks, GM drum -> DRUM) |

Rules:

- Never invent a DRUM slot: an old project's fifth lane starts silent.
- A FUN10 project with a DRUM track in a melodic slot still moves to track 4
  (`proj_drum_home`), before the chain conversion.
- `parts` is normalised to `6` after import.
- The project hash covers the whole 3836 bytes, as today.

## 5. Implementation order

1. **Chain/format first:** `CHAIN_NTRK` 5, `chain_row_t`, FUN11 magic, pack/unpack,
   `chain_from_v9`/FUN10 conversion, SONG lanes. Gate: `project_test`,
   `fun10_test`, `ui_test` SONG tests.
2. **Motion width:** `motion_event_t.place` 16-bit, RAM/store growth, FUN10
   conversion, remove the DRUM lock guard. Gate: `motion_test`, `drum_test`.
3. **ARP PAT persistence:** pack/unpack 64 bytes, defaults on migration.
   Gate: `motion_test`/ARP tests, `project_test`.
4. **Pattern bank / INFO / backup:** UI labels, capability tags, backup check.
5. **Hardware validation:** five SONG lanes, DRUM slots/repeats, automation and
   locks on DRUM, ARP PAT save/load, a FUN10 project upgrade.

## 6. Test additions

- A FUN11 round trip with a five-slot chain and a DRUM lane.
- A FUN10 -> FUN11 conversion: fifth lane silent, motion tracks 0..3 intact.
- A lock and an automation event on track 4 (DRUM) survive save/load and play.
- ARP PAT of all four melodic tracks survives save/load; the DRUM track's is
  ignored.
- A FUN10 object is refused by a pre-FUN11 firmware (magic), and a FUN11 object
  is refused by a FUN11 firmware that predates a later magic.
- Backup/restore of a FUN11 project.

## 7. Open decisions

- `CHAIN_ROWS` 32: second storage object vs. a later `FUNC` magic (see 3.5).
- Pattern bank 64: whether user patterns share the 64 numbering or keep `U`
  tags (recommended: keep `U`).
- Queue-at-next-bar: playback-only, no format change.
- MIDI learn / parameter assignment (noted for a later batch): its assignments
  are performance state like the lane mutes, not project data; store them in
  settings, not the project.
