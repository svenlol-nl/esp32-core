/**
 * core_ota.h
 *
 * OTA trigger logic and update decision system.
 *
 * Handles:
 *   - OTA request flag (set by project firmware or BLE command)
 *   - Boot counter for scheduled fallback update checks
 *   - Firmware manifest retrieval and version comparison
 *   - OTA update execution via esp_https_ota
 *
 * The OTA system runs during core boot, before boot state resolution.
 * If an update is applied the device restarts automatically.
 */

#ifndef CORE_OTA_H
#define CORE_OTA_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Set the OTA request flag in NVS.
 *
 * Call from project firmware or BLE to request an OTA update
 * at next boot.
 *
 * @return ESP_OK on success, or an NVS error.
 */
esp_err_t core_ota_set_request_flag(void);

/**
 * Increment the persistent boot counter.
 *
 * @return The new boot count value.
 */
uint32_t core_ota_increment_boot_count(void);

/**
 * Run OTA decision logic during boot.
 *
 * Checks (in order):
 *   1. OTA request flag → start update
 *   2. Boot count % N == 0 → scheduled fallback check
 *
 * On successful OTA the device restarts and this function
 * does not return.
 *
 * @return false if no OTA was performed (continue normal boot).
 */
bool core_ota_run(void);

/**
 * Check for firmware updates and apply if available.
 *
 * Connects to WiFi, fetches the firmware manifest (or uses a
 * direct bin_url), compares versions, and downloads if newer.
 * On successful update the device restarts (does not return).
 *
 * @return ESP_ERR_NOT_FOUND if firmware is up to date,
 *         or another error on failure.
 */
esp_err_t core_ota_check_and_update(void);

#endif /* CORE_OTA_H */
