#include "memtest.h"

#include <stdbool.h>
#include <stdint.h>

#include "syslog.h"
#include "terminal.h"

/*
 * We skip the first 8MB to avoid clobbering:
 * - Real Mode IVT/BDA (0x0 - 0x500)
 * - Bootloader stage 1/2 (0x7C00 - 0x7E00+)
 * - Kernel Load Address (0x100000+)
 * - Kernel .bss and Stack (up to ~4MB in current setup)
 * - BootInfo structure (0x5000)
 */
#define PROBE_START_ADDR (8 * 1024 * 1024) 

/*
 * The identity paging set up in stage2.asm maps the first 1GB (512 * 2MB).
 * Accessing beyond this will cause a Page Fault.
 */
#define PROBE_HARD_LIMIT (1024 * 1024 * 1024)

#define PROBE_STEP (1024 * 1024) // Check every 1MB

static bool probe_address(volatile uint64_t* addr) {
    uint64_t original = *addr;
    bool working = true;

    /* Pattern 1: Alternating bits 0x55... */
    uint64_t pattern1 = 0x5555555555555555;
    *addr = pattern1;
    if (*addr != pattern1) {
        working = false;
    }

    /* Pattern 2: Alternating bits 0xAA... */
    uint64_t pattern2 = 0xAAAAAAAAAAAAAAAA;
    *addr = pattern2;
    if (*addr != pattern2) {
        working = false;
    }

    /* Restore original value (though strictly we are probing free RAM) */
    *addr = original;
    return working;
}

size_t memtest_detect_upper_limit(void) {
    size_t current = PROBE_START_ADDR;

    /*
     * We assume the first 8MB exists because the kernel is running.
     * We probe upwards until a read/write mismatch occurs or we hit the mapping limit.
     */
    while (current < PROBE_HARD_LIMIT) {
        volatile uint64_t* ptr = (volatile uint64_t*)current;
        if (!probe_address(ptr)) {
            break;
        }
        current += PROBE_STEP;
    }

    return current;
}

bool memtest_region(uintptr_t start, size_t size) {
    volatile uint8_t* ptr = (volatile uint8_t*)start;
    bool ok = true;

    for (size_t i = 0; i < size; i++) {
        uint8_t original = ptr[i];
        
        /* Simple invert test */
        ptr[i] = 0xAA;
        if (ptr[i] != 0xAA) {
            ok = false;
            ptr[i] = original; /* Try to restore */
            break;
        }

        ptr[i] = 0x55;
        if (ptr[i] != 0x55) {
            ok = false;
            ptr[i] = original;
            break;
        }

        ptr[i] = original;
    }

    return ok;
}

void memtest_run_diagnostic(void) {
    terminal_writestring("Starting RAM probe (safe range 8MB+)...\n");

    size_t upper_limit = memtest_detect_upper_limit();
    
    terminal_writestring("Detected RAM Limit: ");
    terminal_write_uint((unsigned int)(upper_limit / 1024 / 1024));
    terminal_writestring(" MB\n");

    terminal_writestring("Performing integrity check on detected RAM...\n");
    
    /* Test in 1MB chunks */
    size_t tested = 0;
    size_t failures = 0;
    size_t start = PROBE_START_ADDR;

    while (start < upper_limit) {
        /* Visual progress indicator every 16MB */
        if ((start % (16 * 1024 * 1024)) == 0) {
            terminal_writestring(".");
        }

        if (!memtest_region(start, PROBE_STEP)) {
            failures++;
            terminal_writestring("\nFail @ ");
            terminal_write_uint((unsigned int)(start / 1024 / 1024));
            terminal_writestring(" MB");
        }
        tested += PROBE_STEP;
        start += PROBE_STEP;
    }
    
    terminal_newline();
    if (failures == 0) {
        terminal_writestring("Memory integrity verified: OK.\n");
        syslog_write("MemTest: passed successfully");
    } else {
        terminal_writestring("Memory errors detected!\n");
        syslog_write("MemTest: errors detected");
    }
}
