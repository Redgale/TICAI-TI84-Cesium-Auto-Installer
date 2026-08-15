#ifndef _FLASH_H_
#define _FLASH_H_

#include <ctype.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>


#define FLASH_FAT_BLOCK_SIZE       FLASH_SECTOR_SIZE
#define FLASH_FAT_OFFSET           0x180000u
#define FAT_BLOCK_NUM              1024u  // 512 KiB total volume size
#define FAT_BLOCK_SIZE             512u
#define FAT_ROOT_ENTRY_COUNT       64u
#define FAT_TABLE_SECTOR_COUNT     3u
#define FAT_ROOT_DIR_SECTOR_COUNT  ((FAT_ROOT_ENTRY_COUNT * 32u + FAT_BLOCK_SIZE - 1u) / FAT_BLOCK_SIZE)
#define FAT_METADATA_SECTOR_COUNT  (1u + FAT_TABLE_SECTOR_COUNT + FAT_ROOT_DIR_SECTOR_COUNT)
#define FAT_DATA_CLUSTER_COUNT     (FAT_BLOCK_NUM - FAT_METADATA_SECTOR_COUNT)
#define FAT_VOLUME_SIZE_BYTES      (FAT_BLOCK_NUM * FAT_BLOCK_SIZE)
#define FAT_ERASE_BLOCK_SECTORS    (FLASH_FAT_BLOCK_SIZE / FAT_BLOCK_SIZE)

_Static_assert((FLASH_FAT_OFFSET % FLASH_SECTOR_SIZE) == 0,
               "FAT flash offset must be erase-sector aligned");
_Static_assert((FAT_VOLUME_SIZE_BYTES % FLASH_SECTOR_SIZE) == 0,
               "FAT volume must contain a whole number of flash erase sectors");
_Static_assert((FLASH_FAT_OFFSET + FAT_VOLUME_SIZE_BYTES) <= PICO_FLASH_SIZE_BYTES,
               "FAT volume extends past the end of onboard flash");
_Static_assert((((FAT_DATA_CLUSTER_COUNT + 2u) * 3u + 1u) / 2u) <=
                   (FAT_TABLE_SECTOR_COUNT * FAT_BLOCK_SIZE),
               "FAT12 table is too small for the configured volume");


void flash_fat_initialize(void);
bool flash_fat_read(uint32_t block, uint8_t *buffer);
bool flash_fat_write(uint32_t block, const uint8_t *buffer);

#endif
