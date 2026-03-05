/**
 * core_device.h
 *
 * Device identity management.
 * Generates a deterministic UUIDv4-style device ID from the ESP32
 * MAC address, stores it in NVS on first boot, and loads it on
 * subsequent boots.
 */

#ifndef CORE_DEVICE_H
#define CORE_DEVICE_H

#include "esp_err.h"

/** Length of the device ID string including null terminator (UUID format) */
#define DEVICE_ID_STR_LEN 37  // "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx\0"

/**
 * Initialize the device identity.
 *
 * On first boot: generates a UUID from the MAC address and stores
 * it in NVS under the "device" namespace.
 *
 * On subsequent boots: loads the previously stored UUID from NVS.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t core_device_init(void);

/**
 * Get the device ID string.
 *
 * Must be called after core_device_init().
 *
 * @return Pointer to the null-terminated device ID string,
 *         or "unknown" if not yet initialized.
 */
const char *core_device_get_id(void);

#endif // CORE_DEVICE_H
