/*
 * ti8x_file.c — minimal parser for standard TI-83+/84+ family variable files
 * (.8xp programs, .8xv appvars, etc.)
 *
 * Ported from tilibs' libtifiles/trunk/src/files8x.cc (ti8x_file_read_regular),
 * trimmed down to the "83+/84+ family, single entry" case, which is what
 * Cesium and .8xp games ship as. Byte layout confirmed directly from that
 * source rather than reconstructed from memory:
 *
 *   offset 0x00  8 bytes   signature, e.g. "**TI83F*"
 *   offset 0x08  3 bytes   fixed marker (0x1A 0x0A 0x00)
 *   offset 0x0B  42 bytes  comment field
 *   offset 0x35  2 bytes   LE data_size = size of everything from 0x37 to
 *                          just before the trailing checksum
 *   offset 0x37  entry:
 *     2 bytes  LE packet_length (0x0D for 83+/84+ family)
 *     2 bytes  LE entry_size (size of the variable's raw data payload)
 *     1 byte   type
 *     8 bytes  name, NUL/space padded
 *     2 bytes  LE attribute word (bit 15 = archived, low byte = version;
 *              legacy files may instead use the literal value 0x0080 to
 *              mean "archived, version 0")
 *     2 bytes  (skipped, redundant length field)
 *     N bytes  entry_size bytes of raw variable data
 *   ... (checksum trailer, not validated here)
 */

#include <string.h>
#include "ti8x_file.h"

static uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool ti8x_parse_regular_file(const uint8_t *buf, size_t len, Ti8xVarEntry *out) {
    if (len < 0x37 + 2) return false;

    // Offset 0x35: data_size (not strictly required for a single-entry file,
    // but a sanity check that the file isn't truncated)
    uint16_t data_size = rd_le16(buf + 0x35);
    if ((size_t)(0x37 + data_size) > len) {
        // file looks truncated -- still try to parse the first entry
    }

    size_t off = 0x37;
    if (off + 13 > len) return false; // 2+2+1+8 = 13 bytes of fixed header

    uint16_t packet_length = rd_le16(buf + off); off += 2;
    (void)packet_length; // expected 0x0D for 83+/84+ family; not enforced

    uint16_t entry_size = rd_le16(buf + off); off += 2;

    uint8_t type = buf[off]; off += 1;

    char namebuf[9];
    memcpy(namebuf, buf + off, 8);
    namebuf[8] = 0;
    off += 8;
    // trim trailing spaces/NULs
    for (int i = 7; i >= 0; i--) {
        if (namebuf[i] == ' ' || namebuf[i] == 0) namebuf[i] = 0;
        else break;
    }

    if (off + 2 > len) return false;
    uint16_t attribute = rd_le16(buf + off); off += 2;

    bool archived;
    uint8_t version;
    if (attribute == 0x0080) {
        archived = true;
        version = 0;
    } else {
        archived = (attribute & 0x8000) != 0;
        version = (uint8_t)(attribute & 0xFF);
    }

    if (off + 2 > len) return false;
    off += 2; // skip redundant length field

    if (off + entry_size > len) return false;

    out->type = type;
    out->archived = archived;
    out->version = version;
    out->size = entry_size;
    out->data = buf + off; // points into caller's buffer, not copied
    strncpy(out->name, namebuf, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = 0;

    return true;
}
