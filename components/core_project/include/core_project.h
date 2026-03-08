/**
 * core_project.h
 *
 * Project firmware partition discovery, image validation, and launch.
 *
 * Responsible for:
 *   - Locating a project firmware partition (default: "ota_0")
 *   - Validating the project image header
 *   - Transferring control to the project firmware
 *
 * Independent of BLE, OTA download, and networking.
 */

#ifndef CORE_PROJECT_H
#define CORE_PROJECT_H

#include "esp_err.h"
#include <stdbool.h>

/** Max consecutive project crashes before entering recovery mode */
#define MAX_PROJECT_CRASHES 3

/**
 * Check if a project firmware partition exists on the flash.
 *
 * Looks for the partition label stored in NVS under core.proj_part,
 * or falls back to "ota_0".
 *
 * @return true if a suitable partition was found, false otherwise.
 */
bool core_project_partition_exists(void);

/**
 * Validate the project firmware image.
 *
 * Reads the app descriptor from the project partition and verifies
 * the image header is well-formed.  Logs project name, version, and
 * IDF version on success.
 *
 * @return true if the image is valid, false otherwise.
 */
bool core_project_validate_image(void);

/**
 * Locate, validate, and launch the project firmware.
 *
 * On success this function does not return — it sets the boot
 * partition and restarts into the project image.  On failure it
 * returns an error so the caller can fall back.
 *
 * @return ESP_ERR_NOT_FOUND      no project partition found
 * @return ESP_ERR_INVALID_STATE  image validation failed
 * @return (other)                boot partition write error
 */
esp_err_t core_project_launch(void);

/**
 * Check the reset reason and update the project crash counter.
 *
 * If the last reset was caused by a panic, task WDT, or interrupt WDT,
 * the crash counter is incremented.  Otherwise it is reset to 0.
 *
 * @return true if the crash counter has reached or exceeded
 *         MAX_PROJECT_CRASHES (caller should enter recovery mode),
 *         false if it is safe to launch the project firmware.
 */
bool core_project_check_crash_loop(void);

#endif // CORE_PROJECT_H
