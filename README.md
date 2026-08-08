# TACAI

A Raspberry Pi Pico–based auto-loader for the **TI-84 Plus CE**.

`cesium-loader` runs as a battery-powered USB host that pushes files onto the calculator over TI's **DUSB link protocol** at the press of a button. It is designed to make reinstalling **Cesium/arTIfiCE** and reloading games fast after a RAM-clearing test.

## How It Works

Two separate firmware images run on the same Raspberry Pi Pico. They are flashed one at a time through **BOOTSEL**, depending on what you are doing.

### `loader_mode`

Connect the Pico to a computer through USB-C. The Pico appears as a normal USB flash drive.

Files can be dragged into two folders:

```text
GAMES/
CESIUM/
```

* `GAMES/` — `.8xp` and `.8xv` game/program files
* `CESIUM/` — arTIfiCE and Cesium-related files

This mode is based on [`pico-usb-flash-drive`](https://github.com/oyama/pico-usb-flash-drive).

### `field_mode`

`field_mode` is the firmware contained in this repository's `src/` directory.

It is intended to run from battery power. Once a TI-84 Plus CE is connected, pressing the button transfers files from the Pico's onboard flash directly to the calculator.

No computer is required.

The Pico acts as a **USB host** and communicates with the calculator directly using TI's DUSB protocol.

### Button Modes

The button supports two transfer modes so the games payload and the Cesium/arTIfiCE payload can be tested independently.

| Button action                        | Transfer                |
| ------------------------------------ | ----------------------- |
| Press and release in under 5 seconds | Everything in `GAMES/`  |
| Hold for 5+ seconds, then release    | Everything in `CESIUM/` |

When the button is held for 5 seconds, the LED begins slow-blinking to indicate that the `CESIUM/` payload has been selected.

For more information about the implementation, see the file-level comments in:

* `src/main.c`
* `src/dusb_link.c`
* `src/usbh_ti_vendor.c`

These files document the USB host stack, DUSB protocol implementation, and button/LED state machine.

---

## Repository Layout

```text
cesium-loader/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── LICENSE
├── THIRD_PARTY_LICENSES.md
└── src/
    ├── main.c
    │   └── Button/LED state machine and transfer flow
    ├── dusb_link.c
    ├── dusb_link.h
    │   └── DUSB protocol implementation
    ├── ti8x_file.c
    ├── ti8x_file.h
    │   └── .8xp/.8xv file parser
    ├── usbh_ti_vendor.c
    │   └── TinyUSB host class driver for the calculator
    ├── flash_ro.c
    ├── flash_ro.h
    │   └── Read-only access to the flash region written by loader_mode
    ├── fatfs_driver_ro.c
    │   └── FatFs disk I/O glue over flash_ro
    └── tusb_config.h
```

### Source Overview

| File                | Purpose                                             |
| ------------------- | --------------------------------------------------- |
| `main.c`            | Button/LED state machine and transfer flow          |
| `dusb_link.c/.h`    | TI DUSB protocol implementation                     |
| `ti8x_file.c/.h`    | `.8xp`/`.8xv` file parsing                          |
| `usbh_ti_vendor.c`  | TinyUSB host driver for the TI calculator interface |
| `flash_ro.c/.h`     | Read-only access to the flash storage region        |
| `fatfs_driver_ro.c` | FatFs disk I/O glue for onboard flash               |
| `tusb_config.h`     | TinyUSB configuration                               |

---

## Requirements

### Hardware

* Raspberry Pi Pico
* TI-84 Plus CE
* USB connection between the Pico and calculator
* Battery/power source suitable for the Pico
* Computer for initially loading files and flashing firmware

### Software

* Raspberry Pi Pico SDK
* CMake
* GNU Make
* ARM GCC toolchain

The project currently targets the standard Raspberry Pi Pico board:

```text
PICO_BOARD=pico
```

---

## Building

Set `PICO_SDK_PATH` to the location of your Pico SDK:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
```

Then build `field_mode`:

```bash
mkdir -p build
cd build
cmake .. -DPICO_BOARD=pico
make -j$(nproc)
```

The resulting firmware will be located at:

```text
build/field_mode.uf2
```

### Flashing `field_mode`

1. Disconnect the Pico from USB.
2. Hold the **BOOTSEL** button.
3. Connect the Pico to your computer.
4. Release **BOOTSEL**.
5. Copy `build/field_mode.uf2` to the mounted `RPI-RP2` drive.

The Pico will reboot automatically after the firmware is copied.

---

## `loader_mode`

`loader_mode` is built separately from the project's [`pico-usb-flash-drive`](https://github.com/oyama/pico-usb-flash-drive) fork.

Its purpose is to provide a USB mass-storage interface for loading files into the Pico's flash storage.

The typical workflow is:

```text
Computer
   │
   │ USB
   ▼
Pico — loader_mode
   │
   ├── GAMES/
   │    ├── game1.8xp
   │    └── game2.8xp
   │
   └── CESIUM/
        ├── arTIfiCE.8xp
        └── Cesium.8xp
```

After the files have been copied, flash `field_mode` to the Pico and use it as the standalone loader.

---

## Typical Workflow

```text
                ┌─────────────────────┐
                │      Computer       │
                └──────────┬──────────┘
                           │
                         USB-C
                           │
                           ▼
                ┌─────────────────────┐
                │   Pico loader_mode  │
                │                     │
                │   GAMES/            │
                │   CESIUM/           │
                └──────────┬──────────┘
                           │
                    Files stored in
                    onboard flash
                           │
                    Flash field_mode
                           │
                           ▼
                ┌─────────────────────┐
                │   Pico field_mode   │
                │                     │
                │   USB Host          │
                │   DUSB Protocol     │
                └──────────┬──────────┘
                           │
                       USB cable
                           │
                           ▼
                ┌─────────────────────┐
                │     TI-84 Plus CE   │
                └─────────────────────┘
```

### Loading Games

1. Flash `loader_mode`.
2. Connect the Pico to a computer.
3. Copy `.8xp`/`.8xv` files into `GAMES/`.
4. Safely eject the Pico.
5. Flash `field_mode`.
6. Connect the TI-84 Plus CE.
7. Press and release the button within 5 seconds.
8. The contents of `GAMES/` are transferred.

### Loading Cesium/arTIfiCE

1. Flash `loader_mode`.
2. Connect the Pico to a computer.
3. Copy the required Cesium/arTIfiCE files into `CESIUM/`.
4. Safely eject the Pico.
5. Flash `field_mode`.
6. Connect the TI-84 Plus CE.
7. Hold the button for at least 5 seconds.
8. Wait for the LED to begin slow-blinking.
9. Release the button.
10. The contents of `CESIUM/` are transferred.

---

## Architecture

The project is split into several major components.

### USB Host

`usbh_ti_vendor.c` implements the TinyUSB host-side interface used to communicate with the TI-84 Plus CE.

The Pico operates as the USB host rather than the calculator.

### DUSB

`dusb_link.c/.h` implements the TI DUSB communication protocol.

The implementation is based on protocol logic from **tilibs** and has been ported to run on the Pico.

### TI File Formats

`ti8x_file.c/.h` handles TI calculator file formats, including:

* `.8xp`
* `.8xv`

This allows the loader to interpret files before sending them over DUSB.

### Flash Storage

`flash_ro.c/.h` provides read-only access to the flash region containing the files uploaded through `loader_mode`.

`fatfs_driver_ro.c` exposes this storage through FatFs.

This allows `field_mode` to read the same filesystem that `loader_mode` previously populated.

---

## License

This repository is licensed under the **GNU General Public License v3.0 or later (GPL-3.0-or-later)**.

That's not the default choice for a personal hobby project, and it's worth explaining why: `src/dusb_link.c` and `src/ti8x_file.c` are direct ports of protocol/format logic from the **tilibs** project (`libticalcs`, `libtifiles`), which is licensed under the GPL.

A ported or translated reimplementation of GPL-licensed logic is generally treated as a derivative work, and the GPL requires derivative works to be distributed under the same license. The tilibs source files used as the basis for these ports explicitly include the **"or (at your option) any later version"** clause, which permits this project to use **GPL-3.0-or-later** rather than remaining pinned to GPL v2.

Licensing the entire repository under the GPL is therefore the straightforward way to remain consistent with the licensing requirements of the code this project is derived from.

> **Note:** I'm not a lawyer, and this is not legal advice. If you plan to redistribute this project further, especially in a commercial context, it's worth having the licensing situation professionally assessed rather than relying solely on this explanation.

This is compatible with the other code reused in this project: **BSD-3-Clause** (`pico-usb-flash-drive`, Raspberry Pi Pico SDK) and **MIT** (TinyUSB) are permissive licenses that can be incorporated into a GPL-licensed work.

Full license texts and per-file attribution are available in [`THIRD_PARTY_LICENSES.md`](./THIRD_PARTY_LICENSES.md).

---

## Third-Party Software and Credits

### tilibs

**[tilibs](https://github.com/debrouxl/tilibs)** by Romain Liévin and contributors.

Used for the basis of:

* `src/dusb_link.c`
* `src/ti8x_file.c`

These files contain direct ports of DUSB protocol and `.8xp`/`.8xv` file-format logic from `libticalcs`, `libtifiles`, and related tilibs components.

**License:** GPL-2.0-or-later

### pico-usb-flash-drive

**[pico-usb-flash-drive](https://github.com/oyama/pico-usb-flash-drive)** by Hiroyuki OYAMA.

This project provides the basis for `loader_mode`, including the flash-backed FAT12 USB mass-storage implementation and the resized-partition version present in this project's history.

**License:** BSD-3-Clause

### TinyUSB

**[TinyUSB](https://github.com/hathach/tinyusb)** by hathach and contributors.

TinyUSB provides the USB host stack used by `field_mode`.

**License:** MIT

### Raspberry Pi Pico SDK

**[Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)**

Provides the SDK and hardware support used to build the Pico firmware.

**License:** BSD-3-Clause

### Cesium

**[Cesium](https://github.com/mateoconlechuga/cesium)** by mateoconlechuga.

Cesium is the TI-84 Plus CE homebrew shell that this loader transfers to the calculator.

Cesium is **not redistributed by this repository**. Users download it separately.

### arTIfiCE

**[arTIfiCE](https://github.com/YvanTT/arTIfiCE)** by YvanTT.

arTIfiCE is the TI-BASIC exploit used to restore ASM program execution on supported locked-down OS versions.

arTIfiCE is **not redistributed by this repository**. Users download it separately.

---

## Legal / Attribution

This project does not redistribute Cesium or arTIfiCE.

Users are responsible for obtaining those files from their respective projects and complying with their applicable licenses and terms.

Third-party license texts and attribution information for code incorporated into this repository are provided in [`THIRD_PARTY_LICENSES.md`](./THIRD_PARTY_LICENSES.md).

---

## Project Status

`cesium-loader` is intended as a dedicated hardware loader for the TI-84 Plus CE, with the goal of making repeated calculator testing and setup substantially faster.

The two-firmware architecture keeps file management separate from standalone field operation:

```text
loader_mode  →  Load/update files
field_mode   →  Transfer files to calculator
```

Once the files are loaded, `field_mode` can operate without a computer.
