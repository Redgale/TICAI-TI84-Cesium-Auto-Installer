# Cesium Auto-Loader for TI-84 Plus CE — Pico Firmware

## Why two firmware images instead of one

The RP2040 has one native USB controller. It can be a **device** (so a PC sees
it as a flash drive) or a **host** (so it can talk to the calculator), but not
both at the same instant. TinyUSB's device/host role is normally fixed at
compile time, and dynamically flipping it at runtime is a fragile, poorly
documented corner of the stack. Rather than build something you can't debug
in the field, this project ships as **two separate .uf2 firmware images** that
you choose between by which one is currently flashed:

1. **`loader_mode.uf2`** — Pico boots as a USB flash drive (FAT12, backed by
   onboard flash). Plug into your computer, drag files into the `CESIUM` and
   `GAMES` folders, safely eject.
2. **`field_mode.uf2`** — Pico boots on battery power, waits for you to plug
   it into the calculator's USB port, and the push-button triggers the
   transfer with LED feedback. Reads the same flash region `loader_mode`
   wrote to.

To switch: hold **BOOTSEL** on the Pico, plug into your PC via USB, drag the
other .uf2 over. This is the same drag-and-drop flow you already used to
program the Pico originally, so there's no new tooling to learn. Updating
your game roster means: reflash to loader mode, drag files in, reflash back
to field mode. A bit more friction than fully automatic mode detection, but
it will actually work reliably, which matters more for something you're
relying on before a test.

## `loader_mode` — what to use

Don't write this from scratch — fork **`oyama/pico-usb-flash-drive`**
(https://github.com/oyama/pico-usb-flash-drive). It already implements a
FAT12 filesystem living in the Pico's onboard flash, exposed over USB MSC,
readable/writable from both the PC side and from your own code via FatFs.
Steps:
- Clone it, build it once as-is to confirm your Pico shows up as a drive.
- Pre-create two folders in the FAT image: `CESIUM/` and `GAMES/`. Look at
  how the project initializes its flash region on first boot and add the
  `f_mkdir()` calls there.
- That's it — `field_mode` will read from the same flash layout.

## `field_mode` — what's in each file

The DUSB link protocol is now a real, direct port from the `tilibs` source
you provided, not a stub. Here's what's in each file and where it came from:

- **`main.c`** — button debounce, LED state machine, FatFs file listing,
  and the top-level transfer flow. Should work as written.
- **`ti8x_file.c/.h`** — parses standard `.8xp`/`.8xv` files (name, type,
  archived flag, version, raw data). Byte layout ported directly from
  `libtifiles/trunk/src/files8x.cc`'s `ti8x_file_read_regular()`.
- **`dusb_link.c/.h`** — the actual protocol: raw packet framing, virtual
  packet fragmentation + ACKing, the buffer-size/ping connect handshake,
  and the RTS to ACK to content to ACK to EOT sequence for sending one
  variable. Ported from:
  - `libticalcs/trunk/src/dusb_rpkt.cc` (raw packet framing)
  - `libticalcs/trunk/src/dusb_vpkt.cc` (fragmentation, ACKs, buffer-size negotiation)
  - `libticalcs/trunk/src/dusb_cmd.cc` (RTS/EOT/mode-set packet builders)
  - `libticalcs/trunk/src/calc_84p.cc` (the `send_var()` sequence and the
    84+CE-specific attribute header byte, `0x0F`)
  - `libticables/trunk/src/linux/link_usb1.cc` (confirms VID `0x0451`,
    PID `0xE003` for the 83+/84+ USB family, and default endpoints
    `0x81`/`0x02`)
- **`usbh_ti_vendor.c`** — the one piece that's a *shape*, not a verified
  port: a minimal TinyUSB host class driver that claims the calculator's
  vendor-specific interface and opens its bulk endpoints. TinyUSB's
  built-in class drivers (MSC/CDC/HID) won't touch a vendor-specific
  interface, so without this, `dusb_link.c` has no endpoints to send to.
  **This file hooks into TinyUSB's internal (non-stable) class-driver
  struct**, whose field names/order shift between TinyUSB releases — check
  it against `usbh_pvt.h`/`usbh.h` in whichever TinyUSB version Pico SDK
  pulls in for you, and adjust field names if the compiler complains.
  Everything else in this repo uses TinyUSB's stable public API and
  shouldn't need adjustment.

### What to verify on real hardware, in order

Nothing here has been tested against an actual TI-84+ CE — I don't have
one attached to a Pico to try it on. Bring it up incrementally:

1. Flash `field_mode`, plug into the calculator, confirm `tuh_mount_cb()`
   fires (LED goes solid) — this alone tells you `usbh_ti_vendor.c` is
   correctly claiming the interface and finding both bulk endpoints.
2. Add a debug trace around `dusb_link_connect()` and confirm the
   buffer-size and ping/mode-set exchange completes — this is the smallest
   unit that exercises the raw + virtual packet code without needing a
   real file yet.
3. Only then try a full `dusb_link_send_variable()` with a small test
   `.8xp` file.

If step 1 fails, the problem is almost certainly in `usbh_ti_vendor.c`'s
endpoint descriptor walk or the version-sensitive struct fields. If step 2
fails but step 1 works, the problem is in the raw/virtual packet framing in
`dusb_link.c` — a USB analyzer (or toggling the debug LED per raw packet
sent/received) will show where the byte stream diverges from what's
described in the source comments.

## Hardware notes on your current wiring

- Battery → VSYS/GND: correct, VSYS accepts ~1.8–5.5V and the onboard
  regulator handles the rest.
- Add a series resistor (220–330Ω is fine) between GP18 and the LED if you
  haven't already — driving an LED straight from a GPIO with no resistor
  works today but shortens both the LED's and the pin's life.
- Consider a small diode across the battery pack input to guard against
  reversed cells, since AAA holders are easy to load backward.
