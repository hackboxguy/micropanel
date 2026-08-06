# RH850 Bluebox transport selection

`rh850-flash-auto.sh` selects the Bluebox USB transport for the IOC-update
actions when one complete Bluebox is attached. It identifies an FT2232D with
USB ID `0403:a9a0` through sysfs, then requires both interfaces from the same
device:

- interface `01`: programming UART;
- interface `00`: normal firmware console UART.

The selector passes their discovered paths to `rh850-tool`, so it does not
assume a fixed `ttyUSB` enumeration. A Bluebox that is physically present but
lacks either serial interface is treated as unavailable.

For 983HH and Lattice45-9090, no Bluebox means `flashrh850.sh` is executed with
the original action arguments unchanged. This retains the existing direct Pi
GPIO and `/dev/ttyS0` workflow, including its NPJ and USB-image behaviour.
There is deliberately no fallback after a Bluebox operation has started: a
Bluebox error must be fixed rather than risking an unintended direct-GPIO
operation.

S7-9090 is new to this menu, so its no-Bluebox path uses the explicit
`rh850-tool --transport=pi-gpio` S4 profile. Its control lines and local UART
are configured in `rh850-flash-tools/profiles/transports/pi-gpio.conf`.

## Menu coverage

The IOC-Update menu routes these actions through the selector:

| Board | Target profile | Built-in images |
| --- | --- | --- |
| 983HH | `r7f7016863` | existing packet, bare-metal, script, and blink images |
| Lattice45-9090 | `r7f7016863` | existing packet and bare-metal images |
| S7-9090 | `r7f7015873` | `Spartan7_9090_ROMBIOS_packet.bin` and `REMOTE_DISP_SPARTAN7_display_manager_s4.bin` |

Bluebox backups are stored under
`share/sp6bins/bkups/<board>/` with a SHA-256 manifest. Recovery first selects
the newest backup in that board directory. For 983HH and Lattice45-9090 it
also accepts the legacy canonical backup file in `share/sp6bins/bkups/` when
there is no Bluebox-specific backup yet.

## Build and install

The selector itself is installed as `bin/rh850-flash-auto.sh`. To install the
required Bluebox executables and profiles with Micropanel, enable the explicit
cross-repository option:

```sh
cmake -S . -B build \
  -DCMAKE_INSTALL_PREFIX=/home/pi/micropanel \
  -DINSTALL_SCREEN=config-pios-new.json \
  -DINSTALL_RH850_FLASH_TOOLS=ON \
  -DRH850_FLASH_TOOLS_SOURCE_DIR=/path/to/rh850-flash-tools
cmake --build build
cmake --install build
```

The account starting Micropanel must be able to access the Bluebox serial
devices (normally through the `dialout` group). `rh850-tool` preserves target
option bytes and verifies programming by default.
