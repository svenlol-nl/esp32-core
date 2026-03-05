/**
 * core_boot.h
 *
 * Boot banner and startup logging.
 * Prints a readable boot banner and logs key initialization steps.
 */

#ifndef CORE_BOOT_H
#define CORE_BOOT_H

/**
 * Print the boot banner to serial output.
 * Shows firmware name, version, and ESP-IDF version.
 */
void core_boot_print_banner(void);

/**
 * Print device information summary after initialization.
 * Shows chip model, revision, flash size, and free heap.
 */
void core_boot_print_device_info(void);

#endif // CORE_BOOT_H
