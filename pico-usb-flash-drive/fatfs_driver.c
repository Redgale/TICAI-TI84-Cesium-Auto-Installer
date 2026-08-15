#include "ff.h"
#include "diskio.h"

#include "flash.h"

#define FAT_MAGIC  (0x55AA)

static DSTATUS Stat = STA_NOINIT;


DSTATUS disk_status(BYTE drv) {
    if (drv != 0) return STA_NOINIT;
    return Stat;
}

DSTATUS disk_initialize(BYTE drv) {
    if (drv != 0) return STA_NOINIT;

    uint8_t block[FAT_BLOCK_SIZE];
    if (!flash_fat_read(0, block)) return STA_NOINIT;

    uint16_t magic = block[FAT_BLOCK_SIZE - 2] << 8 | block[FAT_BLOCK_SIZE - 1];
    if (magic == FAT_MAGIC) {
        Stat = 0;
        return Stat;
    }

    printf("initialize flash FAT12\n");
    flash_fat_initialize();

    Stat = 0;
    return Stat;
}

DRESULT disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count) {
    if (drv != 0 || buff == NULL || count == 0 || sector >= FAT_BLOCK_NUM ||
        count > FAT_BLOCK_NUM - sector) {
        return RES_PARERR;
    }

    for (UINT i = 0; i < count; i++) {
        if (!flash_fat_read((uint32_t)(sector + i), buff + i * FAT_BLOCK_SIZE)) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buff, LBA_t sector, UINT count) {
    if (drv != 0 || buff == NULL || count == 0 || sector >= FAT_BLOCK_NUM ||
        count > FAT_BLOCK_NUM - sector) {
        return RES_PARERR;
    }

    for (UINT i = 0; i < count; i++) {
        if (!flash_fat_write((uint32_t)(sector + i), buff + i * FAT_BLOCK_SIZE)) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buff) {
    if (drv != 0) return RES_PARERR;

    switch (ctrl) {
        case CTRL_SYNC:
            return RES_OK;
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
        case CTRL_TRIM:
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

DWORD get_fattime (void) {
    return 0;
}
