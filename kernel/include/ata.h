#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stdbool.h>

/* 
 * Initializes the ATA driver (Primary Bus, Master Drive).
 * Returns true if a drive is detected.
 */
bool ata_init(void);

/*
 * Reads 'count' sectors starting at LBA 'lba' into 'buffer'.
 * Buffer must be large enough (count * 512 bytes).
 */
void ata_read(uint32_t lba, uint8_t count, uint8_t* buffer);

/*
 * Writes 'count' sectors starting at LBA 'lba' from 'buffer'.
 */
void ata_write(uint32_t lba, uint8_t count, const uint8_t* buffer);

#endif /* ATA_H */
