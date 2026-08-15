/*
 * fatfs_driver_ro.c -- FatFs diskio glue for field_mode.
 *
 * Mirrors pico-usb-flash-drive's fatfs_driver.c function-for-function
 * (disk_status/disk_initialize/disk_read/disk_write/disk_ioctl/get_fattime
 * are FatFs's required diskio interface), but:
 *   - disk_read pulls from flash_fat_read_ro() (memory-mapped, no erase)
 *   - disk_write is a no-op that reports failure -- field_mode must never
 *     write to this volume, only loader_mode owns writes
 *   - disk_initialize does NOT call any flash_fat_initialize()-equivalent.
 *     If the FAT12 magic isn't found, that means loader_mode hasn't set up
 *     the volume yet -- field_mode reports that back rather than silently
 *     formatting a blank volume out from under it.
 */

#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "flash_ro.h"

#define FAT_MAGIC (0x55AA)

static DSTATUS disk_stat = STA_NOINIT;

DSTATUS disk_status(BYTE drv) {
    if (drv != 0) return STA_NOINIT;
    return disk_stat;
}

DSTATUS disk_initialize(BYTE drv) {
    if (drv != 0) return STA_NOINIT;
    uint8_t block[FAT_BLOCK_SIZE];
    flash_fat_read_ro(0, block);

    uint16_t magic = (uint16_t)(block[FAT_BLOCK_SIZE - 2] << 8 | block[FAT_BLOCK_SIZE - 1]);
    if (magic == FAT_MAGIC) {
        disk_stat = 0; // ready
    } else {
        // Volume not initialized -- loader_mode needs to be flashed and
        // used first. Do NOT format it from here.
        disk_stat = STA_NOINIT;
    }
    return disk_stat;
}

DRESULT disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count) {
    if (drv != 0 || buff == NULL || count == 0 || sector >= FAT_BLOCK_NUM ||
        count > FAT_BLOCK_NUM - sector) {
        return RES_PARERR;
    }
    for (UINT i = 0; i < count; i++) {
        if (!flash_fat_read_ro((int)(sector + i), buff + (i * FAT_BLOCK_SIZE))) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buff, LBA_t sector, UINT count) {
    if (drv != 0) return RES_PARERR;
    (void)buff; (void)sector; (void)count;
    return RES_WRPRT; // write-protected: field_mode never writes here
}

DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buff) {
    if (drv != 0) return RES_PARERR;
    switch (ctrl) {
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_COUNT:
            if (buff == NULL) return RES_PARERR;
            *(LBA_t *)buff = FAT_BLOCK_NUM;
            return RES_OK;
        case GET_SECTOR_SIZE:
            if (buff == NULL) return RES_PARERR;
            *(WORD *)buff = FAT_BLOCK_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            if (buff == NULL) return RES_PARERR;
            *(DWORD *)buff = FAT_ERASE_BLOCK_SECTORS;
            return RES_OK;
        case CTRL_TRIM: return RES_OK;
        default:
            return RES_PARERR;
    }
}

DWORD get_fattime(void) {
    return 0;
}
