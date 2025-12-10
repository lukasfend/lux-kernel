/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Minimal ATA PIO disk interface for sector access.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATA_SECTOR_SIZE 512u

bool ata_pio_init(void);
bool ata_pio_ready(void);
bool ata_pio_read(uint32_t lba, uint16_t sector_count, void *buffer);
bool ata_pio_write(uint32_t lba, uint16_t sector_count, const void *buffer);
uint32_t ata_pio_total_sectors(void);
