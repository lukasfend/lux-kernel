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

/**
 * Delay approximately 400 nanoseconds required by ATA device timing.
 *
 * Performs four reads of the controller's alternate status register to
 * generate the required ~400ns delay between ATA register operations.
 */
static inline void ata_delay_400ns(void)
{
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
}

/**
 * Waits until the ATA device clears the BSY (busy) status or the timeout expires.
 *
 * @returns `true` if the device is no longer busy (BSY cleared), `false` if the timeout elapsed while still busy.
 */
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

/**
 * Waits until the ATA device sets DRQ (data request) or an error/device fault occurs.
 *
 * Polls the status register until BSY is cleared and DRQ is set, or until a timeout
 * expires. If an ERR or DF status bit is observed the function returns immediately.
 *
 * @returns `true` if DRQ was observed before timeout and no error/device fault occurred, `false` otherwise.
 */
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

/**
 * Selects the ATA drive/head corresponding to the high 4 bits of a 28-bit LBA and waits 400 ns.
 * @param lba Logical block address; only bits 24–27 (the high 4 bits) are used to set the drive/head select.
 */
static void ata_select_drive(uint32_t lba)
{
    outb(ATA_REG_HDDEVSEL, 0xE0u | (uint8_t)((lba >> 24) & 0x0Fu));
    ata_delay_400ns();
}

/**
 * Perform a PIO data transfer of consecutive sectors starting at the specified LBA.
 *
 * Transfers up to ATA_TRANSFER_MAX sectors using 28-bit LBA addressing; on success the full
 * requested sector_count are read from or written to the device into the provided buffer.
 *
 * @param lba Starting sector address (28-bit LBA).
 * @param sector_count Number of sectors to transfer (must be between 1 and ATA_TRANSFER_MAX).
 * @param buffer Pointer to a buffer of at least (ATA_SECTOR_SIZE * sector_count) bytes.
 *               For writes, this is the source; for reads, this is the destination.
 * @param write If true perform a write (buffer -> device); if false perform a read (device -> buffer).
 * @returns `true` if all sectors were transferred successfully, `false` on any error or timeout.
 */
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

/**
 * Initialize the primary-master ATA PIO driver and populate driver state.
 *
 * Performs device discovery via the IDENTIFY command and fills internal state
 * (ata_ctx.total_sectors and ata_ctx.ready) when a valid device is found.
 *
 * @returns `true` if a device was identified and total sectors > 0, `false` otherwise (e.g., no device present, device reported an error, or DRQ readiness timed out).
 */
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

/**
 * Report whether the ATA PIO driver has successfully initialized and identified the device.
 *
 * @returns `true` if the device is initialized and ready for transfers, `false` otherwise.
 */
bool ata_pio_ready(void)
{
    return ata_ctx.ready;
}

/**
 * Get the total number of sectors reported by the primary master ATA device.
 *
 * @returns Total number of 28-bit LBA sectors on the device; returns `0` if the driver is not initialized or identification failed.
 */
uint32_t ata_pio_total_sectors(void)
{
    return ata_ctx.total_sectors;
}

/**
 * Read one or more 512-byte sectors from the primary master ATA device starting at the given LBA into a caller-provided buffer.
 *
 * @param lba Logical block address of the first sector to read (28-bit LBA range).
 * @param sector_count Number of sectors to read.
 * @param buffer Destination buffer; must be at least `sector_count * ATA_SECTOR_SIZE` bytes.
 * @returns `true` if all requested sectors were read successfully, `false` otherwise (device not ready, invalid arguments, or transfer failure).
 */
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

/**
 * Write consecutive 512-byte sectors to the primary master starting at the given LBA.
 *
 * @param lba Starting logical block address for the write.
 * @param sector_count Number of sectors to write.
 * @param buffer Pointer to the source data; must contain at least `sector_count * ATA_SECTOR_SIZE` bytes.
 * @returns `true` if all sectors were written successfully, `false` on invalid input, if the device is not ready, or if any transfer fails.
 */
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