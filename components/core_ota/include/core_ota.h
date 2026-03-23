/**
 * core_ota.h
 *
 * OTA trigger logic and update decision system.
 *
 * Handles:
 *   - OTA request flag (set by project firmware or BLE command)
 *   - Boot counter for scheduled fallback update checks
 *   - Firmware manifest retrieval and hash comparison
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
 * Get the currently installed project firmware version.
 *
 * Reads the app descriptor from the project partition.
 * Falls back to "0.0.0" if no valid image is present.
 *
 * @param out     Buffer to receive the version string.
 * @param max     Size of the buffer.
 */
void core_ota_get_project_version(char *out, size_t max);

/**
 * Confirm image validity after a successful OTA update.
 *
 * Should be called by the project firmware after successful startup
 * to prevent automatic rollback on the next reboot.  When running
 * from the core (factory) partition this is a no-op.
 */
void core_ota_confirm_image(void);

/**
 * Check if the current OTA image is pending verification and
 * handle rollback if necessary.  Called during core boot.
 */
void core_ota_check_rollback(void);

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
 * direct bin_url), compares firmware hashes, and downloads when
 * the hash differs.
 * On successful update the device restarts (does not return).
 *
 * @return ESP_ERR_NOT_FOUND if firmware is up to date,
 *         or another error on failure.
 */
esp_err_t core_ota_check_and_update(void);

#endif /* CORE_OTA_H */
