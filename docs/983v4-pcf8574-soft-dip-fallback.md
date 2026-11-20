# 983v4: soft-DIP fallback when the Pi cannot read the DIP switches

Software workaround in `scripts/dip-switch-resolution.sh`
(`dip_from_rh850()`), added 2026-11-20.

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

The RH850 reads the same switches itself (AP0_7..14, latched at its power-up)
and reports them at `0x67` reg `0x0315` as an on-mask (DIP0 = bit 0). That
mask is the PCF8574 value inverted. If `/dev/i2c-3` exists, three PCF8574
reads in a row fail and the RH850 answers, the script uses the RH850's mask
right away (about 30 ms, no waiting), logs

`WARNING: PCF8574 not answering on i2c-3 (after 0s), DIP switches read from the RH850 (on-mask 0x.. = DIP value 0x..) (983v4 workaround)`

and carries on with `mask ^ 0xff` as if the PCF8574 had read it:

- every switch off (`0x00` -> `0xff`): soft DIP. The Pi's display type
  (`/boot/firmware/micropanel-display.txt`, set by the Stream Deck keys or by
  `pi-config-txt.sh --type=<type>` over SSH) is sent to the RH850.
- switches set: the normal DIP-switch type check. The RH850 runs the profile
  itself at power-up; the Pi compares its display type with the switches and,
  on a mismatch, reconfigures and reboots once (reboot-loop guard unchanged).
- neither the PCF8574 nor the RH850 answers (no 983HH): the script keeps
  retrying both once a second for `DIP_WAIT_S` (15 s), then stops, as before.

There is no extra wait because the 15 s exists only for `/dev/i2c-3` to
appear late at boot. Once the bus exists, a fitted PCF8574 answers the first
read, and an RH850 that answers means the board is powered. If a good board
ever took the fallback by a fluke it would get the same switches, read by the
RH850 instead.

So **DIP switches and soft DIP both work on a 983v4 as on a 983v3**, without a
Stream Deck. Checked 2026-11-20: on the 983v4 rig, Pi GPIO22 (the RH850 reset
the post-reconfigure step pulses) does reset the RH850, so only the i2c-3 path
is broken on that board.

**Effect on boards with a working PCF8574 (983v3 and fixed 983v4): none.**
Those boards never reach the fallback.

**Limits while the workaround is in use:**

1. The RH850 latches the switches at its own power-up, the PCF8574 is read
   live. A switch moved without a power cycle is seen by neither the RH850's
   profile nor (now) the Pi until the next power cycle; with the PCF8574 the
   Pi would have seen it at a Pi-only reboot. Power-cycle after changing DIPs.
2. Whatever sits at `0x19` on i2c-3 stays unreachable. Nothing in this tree
   uses it.

Until 2026-11-20 the fallback ran only after the full 15 s PCF8574 wait, on
every 983v4 boot; it now takes about 30 ms (the service finishes ~0.2 s after
it starts). On the test rigs `multi-user.target` and the Qt demo are held
until ~25 s by `disp-settings-dual-display-restore.service` either way, so the
gain shows in the units ordered after this one (e.g. `micropanel.service`),
not in when the demo appears.

Tested: 983v4 + 12.3"-NQ5, every DIP off, 5 Tasmota cold boots (profile 0x04,
picture every time, ~14 cd/m², 0.000 before). Image v2.61 with DIP3 set on the
983v4: first boot reconfigured `edid` -> 12.3 and rebooted once, later boots
matched; with the no-wait fallback, 3 Tasmota cold boots OK (profile 0x04,
fallback after 0 s) and the 983v3 normal path unaffected (2 reboots). Switch
mismatch / unknown position / no RH850 / no bus also with stubbed I2C.

## Checking a board

```sh
sudo i2cdetect -y -r 3                       # fixed board: 0x19 and 0x20 present
sudo i2cget -y 3 0x20                        # 0xff with every switch off
journalctl -b -u dip-switch-resolution       # which path ran
```

- `DIP switch value: 0xff` then `Every DIP switch off: soft DIP mode`: the
  normal path; the hardware works.
- `WARNING: PCF8574 not answering on i2c-3 (after 0s), DIP switches read from
  the RH850 ... (983v4 workaround)`: this board still has the fault.

## When the hardware is fixed

The fallback switches itself off: on a fixed board the PCF8574 answers, the
normal path runs, and the fallback code is never reached. **Nothing needs to
change in software.** You can leave the block in place indefinitely; it costs
nothing on a good board.

Remove it (the function `dip_from_rh850` and the `dip_from_rh850 && return 0`
line in `read_dip`) only when **all** of these are true:

1. Every 983v4 board in use has the fix. Check each one with `i2cdetect -y -r 3`
   (`0x20` present) and the journal (no `983v4 workaround` warning across a few
   boots).
2. No unfixed 983v4 remains in the field or on a bench. If one turns up after
   the removal, it boots black again with every DIP off.

If the hardware is never fixed, keep the workaround and mind the limits
above.
