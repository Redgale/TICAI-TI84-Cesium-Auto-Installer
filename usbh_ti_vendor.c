/*
 * usbh_ti_vendor.c — minimal TinyUSB host class driver that claims the TI
 * calculator's vendor-specific USB interface and opens its two bulk
 * endpoints, so dusb_link.c has something to talk to.
 *
 * WHY THIS FILE EXISTS: TinyUSB's built-in host class drivers (CDC, MSC,
 * HID) only claim interfaces that declare one of those standard USB
 * classes. The TI-84+ CE's DUSB interface is vendor-specific
 * (bInterfaceClass = 0xFF), so nothing claims it or opens its endpoints
 * unless we register a class driver ourselves via
 * usbh_app_driver_get_cb().
 *
 * VERSION CAVEAT: this hooks into TinyUSB's internal host class-driver
 * table (usbh_class_driver_t / tuh_class_driver_t depending on version),
 * which is NOT part of TinyUSB's stable public API and has changed field
 * names/order across releases. Check the struct definition in your
 * installed copy of tinyusb's src/host/usbh_pvt.h (or usbh.h, depending on
 * version) and adjust the field names below to match before this will
 * compile. This file gets you the right *shape* of the solution (claim
 * interface -> find two bulk endpoints -> tuh_edpt_open both -> tell
 * dusb_link.c) ported from how TinyUSB's own MSC/vendor examples do it,
 * but the exact struct layout needs a one-time check against your SDK
 * version.
 */

#include <string.h>
#include "tusb.h"
#include "dusb_link.h"

static bool ti_open(uint8_t rhport, uint8_t daddr, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
    (void)rhport;

    // Only claim vendor-specific interfaces (TI's calculators use 0xFF).
    if (itf_desc->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC) {
        return false;
    }

    uint8_t const *p = (uint8_t const *)itf_desc;
    uint8_t const *end = p + max_len;
    p += itf_desc->bLength;

    uint8_t ep_in = 0, ep_out = 0;

    while (p < end && itf_desc->bNumEndpoints >= 1) {
        tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
        if (ep->bDescriptorType == TUSB_DESC_ENDPOINT &&
            ep->bmAttributes.xfer == TUSB_XFER_BULK) {
            if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
                ep_in = ep->bEndpointAddress;
            } else {
                ep_out = ep->bEndpointAddress;
            }
            if (!tuh_edpt_open(daddr, ep)) {
                return false;
            }
        }
        p += p[0]; // advance by bLength of whatever descriptor this is
    }

    if (!ep_in || !ep_out) {
        return false; // didn't find the pair we expected
    }

    dusb_link_init(daddr, ep_in, ep_out);
    return true;
}

// --- Registration -----------------------------------------------------------
// The exact struct name/fields are the version-sensitive part mentioned
// above. This mirrors the shape TinyUSB's own examples/host vendor class
// stub uses: init/open/set_config/xfer_cb/close.

static bool ti_init(void) { return true; }
static bool ti_deinit(void) { return true; }
static bool ti_set_config(uint8_t daddr, uint8_t itf_num) {
    (void)daddr; (void)itf_num;
    tuh_vid_pid_t vid_pid;
    (void)vid_pid;
    return true; // no additional SET_CONFIGURATION step needed for DUSB
}
static bool ti_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)daddr; (void)ep_addr; (void)result; (void)xferred_bytes;
    return true; // dusb_link.c drives its own transfers synchronously; nothing to do here
}
static void ti_close(uint8_t daddr) { (void)daddr; }

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
    static usbh_class_driver_t const driver = {
        .name       = "TI-DUSB",
        .init       = ti_init,
        .deinit     = ti_deinit,
        .open       = ti_open,
        .set_config = ti_set_config,
        .xfer_cb    = ti_xfer_cb,
        .close      = ti_close,
    };
    *driver_count = 1;
    return &driver;
}
