/**
 * core_project.h
 *
 * Project firmware detection and launch logic.
 *
 * Responsible for:
 *   - Checking whether a project firmware partition exists
 *   - (Future) Jumping to the project firmware
 *
 * The actual partition jump is not implemented yet.
 */

#ifndef CORE_PROJECT_H
#define CORE_PROJECT_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * Check if a project firmware partition exists on the flash.
 *
 * Looks for the partition label stored in NVS under core.proj_part,
 * or falls back to looking for a partition labelled "project".
 *
 * @return true if a suitable partition was found, false otherwise.
 */
bool core_project_partition_exists(void);

/**
 * Attempt to launch the project firmware.
 *
 * Currently a skeleton — logs what it would do but does not
 * perform the actual jump.
 *
 * @return ESP_OK if the project would be launchable,
 *         ESP_ERR_NOT_FOUND if no project partition exists.
 */
esp_err_t core_project_launch(void);

#endif // CORE_PROJECT_H
