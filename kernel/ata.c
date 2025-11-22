#include "ata.h"
#include "io.h"
#include "syslog.h"
#include "kstdio.h"

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

// 100,000 iterations is plenty for PIO in QEMU
// If it takes longer, the drive is likely stuck.
#define ATA_TIMEOUT     100000 

static bool ata_wait_bsy(void) {
    int timeout = ATA_TIMEOUT;
    while (inb(ATA_STATUS) & STATUS_BSY) {
        if (--timeout == 0) {
            syslog_write("ATA: Timeout waiting for BSY to clear");
            return false;
        }
    }
    return true;
}

static bool ata_wait_drq(void) {
    int timeout = ATA_TIMEOUT;
    while (!(inb(ATA_STATUS) & STATUS_DRQ)) {
        if (--timeout == 0) {
            syslog_write("ATA: Timeout waiting for DRQ to set");
            return false;
        }
    }
    return true;
}

static void ata_select_drive(void) {
    outb(ATA_DRIVE_HEAD, 0xE0); 
}

bool ata_init(void) {
    uint8_t status = inb(ATA_STATUS);
    if (status == 0xFF) {
        // Floating bus, no drive
        return false;
    }

    ata_select_drive();
    io_wait();
    
    outb(ATA_SECTOR_CNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, CMD_IDENTIFY);
    
    status = inb(ATA_STATUS);
    if (status == 0) return false;

    if (!ata_wait_bsy()) return false;
    
    // Read Identify data
    uint16_t tmp[256];
    // Check DRQ before reading
    if (inb(ATA_STATUS) & STATUS_DRQ) {
         insw(ATA_DATA, tmp, 256);
    }
    
    return true;
}

bool ata_read(uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (!ata_wait_bsy()) return false;
    
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, count);
    outb(ATA_LBA_LOW, (uint8_t)(lba));
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, CMD_READ_PIO);

    for (int i = 0; i < count; i++) {
        if (!ata_wait_bsy()) return false;
        if (!ata_wait_drq()) return false;
        insw(ATA_DATA, buffer + (i * 512), 256);
    }
    return true;
}

bool ata_write(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    if (!ata_wait_bsy()) return false;

    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, count);
    outb(ATA_LBA_LOW, (uint8_t)(lba));
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, CMD_WRITE_PIO);

    for (int i = 0; i < count; i++) {
        if (!ata_wait_bsy()) return false;
        if (!ata_wait_drq()) return false;
        
        outsw(ATA_DATA, buffer + (i * 512), 256);
        
        outb(ATA_COMMAND, 0xE7); // Flush
        if (!ata_wait_bsy()) return false;
    }
    return true;
}
