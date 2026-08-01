#ifndef FLASH_RO_H
#define FLASH_RO_H

#include <stdint.h>
#include <stdbool.h>

/*
 * These four constants MUST match flash.h in your pico-usb-flash-drive
 * fork exactly, or field_mode will read garbage / the wrong flash region.
 * If you change FLASH_FAT_OFFSET or FAT_BLOCK_NUM over there (e.g. to make
 * the volume bigger than 64KB), mirror the change here too.
 */
#define FLASH_FAT_OFFSET   0x1F0000
#define FAT_BLOCK_NUM      128   // 64KB
#define FAT_BLOCK_SIZE     512

// Memory-mapped read only -- no erase, no write. Safe to call any time.
bool flash_fat_read_ro(int block, uint8_t *buffer);

#endif
