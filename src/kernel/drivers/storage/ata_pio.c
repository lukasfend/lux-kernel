/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Minimal ATA PIO driver supporting 28-bit LBA transfers for the primary master disk.
 */
#include <lux/ata.h>
#include <lux/io.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ATA_PRIMARY_IO          0x1F0u
#define ATA_PRIMARY_CTRL        0x3F6u

#define ATA_REG_DATA            (ATA_PRIMARY_IO + 0)
#define ATA_REG_ERROR           (ATA_PRIMARY_IO + 1)
#define ATA_REG_FEATURES        (ATA_PRIMARY_IO + 1)
#define ATA_REG_SECCOUNT0       (ATA_PRIMARY_IO + 2)
#define ATA_REG_LBA0            (ATA_PRIMARY_IO + 3)
#define ATA_REG_LBA1            (ATA_PRIMARY_IO + 4)
#define ATA_REG_LBA2            (ATA_PRIMARY_IO + 5)
#define ATA_REG_HDDEVSEL        (ATA_PRIMARY_IO + 6)
#define ATA_REG_COMMAND         (ATA_PRIMARY_IO + 7)
#define ATA_REG_STATUS          (ATA_PRIMARY_IO + 7)
#define ATA_REG_ALTSTATUS       ATA_PRIMARY_CTRL
#define ATA_REG_CONTROL         ATA_PRIMARY_CTRL

#define ATA_CMD_IDENTIFY        0xECu
#define ATA_CMD_READ_PIO        0x20u
#define ATA_CMD_WRITE_PIO       0x30u
#define ATA_CMD_CACHE_FLUSH     0xE7u

#define ATA_SR_BSY              0x80u
#define ATA_SR_DRDY             0x40u
#define ATA_SR_DF               0x20u
#define ATA_SR_DRQ              0x08u
#define ATA_SR_ERR              0x01u

#define ATA_DCR_nIEN            0x02u

#define ATA_TRANSFER_MAX        128u
#define ATA_TIMEOUT             1000000u

struct ata_state {
    bool ready;
    uint32_t total_sectors;
};

static struct ata_state ata_ctx;

static inline void ata_delay_400ns(void)
{
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
}

static bool ata_wait_not_busy(void)
{
    uint32_t timeout = ATA_TIMEOUT;
    while (timeout--) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return true;
        }
    }
    return false;
}

static bool ata_wait_drq(void)
{
    uint32_t timeout = ATA_TIMEOUT;
    while (timeout--) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            return false;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return true;
        }
    }
    return false;
}

static void ata_select_drive(uint32_t lba)
{
    outb(ATA_REG_HDDEVSEL, 0xE0u | (uint8_t)((lba >> 24) & 0x0Fu));
    ata_delay_400ns();
}

static bool ata_transfer(uint32_t lba, uint16_t sector_count, void *buffer, bool write)
{
    if (!sector_count || sector_count > ATA_TRANSFER_MAX || !buffer) {
        return false;
    }

    ata_select_drive(lba);
    outb(ATA_REG_FEATURES, 0);
    outb(ATA_REG_SECCOUNT0, (uint8_t)sector_count);
    outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFFu));
    outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
    outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
    outb(ATA_REG_COMMAND, write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);

    uint8_t *byte_cursor = (uint8_t *)buffer;
    for (uint16_t sector = 0; sector < sector_count; ++sector) {
        if (!ata_wait_drq()) {
            return false;
        }

        if (write) {
            const uint16_t *src = (const uint16_t *)byte_cursor;
            for (uint16_t i = 0; i < ATA_SECTOR_SIZE / 2; ++i) {
                outw(ATA_REG_DATA, src[i]);
            }
        } else {
            uint16_t *dst = (uint16_t *)byte_cursor;
            for (uint16_t i = 0; i < ATA_SECTOR_SIZE / 2; ++i) {
                dst[i] = inw(ATA_REG_DATA);
            }
        }

        byte_cursor += ATA_SECTOR_SIZE;
        ata_delay_400ns();
    }

    if (write) {
        outb(ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        ata_wait_not_busy();
    }

    return true;
}

bool ata_pio_init(void)
{
    memset(&ata_ctx, 0, sizeof(ata_ctx));

    outb(ATA_REG_CONTROL, ATA_DCR_nIEN);
    ata_delay_400ns();

    ata_select_drive(0);
    outb(ATA_REG_SECCOUNT0, 0);
    outb(ATA_REG_LBA0, 0);
    outb(ATA_REG_LBA1, 0);
    outb(ATA_REG_LBA2, 0);
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_REG_STATUS);
    if (!status) {
        return false;
    }

    while (status & ATA_SR_BSY) {
        status = inb(ATA_REG_STATUS);
    }

    if (status & ATA_SR_ERR) {
        return false;
    }

    if (!ata_wait_drq()) {
        return false;
    }

    uint16_t identify_data[256];
    for (size_t i = 0; i < 256; ++i) {
        identify_data[i] = inw(ATA_REG_DATA);
    }

    ata_ctx.total_sectors = ((uint32_t)identify_data[61] << 16) | identify_data[60];
    ata_ctx.ready = ata_ctx.total_sectors != 0;
    return ata_ctx.ready;
}

bool ata_pio_ready(void)
{
    return ata_ctx.ready;
}

uint32_t ata_pio_total_sectors(void)
{
    return ata_ctx.total_sectors;
}

bool ata_pio_read(uint32_t lba, uint16_t sector_count, void *buffer)
{
    if (!ata_ctx.ready || !sector_count || !buffer) {
        return false;
    }

    uint8_t *cursor = (uint8_t *)buffer;
    while (sector_count) {
        uint16_t chunk = (sector_count > ATA_TRANSFER_MAX) ? ATA_TRANSFER_MAX : sector_count;
        if (!ata_transfer(lba, chunk, cursor, false)) {
            return false;
        }
        sector_count -= chunk;
        lba += chunk;
        cursor += (size_t)chunk * ATA_SECTOR_SIZE;
    }

    return true;
}

bool ata_pio_write(uint32_t lba, uint16_t sector_count, const void *buffer)
{
    if (!ata_ctx.ready || !sector_count || !buffer) {
        return false;
    }

    const uint8_t *cursor = (const uint8_t *)buffer;
    while (sector_count) {
        uint16_t chunk = (sector_count > ATA_TRANSFER_MAX) ? ATA_TRANSFER_MAX : sector_count;
        if (!ata_transfer(lba, chunk, (void *)cursor, true)) {
            return false;
        }
        sector_count -= chunk;
        lba += chunk;
        cursor += (size_t)chunk * ATA_SECTOR_SIZE;
    }

    return true;
}
