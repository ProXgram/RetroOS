#include "ata.h"
#include "io.h"
#include "syslog.h"
#include "kstdio.h" // Assuming you added kstdio from the previous step, otherwise use terminal

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define CMD_READ_PIO    0x20
#define CMD_WRITE_PIO   0x30
#define CMD_IDENTIFY    0xEC

#define STATUS_BSY      0x80
#define STATUS_DRQ      0x08
#define STATUS_ERR      0x01

static void ata_wait_bsy(void) {
    while (inb(ATA_STATUS) & STATUS_BSY);
}

static void ata_wait_drq(void) {
    while (!(inb(ATA_STATUS) & STATUS_DRQ));
}

/* 
 * Selects the drive and standard LBA28 parameters.
 * Note: 0xE0 = 11100000 (Mode=LBA, Drive=Master, Top 4 bits of LBA=0 for now)
 */
static void ata_select_drive(void) {
    outb(ATA_DRIVE_HEAD, 0xE0); 
}

bool ata_init(void) {
    // Check for floating bus
    uint8_t status = inb(ATA_STATUS);
    if (status == 0xFF) {
        syslog_write("ATA: No drive detected on Primary Bus");
        return false;
    }

    // Soft Reset (Optional, but good practice)
    ata_select_drive();
    io_wait();
    
    // Identify command to ensure drive exists and is working
    outb(ATA_SECTOR_CNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, CMD_IDENTIFY);
    
    status = inb(ATA_STATUS);
    if (status == 0) {
        syslog_write("ATA: Drive does not exist");
        return false;
    }

    ata_wait_bsy();
    
    // Read the identification data (256 words) to clear buffer
    // We don't store it for now, but we must read it.
    uint16_t tmp[256];
    insw(ATA_DATA, tmp, 256);

    syslog_write("ATA: Primary Master initialized (PIO Mode)");
    return true;
}

void ata_read(uint32_t lba, uint8_t count, uint8_t* buffer) {
    ata_wait_bsy();
    
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, count);
    outb(ATA_LBA_LOW, (uint8_t)(lba));
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, CMD_READ_PIO);

    for (int i = 0; i < count; i++) {
        ata_wait_bsy();
        ata_wait_drq();
        insw(ATA_DATA, buffer + (i * 512), 256);
    }
}

void ata_write(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    ata_wait_bsy();

    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, count);
    outb(ATA_LBA_LOW, (uint8_t)(lba));
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, CMD_WRITE_PIO);

    for (int i = 0; i < count; i++) {
        ata_wait_bsy();
        // Note: For write, we wait for DRQ *before* sending data
        ata_wait_drq();
        outsw(ATA_DATA, buffer + (i * 512), 256);
        
        // Flush cache / wait for write to finish
        outb(ATA_COMMAND, 0xE7); // Cache Flush (Optional but safe)
        ata_wait_bsy();
    }
}
