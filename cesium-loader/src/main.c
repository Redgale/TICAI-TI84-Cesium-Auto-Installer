/*
 * field_mode firmware — TI-84+ CE Cesium/game auto-loader
 *
 * Boots on battery, waits for the calculator to be plugged into the Pico's
 * USB-C port, and on a button press transfers files from CESIUM/ and
 * GAMES/ (read from onboard flash via FatFs) to the calculator.
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

typedef enum {
    APP_IDLE_NO_CALC,      // LED off — nothing connected
    APP_IDLE_CALC_READY,   // calc connected, waiting for button press
    APP_TRANSFERRING,      // LED blinking
    APP_DONE,              // LED solid
    APP_ERROR              // LED fast-blink (distinct from transferring)
} app_state_t;

static app_state_t app_state = APP_IDLE_NO_CALC;
static bool led_on = false;
static uint64_t last_blink_us = 0;

// Debounced button read
static bool button_pressed(void) {
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
        bool was_stable = last_stable;
        last_stable = reading;
        // Return true only on the rising edge into "pressed"
        return (!was_stable && last_stable);
    }
    return false;
}

static void led_task(void) {
    uint64_t now = time_us_64();
    switch (app_state) {
        case APP_IDLE_NO_CALC:
            gpio_put(PIN_LED, 0);
            led_on = false;
            break;
        case APP_IDLE_CALC_READY:
            gpio_put(PIN_LED, 1); // solid-on to say "ready, press button"
            led_on = true;
            break;
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
static char cesium_files[MAX_FILES][64];
static int  cesium_file_count = 0;
static char game_files[MAX_FILES][64];
static int  game_file_count = 0;

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

static void refresh_file_lists(void) {
    cesium_file_count = list_dir("CESIUM", cesium_files, MAX_FILES);
    game_file_count   = list_dir("GAMES", game_files, MAX_FILES);
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
    if (f_open(&f, path, FA_READ) != FR_OK) return false;

    UINT br = 0;
    FRESULT fr = f_read(&f, file_buf, sizeof(file_buf), &br);
    f_close(&f);
    if (fr != FR_OK) return false;

    Ti8xVarEntry entry;
    if (!ti8x_parse_regular_file(file_buf, br, &entry)) return false;

    return dusb_link_send_variable(entry.name, entry.type, entry.archived,
                                    entry.version, entry.data, entry.size);
}

static bool do_transfer(void) {
    refresh_file_lists();

    if (!dusb_link_connect()) return false;

    bool all_ok = true;
    for (int i = 0; i < cesium_file_count; i++) {
        all_ok &= send_one_file("CESIUM", cesium_files[i]);
    }
    for (int i = 0; i < game_file_count; i++) {
        all_ok &= send_one_file("GAMES", game_files[i]);
    }
    return all_ok;
}

// ---- TinyUSB host callbacks -------------------------------------------------

void tuh_mount_cb(uint8_t dev_addr) {
    app_state = APP_IDLE_CALC_READY;
}

void tuh_umount_cb(uint8_t dev_addr) {
    app_state = APP_IDLE_NO_CALC;
}

// ---- Main -------------------------------------------------------------------

int main(void) {
    stdio_init_all();

    static FATFS fs;
    FRESULT mnt = f_mount(&fs, "", 1);
    if (mnt != FR_OK) {
        printf("f_mount failed (%d) -- has loader_mode been used to set up the volume yet?\n", mnt);
        // Don't hang here -- app_state stays APP_IDLE_NO_CALC until a calc
        // mounts, and any transfer attempt will simply fail (APP_ERROR)
        // since f_opendir/f_open will fail against an unmounted volume.
    }

    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0);

    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);

    tuh_init(BOARD_TUH_RHPORT);

    while (1) {
        tuh_task();
        led_task();

        if (app_state == APP_IDLE_CALC_READY && button_pressed()) {
            app_state = APP_TRANSFERRING;
            bool ok = do_transfer();
            app_state = ok ? APP_DONE : APP_ERROR;
        }

        // Pressing again after DONE/ERROR goes back to ready-if-still-mounted
        if ((app_state == APP_DONE || app_state == APP_ERROR) && button_pressed()) {
            app_state = tuh_mounted(1) ? APP_IDLE_CALC_READY : APP_IDLE_NO_CALC;
        }
    }
}
