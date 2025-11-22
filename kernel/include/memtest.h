#ifndef MEMTEST_H
#define MEMTEST_H

#include <stddef.h>
#include <stdint.h>

/*
 * Attempts to detect the amount of physical RAM available by probing
 * memory addresses starting from 8MB up to the identity-mapped limit (1GB).
 * Returns the detected size in bytes.
 */
size_t memtest_detect_upper_limit(void);

/*
 * Runs a read/write pattern test on a specific range of memory.
 * Returns true if the test passes, false if verification failed.
 */
bool memtest_region(uintptr_t start, size_t size);

/*
 * Shell command handler to run a verbose memory diagnostic.
 */
void memtest_run_diagnostic(void);

#endif /* MEMTEST_H */
