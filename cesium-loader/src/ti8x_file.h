#ifndef TI8X_FILE_H
#define TI8X_FILE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char     name[9];   // trimmed variable name, up to 8 chars + NUL
    uint8_t  type;       // TI variable type byte (e.g. program, appvar)
    bool     archived;
    uint8_t  version;
    uint32_t size;        // size of `data` in bytes
    const uint8_t *data;  // points into the caller's file buffer
} Ti8xVarEntry;

// Parses a single-entry standard 83+/84+ family .8xp/.8xv file already
// loaded into `buf` (length `len`). Returns false if the file doesn't look
// like a valid regular TI variable file. `out->data` points back into `buf`,
// so `buf` must stay valid as long as `out` is used.
bool ti8x_parse_regular_file(const uint8_t *buf, size_t len, Ti8xVarEntry *out);

#endif
