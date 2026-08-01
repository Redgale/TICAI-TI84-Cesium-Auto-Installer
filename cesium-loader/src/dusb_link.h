#ifndef DUSB_LINK_H
#define DUSB_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Texas Instruments USB vendor ID, and the product ID shared by the whole
// 83+/84+ USB family (including the 84+ CE) for the DUSB "DirectLink"
// protocol. Source: libticables/trunk/src/ticables.h + linux/link_usb1.cc.
#define TI_VID          0x0451
#define TI84P_PID       0xE003

// Default/negotiated buffer sizes (libticalcs dusb_cmd.h / dusb_vpkt.cc).
// The 83PCE/84+CE advertise more than they actually support, so the alloc
// reply gets clamped to 1018 -- ported verbatim from that clamp.
#define DUSB_DFL_BUF_SIZE   1024
#define DUSB_CE_MAX_BUF     1018
#define DUSB_MAX_RAW_DATA   1024   // must be >= DUSB_DFL_BUF_SIZE

bool dusb_link_init(uint8_t daddr, uint8_t ep_in, uint8_t ep_out);

// Runs the buffer-size negotiation + ping/mode-set handshake
// (dusb_cmd_s_mode_set + dusb_cmd_r_mode_ack in tilibs). Call this once
// right after the calculator enumerates, before sending any variables.
bool dusb_link_connect(void);

// Sends one variable (a parsed .8xp/.8xv entry) to the calculator:
// RTS -> wait ACK -> content -> wait ACK -> EOT. Mirrors calc_84p.cc's
// send_var() loop body for a single entry, targeting the 84+ CE
// (var-type attribute header byte 0x0F).
bool dusb_link_send_variable(const char *name, uint8_t type, bool archived,
                              uint8_t version, const uint8_t *data, uint32_t size);

#endif
