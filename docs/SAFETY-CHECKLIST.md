# FM-1 update safety — checklist and follow-up

Status: the loader-side safety improvements below are implemented and build-verified
(`./build.sh` passes, `ota_test`/`ldr_test` pass), but the on-device behaviour still
needs a working FM-1 to confirm. Keep this document as the return-to-later checklist.

## Already implemented

- **Red / green screen.** The loader drives the panel: red while an update is in
  flight (do not disconnect), green only after every app sector is written, verified,
  and the update record cleared (safe). The running firmware shows the same red screen
  before handing off to the loader, so the whole update is visibly "unsafe" until done.
  (`firmware/loader/ldr_lcd.h`, `firmware/loader/loader.c`, `firmware/src/ota_hw.c`)
- **Flashing red "RECONNECT".** If the host disappears mid-write, the loader stops,
  flashes red + "RECONNECT", and waits for the host to resume (the update record is
  kept, so a re-run finishes the install). After 30 s with no host it drops into the
  mask-ROM UBOOT recovery on its own. (`loader.c`)
- **App-only writes, head untouched.** The loader writes only the app area and never
  the flash head (SPL / `isd_config`), and verifies the whole package before erasing.
- **Frozen loader.** `LDR_ABI` is the loader's interface contract; the loader bytes are
  the same in every release. Later updates change only the app.

## Validate on a working FM-1

- [ ] First install (stock → Loom) shows red during the write, green "DONE" after.
- [ ] Unplug the cable mid-write → flashing red "RECONNECT"; replug → installer resumes and finishes.
- [ ] Left unplugged > 30 s → drops into mask-ROM UBOOT (`WL80UBOOT` / `4C4A:8057`) on its own.
- [ ] Corrupt / foreign package → solid red "FAILED", nothing erased.
- [ ] `register access: hal/ only` still passes; loader stays within its 80 KB budget.

## Possible next increments (each needs a live device to test)

- [ ] **Permanent loader region + SPL change** — stage the loader once, never rewrite it
      on routine updates (removes the last boot-adjacent write).
- [ ] **Dual-slot loader + verify** — full A/B loader with a fallback selector record.
- [ ] **Boot-time app CRC** — the SPL refuses a half-written app and stays in the loader.
- [ ] **Battery threshold** — refuse to start a write below a safe voltage (ADC available).
- [ ] **Pogo pads (D+/D−/GND) + a Pico Transporter** — physical recovery net; full-flash
      backup before every update.

## Key facts to remember

- Loader write window: app area `[0x4000, 0x93000)` only; head `[0, 0x4000)` never written.
- The loader is *staged* fresh on each update (erased after a successful boot); it is the
  same frozen bytes, not a new binary, across app-only releases.
- The failure that prompted this was the loader not re-enumerating after a disconnect;
  the reconnect screen + auto-UBOOT directly target that, and the frozen loader stops
  routine releases from changing the only code that can brick the device.
