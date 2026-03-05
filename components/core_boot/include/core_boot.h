/**
 * core_boot.h
 *
 * Boot banner, device info, and boot state decision logic.
 * Determines what the core firmware should do after initialization:
 *   - enter local configure mode (BLE provisioning, future)
 *   - launch a project firmware
 */

#ifndef CORE_BOOT_H
#define CORE_BOOT_H

/**
 * Boot states the core firmware can transition into.
 */
typedef enum {
    CORE_BOOT,              /**< Initial boot phase (banner, NVS, device ID) */
    CORE_LOCAL_CONFIGURE,   /**< Enter local configuration mode (BLE, future) */
    CORE_START_PROJECT,     /**< Launch project firmware from a secondary partition */
} core_boot_state_t;

/**
 * Print the boot banner to serial output.
 * Shows firmware name, version, and ESP-IDF version.
 */
void core_boot_print_banner(void);

/**
 * Print device information summary after initialization.
 * Shows chip model, revision, core count, and free heap.
 */
void core_boot_print_device_info(void);

/**
 * Determine which boot state the device should enter.
 *
 * Decision logic (evaluated in order):
 *   1. No configuration in NVS at all        → CORE_LOCAL_CONFIGURE
 *   2. "core.local_cfg" flag set to 1 in NVS → CORE_LOCAL_CONFIGURE
 *   3. Otherwise                              → CORE_START_PROJECT
 *
 * @return The resolved boot state.
 */
core_boot_state_t core_boot_resolve_state(void);

/**
 * Return a human-readable name for a boot state.
 */
const char *core_boot_state_name(core_boot_state_t state);

#endif // CORE_BOOT_H
