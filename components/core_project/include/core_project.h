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

#endif // CORE_PROJECT_H
