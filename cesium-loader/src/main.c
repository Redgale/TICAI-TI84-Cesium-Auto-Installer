/*
 * field_mode firmware — TI-84+ CE Cesium/game auto-loader
 *
 * Boots on battery, waits for the calculator to be plugged into the Pico's
 * USB-C port, then:
 *   - short button press/release  -> sends everything in GAMES/
 *   - hold button 5+ seconds, LED starts slow-blinking (armed), then
 *     release -> sends everything in CESIUM/ (arTIfiCE + the Cesium
 *     installer) instead
 * Splitting these lets you reflash/retest the games payload and the
 * cesium/exploit payload independently.
 *
 * See README.md for the two-firmware architecture. The DUSB link protocol
 * itself lives in dusb_link.c (ported from tilibs) and usbh_ti_vendor.c
 * (the TinyUSB host class driver that claims the calculator's interface).
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "tusb.h"
#include "ff.h"          // FatFs, provided by the flash-drive base project
#include "dusb_link.h"
#include "ti8x_file.h"

// ---- Pin assignments (matches what's already soldered) --------------------
#define PIN_LED         18
#define PIN_BUTTON      15
#define BUTTON_ACTIVE_LOW 1

// ---- Debounce / timing -----------------------------------------------------
#define DEBOUNCE_MS     30
#define BLINK_ON_MS     150
#define BLINK_OFF_MS    150
#define HEARTBEAT_PERIOD_MS   30000  // how often to blip the LED when idle
#define HEARTBEAT_FLASH_MS      120  // how long the blip stays on
#define HOLD_THRESHOLD_MS      5000  // hold duration to arm cesium/exploit mode
#define ARMED_BLINK_MS           500 // slow-blink period while armed

typedef enum {
    APP_IDLE_NO_CALC,      // LED off — nothing connected
    APP_IDLE_CALC_READY,   // calc connected, waiting for button press
    APP_HOLD_ARMED,        // button held past threshold -- release to send cesium/exploit
    APP_TRANSFERRING,      // LED blinking
    APP_DONE,              // LED solid
    APP_ERROR              // LED fast-blink (distinct from transferring)
} app_state_t;

static app_state_t app_state = APP_IDLE_NO_CALC;
static bool led_on = false;
static uint64_t last_blink_us = 0;

// Debounced button level (true = currently pressed). Safe to call every
// loop iteration -- unlike an edge detector, hold-duration tracking needs
// to see the current level on every call, not just transitions.
static bool button_down(void) {
    static bool last_stable = false;
    static bool last_reading = false;
    static uint64_t last_change_us = 0;

    bool raw = gpio_get(PIN_BUTTON);
    bool reading = BUTTON_ACTIVE_LOW ? !raw : raw;

    uint64_t now = time_us_64();
    if (reading != last_reading) {
        last_change_us = now;
        last_reading = reading;
    }
    if ((now - last_change_us) > (DEBOUNCE_MS * 1000)) {
        last_stable = reading;
    }
    return last_stable;
}

static void led_task(void) {
    uint64_t now = time_us_64();
    switch (app_state) {
        case APP_IDLE_NO_CALC: {
            // Proof-of-life: blip the LED on briefly every 30s. Without this,
            // "off" (no calc) and "hung/crashed" look identical from outside.
            static uint64_t last_heartbeat_us = 0;
            uint64_t since = now - last_heartbeat_us;
            if (since > (uint64_t)(HEARTBEAT_PERIOD_MS + HEARTBEAT_FLASH_MS) * 1000) {
                last_heartbeat_us = now;
                since = 0;
            }
            bool blip_on = since < (uint64_t)HEARTBEAT_FLASH_MS * 1000;
            gpio_put(PIN_LED, blip_on);
            led_on = blip_on;
            break;
        }
        case APP_IDLE_CALC_READY:
            gpio_put(PIN_LED, 1); // solid-on to say "ready, press button"
            led_on = true;
            break;
        case APP_HOLD_ARMED: {
            // Slow blink: "keep holding to send cesium/exploit, release now to arm it"
            uint32_t period = ARMED_BLINK_MS;
            if ((now - last_blink_us) > (period * 1000)) {
                led_on = !led_on;
                gpio_put(PIN_LED, led_on);
                last_blink_us = now;
            }
            break;
        }
        case APP_TRANSFERRING: {
            uint32_t period = led_on ? BLINK_ON_MS : BLINK_OFF_MS;
            if ((now - last_blink_us) > (period * 1000)) {
                led_on = !led_on;
                gpio_put(PIN_LED, led_on);
                last_blink_us = now;
            }
            break;
        }
        case APP_DONE:
            gpio_put(PIN_LED, 1);
            led_on = true;
            break;
        case APP_ERROR: {
            uint32_t period = 80; // fast blink to signal a problem
            if ((now - last_blink_us) > (period * 1000)) {
                led_on = !led_on;
                gpio_put(PIN_LED, led_on);
                last_blink_us = now;
            }
            break;
        }
    }
}

// ---- File enumeration (flash-backed FatFs, read-only here) ---------------
// Relies on loader_mode.uf2 having already written files into these folders.

#define MAX_FILES 32

static int list_dir(const char *path, char out[][64], int max) {
    DIR dir;
    FILINFO fno;
    int count = 0;
    if (f_opendir(&dir, path) != FR_OK) {
        return 0;
    }
    while (count < max) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
        if (fno.fattrib & AM_DIR) continue;
        strncpy(out[count], fno.fname, 63);
        out[count][63] = 0;
        count++;
    }
    f_closedir(&dir);
    return count;
}

// ---------------------------------------------------------------------------
// DUSB link protocol -- ported from tilibs, see dusb_link.c for the sources.
// dusb_link_init() is called from usbh_ti_vendor.c's ti_open() the moment
// TinyUSB claims the calculator's interface and opens its bulk endpoints.
// ---------------------------------------------------------------------------

// Max size of a single variable file we'll load fully into RAM before
// sending. The RP2040 has 264KB of SRAM; leave headroom for stacks/USB
// buffers. Cesium and most .8xp games are well under this.
#define MAX_FILE_SIZE (128 * 1024)
static uint8_t file_buf[MAX_FILE_SIZE];

static bool send_one_file(const char *dir, const char *filename) {
    char path[80];
    snprintf(path, sizeof(path), "%s/%s", dir, filename);

    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) {
        printf("[xfer] f_open FAILED: %s\n", path);
        return false;
    }

    UINT br = 0;
    FRESULT fr = f_read(&f, file_buf, sizeof(file_buf), &br);
    f_close(&f);
    if (fr != FR_OK) {
        printf("[xfer] f_read FAILED: %s (fr=%d)\n", path, fr);
        return false;
    }
    printf("[xfer] read %s: %u bytes\n", path, br);

    Ti8xVarEntry entry;
    if (!ti8x_parse_regular_file(file_buf, br, &entry)) {
        printf("[xfer] ti8x_parse_regular_file FAILED: %s (not a valid .8xp/.8xv?)\n", path);
        return false;
    }
    printf("[xfer] parsed %s: name='%s' type=0x%02x size=%u\n", path, entry.name, entry.type, entry.size);

    bool ok = dusb_link_send_variable(entry.name, entry.type, entry.archived,
                                       entry.version, entry.data, entry.size);
    printf("[xfer] send_variable %s -> %d\n", path, ok);
    return ok;
}

// Sends every file in a single folder, doing its own connect handshake.
// Used for both modes -- short press sends just GAMES/, long-hold (5s+)
// sends just CESIUM/ (arTIfiCE + the Cesium installer). Keeping these
// separate makes each payload independently retestable/reflashable without
// re-sending the other.
static bool transfer_folder(const char *dir) {
    char files[MAX_FILES][64];
    int count = list_dir(dir, files, MAX_FILES);
    printf("[xfer] found %d file(s) in %s/\n", count, dir);

    if (!dusb_link_connect()) {
        printf("[xfer] dusb_link_connect FAILED\n");
        return false;
    }
    printf("[xfer] connect OK\n");

    bool all_ok = true;
    for (int i = 0; i < count; i++) {
        all_ok &= send_one_file(dir, files[i]);
    }
    return all_ok;
}

static bool do_transfer_games(void) {
    printf("[xfer] mode: GAMES\n");
    return transfer_folder("GAMES");
}

static bool do_transfer_cesium(void) {
    printf("[xfer] mode: CESIUM (arTIfiCE + installer)\n");
    return transfer_folder("CESIUM");
}

// ---- TinyUSB host callbacks -------------------------------------------------

void tuh_mount_cb(uint8_t dev_addr) {
    printf("[usb] tuh_mount_cb: device %u mounted\n", dev_addr);
    app_state = APP_IDLE_CALC_READY;
}

void tuh_umount_cb(uint8_t dev_addr) {
    printf("[usb] tuh_umount_cb: device %u unmounted\n", dev_addr);
    app_state = APP_IDLE_NO_CALC;
}

// ---- Main -------------------------------------------------------------------

int main(void) {
    // LED + boot-confirmation blink FIRST, before anything that could hang
    // (stdio, filesystem mount, USB init) -- this proves the chip booted and
    // is running our code at all, independent of everything downstream.
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    for (int i = 0; i < 3; i++) {
        gpio_put(PIN_LED, 1);
        sleep_ms(100);
        gpio_put(PIN_LED, 0);
        sleep_ms(100);
    }

    stdio_init_all();
    printf("\n\n[boot] field_mode starting\n");

    static FATFS fs;
    FRESULT mnt = f_mount(&fs, "", 1);
    if (mnt != FR_OK) {
        printf("[boot] f_mount failed (%d) -- has loader_mode been used to set up the volume yet?\n", mnt);
        // Don't hang here -- app_state stays APP_IDLE_NO_CALC until a calc
        // mounts, and any transfer attempt will simply fail (APP_ERROR)
        // since f_opendir/f_open will fail against an unmounted volume.
    } else {
        printf("[boot] f_mount OK\n");
    }

    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);

    printf("[boot] calling tuh_init on rhport %d\n", BOARD_TUH_RHPORT);
    tuh_init(BOARD_TUH_RHPORT);
    printf("[boot] tuh_init returned, entering main loop\n");

    uint64_t last_heartbeat_print_us = 0;
    bool prev_button_down = false;
    uint64_t press_start_us = 0;
    bool hold_armed = false; // whether the current hold has crossed HOLD_THRESHOLD_MS

    while (1) {
        tuh_task();
        led_task();

        // Prints roughly once a second so a serial terminal shows the loop
        // is still spinning, not frozen -- and shows which state we're in.
        uint64_t now = time_us_64();
        if (now - last_heartbeat_print_us > 1000000) {
            printf("[loop] alive, state=%d, tuh_mounted(1)=%d\n", (int)app_state, tuh_mounted(1));
            last_heartbeat_print_us = now;
        }

        bool down = button_down();

        if (down && !prev_button_down) {
            // just pressed
            press_start_us = now;
            hold_armed = false;
        }

        if (down && prev_button_down && !hold_armed &&
            (now - press_start_us) >= (uint64_t)HOLD_THRESHOLD_MS * 1000) {
            hold_armed = true;
            if (app_state == APP_IDLE_CALC_READY) {
                printf("[xfer] hold threshold reached -- release now for CESIUM mode\n");
                app_state = APP_HOLD_ARMED;
            }
        }

        if (!down && prev_button_down) {
            // just released
            if (app_state == APP_IDLE_CALC_READY || app_state == APP_HOLD_ARMED) {
                bool long_hold = hold_armed;
                printf("[xfer] button released after %s hold, starting transfer\n",
                       long_hold ? "LONG" : "short");
                app_state = APP_TRANSFERRING;
                bool ok = long_hold ? do_transfer_cesium() : do_transfer_games();
                printf("[xfer] finished, ok=%d\n", ok);
                app_state = ok ? APP_DONE : APP_ERROR;
            } else if (app_state == APP_DONE || app_state == APP_ERROR) {
                // Any tap after DONE/ERROR goes back to ready-if-still-mounted
                app_state = tuh_mounted(1) ? APP_IDLE_CALC_READY : APP_IDLE_NO_CALC;
            }
        }

        prev_button_down = down;
    }
}
