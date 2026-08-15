#ifndef FLASH_RO_H
#define FLASH_RO_H

#include <stdint.h>
#include <stdbool.h>

/*
 * These three constants MUST match flash.h in your pico-usb-flash-drive
 * fork exactly, or field_mode will read garbage / the wrong flash region.
 * Currently sized for a 512KB volume (1024 sectors @ 512 bytes). If you
 * resize the volume again over there, mirror the change here too.
 */
#define FLASH_FAT_OFFSET   0x180000
#define FAT_BLOCK_NUM      1024  // 512KB
#define FAT_BLOCK_SIZE     512
#define FAT_ERASE_BLOCK_SECTORS 8

// Memory-mapped read only -- no erase, no write. Safe to call any time.
bool flash_fat_read_ro(int block, uint8_t *buffer);

#endif
