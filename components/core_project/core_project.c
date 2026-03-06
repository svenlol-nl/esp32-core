/**
 * core_project.c
 *
 * Project firmware partition discovery, image validation, and launch.
 *
 * Locates a project firmware in an OTA-style partition, validates the
 * image header, and transfers control via the ESP-IDF bootloader by
 * setting the boot partition and restarting.
 *
 * Independent of BLE, OTA download, and networking.
 */

#include "core_project.h"
#include "core_storage.h"

#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"

static const char *TAG = "PROJECT";

/** Default partition label when no override is stored in NVS */
#define DEFAULT_PROJECT_LABEL "ota_0"

/** Max length for a partition label string (ESP-IDF limit is 16 + null) */
#define PARTITION_LABEL_MAX 17

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/**
 * Resolve the configured partition label from NVS, falling back to
 * the default label if none is stored.
 */
static void resolve_partition_label(char *out, size_t len)
{
    esp_err_t err = core_storage_read_str(NVS_NAMESPACE_CORE,
                                          NVS_KEY_PROJECT_PARTITION,
                                          out, len);
    if (err != ESP_OK) {
        strncpy(out, DEFAULT_PROJECT_LABEL, len - 1);
        out[len - 1] = '\0';
    }
}

/**
 * Find the project firmware partition by label.
 *
 * @return Pointer to the partition, or NULL if not found.
 */
static const esp_partition_t *find_project_partition(void)
{
    char label[PARTITION_LABEL_MAX] = {0};
    resolve_partition_label(label, sizeof(label));

    ESP_LOGI(TAG, "Searching for project firmware");

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);

    if (part != NULL) {
        ESP_LOGI(TAG, "Found partition: %s", part->label);
        ESP_LOGI(TAG, "  Address : 0x%lx", (unsigned long)part->address);
        ESP_LOGI(TAG, "  Size    : %lu bytes", (unsigned long)part->size);
    } else {
        ESP_LOGW(TAG, "No project partition found (looked for '%s')", label);
    }

    return part;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

bool core_project_partition_exists(void)
{
    return find_project_partition() != NULL;
}

bool core_project_validate_image(void)
{
    const esp_partition_t *part = find_project_partition();
    if (part == NULL) {
        return false;
    }

    esp_app_desc_t app_desc;
    esp_err_t err = esp_ota_get_partition_description(part, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Image validation failed (err=0x%x)", err);
        return false;
    }

    ESP_LOGI(TAG, "Image validated");
    ESP_LOGI(TAG, "  Project : %s", app_desc.project_name);
    ESP_LOGI(TAG, "  Version : %s", app_desc.version);
    ESP_LOGI(TAG, "  IDF     : %s", app_desc.idf_ver);

    return true;
}

esp_err_t core_project_launch(void)
{
    ESP_LOGI(TAG, "Preparing to launch project firmware");

    /* 1. Locate the project partition */
    const esp_partition_t *part = find_project_partition();
    if (part == NULL) {
        ESP_LOGE(TAG, "No valid firmware found");
        return ESP_ERR_NOT_FOUND;
    }

    /* 2. Validate the image header */
    esp_app_desc_t app_desc;
    esp_err_t err = esp_ota_get_partition_description(part, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No valid firmware found");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Image validated");
    ESP_LOGI(TAG, "Launching firmware from partition %s", part->label);

    /* 3. Set the boot partition so the bootloader jumps to it */
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: 0x%x", err);
        return err;
    }

    /* 4. Transfer control — the bootloader will load the project image */
    ESP_LOGI(TAG, "Jumping to project entrypoint");
    esp_restart();

    /* Never reached */
    return ESP_OK;
}
