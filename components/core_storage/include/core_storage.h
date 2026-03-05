/**
 * core_storage.h
 *
 * NVS (Non-Volatile Storage) initialization and access helpers.
 * Handles init, full-partition recovery, and namespace management.
 */

#ifndef CORE_STORAGE_H
#define CORE_STORAGE_H

#include "esp_err.h"

/** NVS namespace for core system configuration */
#define NVS_NAMESPACE_CORE   "core"

/** NVS namespace for device-specific values */
#define NVS_NAMESPACE_DEVICE "device"

/**
 * Initialize the NVS flash subsystem.
 *
 * Handles the standard recovery cases:
 *  - NVS partition truncated or corrupt → erase and reinit
 *  - NVS partition full → erase and reinit
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t core_storage_init(void);

#endif // CORE_STORAGE_H
