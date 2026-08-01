/*
 * dusb_link.c — TI-84+ CE DUSB link protocol over a TinyUSB host bulk pipe.
 *
 * This is a direct port of the logic in three tilibs files, re-targeted from
 * libusb calls to tuh_edpt_xfer(). The packet formats, field order, and
 * command sequence below are copied from that source, not reconstructed
 * from memory:
 *
 *   libticalcs/trunk/src/dusb_rpkt.cc   -> raw packet framing (dusb_send/dusb_recv)
 *   libticalcs/trunk/src/dusb_vpkt.cc   -> virtual packet fragmentation, ACKs,
 *                                          buffer-size negotiation
 *   libticalcs/trunk/src/dusb_cmd.cc    -> mode_set/ping, RTS, EOT commands
 *   libticalcs/trunk/src/calc_84p.cc    -> the send_var() sequence and the
 *                                          84+CE-specific attribute header byte
 *
 * NOT ported / not needed for this project: OS transfers, screenshots,
 * directory listing, receiving variables back from the calculator.
 *
 * IMPORTANT CAVEAT: getting bulk transfers to the calculator's interface
 * requires TinyUSB to have claimed that interface as a vendor-specific class
 * during enumeration and opened its two bulk endpoints. TinyUSB's built-in
 * class drivers (CDC/MSC/HID) will NOT do this for a TI calculator, which
 * exposes a vendor-specific interface. See usbh_ti_vendor.c for that piece,
 * and note in its header comment that this touches TinyUSB's internal
 * (non-stable) class-driver API, which does shift between TinyUSB versions --
 * verify field names/order against the tusb version you're building against.
 */

#include <string.h>
#include "dusb_link.h"
#include "tusb.h"

// ---- Raw packet types (dusb_rpkt.h) ---------------------------------------
#define DUSB_RPKT_BUF_SIZE_REQ   1
#define DUSB_RPKT_BUF_SIZE_ALLOC 2
#define DUSB_RPKT_VIRT_DATA      3
#define DUSB_RPKT_VIRT_DATA_LAST 4
#define DUSB_RPKT_VIRT_DATA_ACK  5

// ---- Virtual packet types (dusb_vpkt.h) -----------------------------------
#define DUSB_VPKT_PING      0x0001
#define DUSB_VPKT_RTS       0x000B
#define DUSB_VPKT_VAR_CNTS  0x000D
#define DUSB_VPKT_MODE_SET  0x0012 // ack of mode-set/ping, confusingly same name as request
#define DUSB_VPKT_DATA_ACK  0xAA00
#define DUSB_VPKT_EOT       0xDD00
#define DUSB_VPKT_ERROR     0xEE00

#define DUSB_AID_VAR_TYPE     0x02
#define DUSB_AID_ARCHIVED     0x03
#define DUSB_AID_VAR_VERSION  0x08

#define DUSB_DH_SIZE 6 // 4-byte size + 2-byte type, prefixed on the first raw fragment of a vpkt

// 84+CE-specific: var-type attribute header byte (calc_84p.cc get_var_type_attr_header_byte)
#define CE_VAR_TYPE_HEADER_BYTE 0x0F

static uint8_t s_daddr = 0;
static uint8_t s_ep_in = 0;
static uint8_t s_ep_out = 0;
static uint32_t s_rpkt_maxlen = DUSB_DFL_BUF_SIZE;

// ---- Blocking bulk transfer wrapper ---------------------------------------

typedef struct {
    volatile bool done;
    volatile bool ok;
    volatile uint32_t actual_len;
} xfer_result_t;

static void xfer_cb(tuh_xfer_t *xfer) {
    xfer_result_t *r = (xfer_result_t *)xfer->user_data;
    r->ok = (xfer->result == XFER_RESULT_SUCCESS);
    r->actual_len = xfer->actual_len;
    r->done = true;
}

// Blocks (pumping tuh_task()) until the transfer completes or times out.
static bool bulk_xfer(uint8_t ep_addr, uint8_t *buf, uint16_t len, uint32_t *actual_len, uint32_t timeout_ms) {
    xfer_result_t result = {0};

    tuh_xfer_t xfer = {
        .daddr = s_daddr,
        .ep_addr = ep_addr,
        .buflen = len,
        .buffer = buf,
        .complete_cb = xfer_cb,
        .user_data = (uintptr_t)&result,
    };

    if (!tuh_edpt_xfer(&xfer)) {
        return false;
    }

    uint64_t start = time_us_64();
    while (!result.done) {
        tuh_task();
        if ((time_us_64() - start) > (uint64_t)timeout_ms * 1000) {
            return false;
        }
    }

    if (actual_len) *actual_len = result.actual_len;
    return result.ok;
}

#define XFER_TIMEOUT_MS 3000

// ---- Raw packet layer (dusb_rpkt.cc) --------------------------------------

static bool raw_send(uint8_t type, const uint8_t *data, uint32_t size) {
    uint8_t buf[DUSB_MAX_RAW_DATA + 5];
    if (size > DUSB_MAX_RAW_DATA) return false;

    buf[0] = (uint8_t)(size >> 24);
    buf[1] = (uint8_t)(size >> 16);
    buf[2] = (uint8_t)(size >> 8);
    buf[3] = (uint8_t)(size);
    buf[4] = type;
    if (size && data) memcpy(buf + 5, data, size);

    uint32_t actual;
    return bulk_xfer(s_ep_out, buf, (uint16_t)(size + 5), &actual, XFER_TIMEOUT_MS);
}

// Reads exactly one raw packet: 5-byte header, then `size` bytes of data.
static bool raw_recv(uint8_t *type, uint32_t *size, uint8_t *data, uint32_t data_cap) {
    uint8_t hdr[5];
    uint32_t actual;
    if (!bulk_xfer(s_ep_in, hdr, 5, &actual, XFER_TIMEOUT_MS) || actual < 5) return false;

    uint32_t sz = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
    uint8_t ty = hdr[4];

    if (sz > data_cap) return false;
    if (sz > 0) {
        if (!bulk_xfer(s_ep_in, data, (uint16_t)sz, &actual, XFER_TIMEOUT_MS) || actual < sz) return false;
    }

    *type = ty;
    *size = sz;
    return true;
}

// ---- Virtual packet layer (dusb_vpkt.cc) ----------------------------------

// Low-level 2-byte ACK exchanged after every raw fragment (both directions).
static bool send_low_ack(void) {
    uint8_t ack[2] = {0xE0, 0x00};
    return raw_send(DUSB_RPKT_VIRT_DATA_ACK, ack, 2);
}

static bool recv_low_ack(void) {
    uint8_t type; uint32_t size; uint8_t data[8];
    if (!raw_recv(&type, &size, data, sizeof(data))) return false;
    // Ported check is lenient (size == 2 or 4); we don't inspect contents.
    return (type == DUSB_RPKT_VIRT_DATA_ACK) && (size == 2 || size == 4);
}

// Sends a virtual packet, fragmenting across raw packets if needed and
// waiting for the low-level ACK after each fragment. Ported from
// dusb_send_data() in dusb_vpkt.cc.
static bool vpkt_send(uint16_t type, const uint8_t *data, uint32_t size) {
    uint8_t frame[DUSB_MAX_RAW_DATA];

    if (size <= s_rpkt_maxlen - DUSB_DH_SIZE) {
        // single fragment, also the last one
        frame[0] = (uint8_t)(size >> 24);
        frame[1] = (uint8_t)(size >> 16);
        frame[2] = (uint8_t)(size >> 8);
        frame[3] = (uint8_t)(size);
        frame[4] = (uint8_t)(type >> 8);
        frame[5] = (uint8_t)(type);
        if (size && data) memcpy(frame + DUSB_DH_SIZE, data, size);

        if (!raw_send(DUSB_RPKT_VIRT_DATA_LAST, frame, size + DUSB_DH_SIZE)) return false;
        return recv_low_ack();
    }

    // first fragment carries the data header
    uint32_t chunk = s_rpkt_maxlen - DUSB_DH_SIZE;
    frame[0] = (uint8_t)(size >> 24);
    frame[1] = (uint8_t)(size >> 16);
    frame[2] = (uint8_t)(size >> 8);
    frame[3] = (uint8_t)(size);
    frame[4] = (uint8_t)(type >> 8);
    frame[5] = (uint8_t)(type);
    memcpy(frame + DUSB_DH_SIZE, data, chunk);

    if (!raw_send(DUSB_RPKT_VIRT_DATA, frame, chunk + DUSB_DH_SIZE)) return false;
    if (!recv_low_ack()) return false;

    uint32_t offset = chunk;
    uint32_t remaining = size - offset;

    while (remaining > s_rpkt_maxlen) {
        memcpy(frame, data + offset, s_rpkt_maxlen);
        if (!raw_send(DUSB_RPKT_VIRT_DATA, frame, s_rpkt_maxlen)) return false;
        if (!recv_low_ack()) return false;
        offset += s_rpkt_maxlen;
        remaining -= s_rpkt_maxlen;
    }

    memcpy(frame, data + offset, remaining);
    if (!raw_send(DUSB_RPKT_VIRT_DATA_LAST, frame, remaining)) return false;
    return recv_low_ack();
}

// Receives a virtual packet, reassembling fragments and ACKing each one.
// Ported from dusb_recv_data_varsize() in dusb_vpkt.cc.
static bool vpkt_recv(uint16_t *type, uint8_t *out, uint32_t out_cap, uint32_t *out_size) {
    uint8_t raw_type;
    uint32_t raw_size;
    uint8_t frame[DUSB_MAX_RAW_DATA];
    uint32_t total = 0;
    bool first = true;
    uint16_t vpkt_type = 0;

    do {
        if (!raw_recv(&raw_type, &raw_size, frame, sizeof(frame))) return false;
        if (raw_type != DUSB_RPKT_VIRT_DATA && raw_type != DUSB_RPKT_VIRT_DATA_LAST) return false;

        if (first) {
            if (raw_size < DUSB_DH_SIZE) return false;
            vpkt_type = ((uint16_t)frame[4] << 8) | frame[5];
            uint32_t payload = raw_size - DUSB_DH_SIZE;
            if (payload > out_cap) return false;
            if (payload) memcpy(out, frame + DUSB_DH_SIZE, payload);
            total = payload;
            first = false;
        } else {
            if (total + raw_size > out_cap) return false;
            memcpy(out + total, frame, raw_size);
            total += raw_size;
        }

        if (!send_low_ack()) return false;
    } while (raw_type != DUSB_RPKT_VIRT_DATA_LAST);

    *type = vpkt_type;
    *out_size = total;
    return true;
}

// ---- Public API -------------------------------------------------------------

bool dusb_link_init(uint8_t daddr, uint8_t ep_in, uint8_t ep_out) {
    s_daddr = daddr;
    s_ep_in = ep_in;
    s_ep_out = ep_out;
    s_rpkt_maxlen = DUSB_DFL_BUF_SIZE;
    return true;
}

bool dusb_link_connect(void) {
    // 1. buffer size negotiation
    uint8_t req[4] = {
        (uint8_t)(DUSB_DFL_BUF_SIZE >> 24), (uint8_t)(DUSB_DFL_BUF_SIZE >> 16),
        (uint8_t)(DUSB_DFL_BUF_SIZE >> 8),  (uint8_t)(DUSB_DFL_BUF_SIZE)
    };
    if (!raw_send(DUSB_RPKT_BUF_SIZE_REQ, req, 4)) return false;

    uint8_t type; uint32_t size; uint8_t data[8];
    if (!raw_recv(&type, &size, data, sizeof(data))) return false;
    if (type != DUSB_RPKT_BUF_SIZE_ALLOC || size != 4) return false;

    uint32_t alloc = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    if (alloc > DUSB_CE_MAX_BUF) alloc = DUSB_CE_MAX_BUF; // 83PCE/84+CE over-advertise; clamp as tilibs does
    s_rpkt_maxlen = alloc;

    // 2. ping / mode set: mode = { 3, 1, 0, 0, 0x07d0 } (DUSB_MODE_NORMAL)
    uint8_t mode[10] = {
        0x00, 0x03,  // arg1 = 3
        0x00, 0x01,  // arg2 = 1
        0x00, 0x00,  // arg3 = 0
        0x00, 0x00,  // arg4 = 0
        0x07, 0xd0,  // arg5 = 0x07d0
    };
    if (!vpkt_send(DUSB_VPKT_PING, mode, sizeof(mode))) return false;

    // 3. wait for the calculator's mode-set acknowledgement
    uint16_t rtype; uint32_t rsize;
    uint8_t rbuf[16];
    if (!vpkt_recv(&rtype, rbuf, sizeof(rbuf), &rsize)) return false;
    if (rtype == DUSB_VPKT_ERROR) return false;
    return rtype == DUSB_VPKT_MODE_SET;
}

bool dusb_link_send_variable(const char *name, uint8_t var_type, bool archived,
                              uint8_t version, const uint8_t *data, uint32_t size) {
    // Build the 3 attributes exactly as calc_84p.cc::send_var does for the
    // 84+ family, using the CE's header byte (0x0F).
    uint8_t attr_type[4]    = { 0xF0, CE_VAR_TYPE_HEADER_BYTE, 0x00, var_type };
    uint8_t attr_archived[1]= { archived ? 1 : 0 };
    uint8_t attr_version[4] = { 0x00, 0x00, 0x00, version };

    // --- Build the RTS (Request To Send) virtual packet payload ---
    // Layout (dusb_cmd_s_rts2): [folder_len=0][name_len][name+NUL]
    //   [4-byte BE size][modeflag=0x01][2-byte BE nattrs]
    //   { [2-byte BE attr id][2-byte BE attr size][attr data] } x3
    char nm[9];
    strncpy(nm, name, 8);
    nm[8] = 0;
    uint8_t name_len = (uint8_t)strlen(nm);

    uint8_t rts[64];
    uint32_t j = 0;
    rts[j++] = 0; // empty folder
    rts[j++] = name_len;
    memcpy(rts + j, nm, name_len + 1); j += name_len + 1;
    rts[j++] = (uint8_t)(size >> 24);
    rts[j++] = (uint8_t)(size >> 16);
    rts[j++] = (uint8_t)(size >> 8);
    rts[j++] = (uint8_t)(size);
    rts[j++] = 0x01; // modeflag: "silent" RTS

    rts[j++] = 0x00; rts[j++] = 0x03; // nattrs = 3

    rts[j++] = 0x00; rts[j++] = DUSB_AID_VAR_TYPE;
    rts[j++] = 0x00; rts[j++] = 0x04;
    memcpy(rts + j, attr_type, 4); j += 4;

    rts[j++] = 0x00; rts[j++] = DUSB_AID_ARCHIVED;
    rts[j++] = 0x00; rts[j++] = 0x01;
    memcpy(rts + j, attr_archived, 1); j += 1;

    rts[j++] = 0x00; rts[j++] = DUSB_AID_VAR_VERSION;
    rts[j++] = 0x00; rts[j++] = 0x04;
    memcpy(rts + j, attr_version, 4); j += 4;

    if (!vpkt_send(DUSB_VPKT_RTS, rts, j)) return false;

    // Wait for DATA_ACK before sending content
    uint16_t rtype; uint32_t rsize; uint8_t rbuf[16];
    if (!vpkt_recv(&rtype, rbuf, sizeof(rbuf), &rsize)) return false;
    if (rtype != DUSB_VPKT_DATA_ACK) return false;

    // Send the variable's raw contents (fragmented internally if large)
    if (!vpkt_send(DUSB_VPKT_VAR_CNTS, data, size)) return false;

    if (!vpkt_recv(&rtype, rbuf, sizeof(rbuf), &rsize)) return false;
    if (rtype != DUSB_VPKT_DATA_ACK) return false;

    // End of transmission for this variable (calc_84p.cc doesn't wait for
    // an ack here either -- it just pauses briefly before the next variable)
    if (!vpkt_send(DUSB_VPKT_EOT, NULL, 0)) return false;

    return true;
}
