#include <string.h>
#include "hardware/flash.h"
#include "flash_ro.h"

// RP2040 flash is memory-mapped for reads at XIP_BASE -- same technique
// pico-usb-flash-drive's flash_fat_read() uses, just without any of the
// erase/write logic since field_mode never modifies this volume.
bool flash_fat_read_ro(int block, uint8_t *buffer) {
    if (block < 0 || block >= FAT_BLOCK_NUM) return false;
    const uint8_t *data = (const uint8_t *)(XIP_BASE + FLASH_FAT_OFFSET + FAT_BLOCK_SIZE * block);
    memcpy(buffer, data, FAT_BLOCK_SIZE);
    return true;
}
