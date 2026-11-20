# 983v4: soft-DIP fallback when the Pi cannot read the DIP switches

Software workaround in `scripts/dip-switch-resolution.sh`
(`soft_dip_without_pcf8574()`), added 2026-11-20.

## The fault

The Pi reads the 983HH's DIP switches through a PCF8574 at `0x20` on `i2c-3`
(GPIO4/GPIO5, header pins 7 and 29). With every switch off, the script sees
`0xff`, then sends the display type picked on the Stream Deck to the RH850 as
the soft DIP (`0x67` reg `0x0316`, cold re-init `0x0300 = 0x11`).

On the first 983v4 board (Pi4 rig `192.168.1.16`, 12.3"-NQ5) **nothing answers
on i2c-3**. A scan finds neither `0x20` nor `0x19`; a retry at 10 kHz found nothing either. The 983v3 rig
(`192.168.1.153`) shows both. The script used to stop at that point. So:

| Symptom on the 983v4 rig | Why |
| --- | --- |
| `dip-switch: No DIP switch (PCF8574) found at 0x20 on i2c-3 after 15s, skipping` | the PCF8574 does not answer |
| RH850 `0x0307 ACTIVE_PROFILE = 0xFF`, `0x0318 PROFILE_RUNS = 0` | no soft DIP was sent, so no profile ran |
| `hh983-serializer: Failed to write DES[0x2C] reg 0x04: -5` | FPD-Link down, 988 not reachable |
| panel black, 0.000 cd/m² | |

The RH850 reads the switches correctly on the 983v4 (`0x0315 = 0x00` with
every switch off, the same as on the 983v3). Only the Pi's path to the PCF8574
is broken. The suspects are the i2c-3 wiring, the PCF8574's power, or the
pull-ups that the v4 integrated in place of the v3 rework. Both `0x19` and
`0x20` are missing, so the whole bus segment is down, not one chip.

## What the workaround does

If the PCF8574 does not answer, the script asks the RH850 instead (`0x67`
reg `0x0315`, up to 5 s):

- `0x00` (every switch off): logs
  `WARNING: PCF8574 missing on i2c-3 but the RH850 reports every DIP off: soft DIP fallback (983v4 workaround)`
  and runs the same `apply_soft_dip` / `apply_fpga_variant` as for a `0xff` read.
- non-zero: the RH850 already runs that profile itself; nothing to do.
- no answer (no 983HH): nothing to do, as before.

**Effect on boards with a working PCF8574 (983v3 and fixed 983v4): none.**
Those boards never reach the fallback.

**Limits while the workaround is in use:**

1. Keep every DIP off and pick the display on the Stream Deck. With hardware
   DIPs set, the RH850 still runs the right profile, but the Pi cannot detect a
   mismatch with its own HDMI timing and correct it.
2. A boot takes about 15 s longer: the script still waits `DIP_WAIT_S` for
   the PCF8574 before it falls back.
3. Whatever sits at `0x19` on i2c-3 stays unreachable. Nothing in this tree
   uses it.

## Checking a board

```sh
sudo i2cdetect -y -r 3                       # fixed board: 0x19 and 0x20 present
sudo i2cget -y 3 0x20                        # 0xff with every switch off
journalctl -b -u dip-switch-resolution       # which path ran
```

- `DIP switch value: 0xff` then `Every DIP switch off: soft DIP mode`: the
  normal path; the hardware works.
- `WARNING: PCF8574 missing ... soft DIP fallback (983v4 workaround)`: this
  board still has the fault.

## When the hardware is fixed

The fallback switches itself off: on a fixed board the PCF8574 answers, the
normal path runs, and the fallback code is never reached. **Nothing needs to
change in software.** You can leave the block in place indefinitely; it costs
nothing on a good board.

Remove it (the function `soft_dip_without_pcf8574` and the `if ! dip_value=...`
call, back to `dip_value=$(read_dip) || exit 0`) only when **all** of these are
true:

1. Every 983v4 board in use has the fix. Check each one with `i2cdetect -y -r 3`
   (`0x20` present) and the journal (no `983v4 workaround` warning across a few
   boots).
2. No unfixed 983v4 remains in the field or on a bench. If one turns up after
   the removal, it boots black again with every DIP off.

If the hardware is never fixed, keep the workaround and follow the limits
above. If boot time matters on those boards, the 15 s PCF8574 wait can be
skipped when the RH850 already reports `0x0315 = 0x00`; that is not done
today, because it would also change the timing on boards that work.
