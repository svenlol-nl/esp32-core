/**
 * core_project.c
 *
 * Project firmware detection and launch skeleton.
 *
 * Checks the partition table for a project firmware partition and
 * logs what it would do.  The actual boot into the project firmware
 * (e.g. via esp_ota_set_boot_partition or direct jump) is NOT
 * implemented yet.
 */

#include "core_project.h"
#include "core_storage.h"

#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "PROJECT";

/** Default label to search for when no override is stored in NVS */
#define DEFAULT_PROJECT_LABEL "project"

/** Max length for a partition label string */
#define PARTITION_LABEL_MAX 17

bool core_project_partition_exists(void)
{
    /* Check if a custom partition label is configured in NVS */
    char label[PARTITION_LABEL_MAX] = {0};
    esp_err_t err = core_storage_read_str(NVS_NAMESPACE_CORE, NVS_KEY_PROJECT_PARTITION,
                                          label, sizeof(label));

    if (err != ESP_OK) {
        /* No override — use the default label */
        strncpy(label, DEFAULT_PROJECT_LABEL, sizeof(label) - 1);
    }

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);

    if (part != NULL) {
        ESP_LOGI(TAG, "Found project partition: label='%s' addr=0x%lx size=%lu",
                 part->label, (unsigned long)part->address, (unsigned long)part->size);
        return true;
    }

    ESP_LOGW(TAG, "No project partition found (looked for '%s')", label);
    return false;
}

esp_err_t core_project_launch(void)
{
    ESP_LOGI(TAG, "Attempting to launch project firmware...");

    if (!core_project_partition_exists()) {
        ESP_LOGE(TAG, "Cannot launch — no project partition available");
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * TODO: Implement the actual jump to the project firmware.
     *
     * This will likely involve:
     *   - Validating the project image
     *   - Setting the boot partition via esp_ota_set_boot_partition()
     *   - Restarting the device
     *
     * For now we just log success and return.
     */
    ESP_LOGI(TAG, "Project firmware launch ready (not yet implemented)");
    return ESP_OK;
}
