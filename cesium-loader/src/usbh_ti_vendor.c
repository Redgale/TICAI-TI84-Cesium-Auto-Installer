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
 * The usbh_class_driver_t type and usbh_app_driver_get_cb() declaration
 * live in host/usbh_pvt.h -- confirmed against hathach/tinyusb's current
 * source (github.com/hathach/tinyusb/blob/master/src/host/usbh.h and the
 * usbh_pvt.h it's paired with). That header is TinyUSB's internal
 * class-driver interface, not the stable tuh_* public API, so if a future
 * TinyUSB update changes field names here, this is the one file that would
 * need adjusting to match.
 */

#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "dusb_link.h"

static bool ti_open(uint8_t rhport, uint8_t daddr, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
    (void)rhport;

    printf("[usbh_ti] ti_open: daddr=%u itf_class=0x%02x itf_num=%u n_ep=%u\n",
           daddr, itf_desc->bInterfaceClass, itf_desc->bInterfaceNumber, itf_desc->bNumEndpoints);

    // Only claim vendor-specific interfaces (TI's calculators use 0xFF).
    if (itf_desc->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC) {
        printf("[usbh_ti] not vendor-specific, skipping\n");
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
            printf("[usbh_ti] found bulk endpoint 0x%02x\n", ep->bEndpointAddress);
            if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
                ep_in = ep->bEndpointAddress;
            } else {
                ep_out = ep->bEndpointAddress;
            }
            if (!tuh_edpt_open(daddr, ep)) {
                printf("[usbh_ti] tuh_edpt_open FAILED for 0x%02x\n", ep->bEndpointAddress);
                return false;
            }
        }
        p += p[0]; // advance by bLength of whatever descriptor this is
    }

    if (!ep_in || !ep_out) {
        printf("[usbh_ti] didn't find both bulk endpoints (in=0x%02x out=0x%02x)\n", ep_in, ep_out);
        return false; // didn't find the pair we expected
    }

    printf("[usbh_ti] claimed interface, ep_in=0x%02x ep_out=0x%02x\n", ep_in, ep_out);
    dusb_link_init(daddr, ep_in, ep_out);
    return true;
}

// --- Registration -----------------------------------------------------------

static bool ti_init(void) { return true; }
static bool ti_deinit(void) { return true; }
static bool ti_set_config(uint8_t daddr, uint8_t itf_num) {
    // No additional control transfers needed for DUSB -- but TinyUSB's
    // enumeration state machine won't proceed to tuh_mount_cb() until every
    // claimed interface's class driver explicitly reports completion here.
    usbh_driver_set_config_complete(daddr, itf_num);
    return true;
}
static bool ti_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)daddr; (void)ep_addr; (void)result; (void)xferred_bytes;
    return true; // dusb_link.c drives its own transfers synchronously; nothing to do here
}
static void ti_close(uint8_t daddr) { (void)daddr; }

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
    static usbh_class_driver_t const driver = {
#if CFG_TUSB_DEBUG >= 2
        .name       = "TI-DUSB",
#endif
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
