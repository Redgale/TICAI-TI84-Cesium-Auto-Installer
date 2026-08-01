#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT 0
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

// This build is HOST role only -- see CMakeLists.txt
// (CFG_TUSB_RHPORT0_MODE = OPT_MODE_HOST).
#define CFG_TUH_ENABLED       1
#define CFG_TUH_MAX_SPEED     OPT_MODE_DEFAULT_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

// One device (the calculator) at a time.
#define CFG_TUH_DEVICE_MAX    1

// We don't use TinyUSB's built-in host classes -- our own vendor driver in
// usbh_ti_vendor.c claims the calculator's interface instead.
#define CFG_TUH_HUB           0
#define CFG_TUH_CDC           0
#define CFG_TUH_HID           0
#define CFG_TUH_MSC           0
#define CFG_TUH_VENDOR        0

// Required so TinyUSB calls usbh_app_driver_get_cb() to pick up our
// custom class driver from usbh_ti_vendor.c.
#define CFG_TUH_API_EDPT_XFER 1

#ifdef __cplusplus
}
#endif

#endif
