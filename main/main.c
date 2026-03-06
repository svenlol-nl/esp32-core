/**
 * app_main.c
 *
 * Entry point for the ESP32 Core Firmware.
 *
 * Boot sequence:
 *   1. Print boot banner
 *   2. Initialize NVS
 *   3. Load or generate device ID
 *   4. Load configuration
 *   5. Print device information
 *   6. Resolve boot state (local configure vs. project launch)
 *   7. Execute the chosen boot path
 */

#include "core_boot.h"
#include "core_storage.h"
#include "core_device.h"
#include "core_project.h"
#include "core_config.h"

#include "esp_log.h"

static const char *TAG = "CORE";

void app_main(void)
{
    /* 1. Boot banner */
    core_boot_print_banner();

    /* 2. Initialize NVS storage */
    esp_err_t err = core_storage_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Storage init failed — halting");
        return;
    }

    /* 3. Load or generate device identity */
    err = core_device_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Device init failed — halting");
        return;
    }

    /* 4. Load configuration */
    err = core_config_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Config init failed — halting");
        return;
    }

    /* 5. Device info summary */
    core_boot_print_device_info();

    /* 6. Determine boot state */
    core_boot_state_t state = core_boot_resolve_state();
    ESP_LOGI(TAG, "Boot mode: %s", core_boot_state_name(state));

    /* 7. Execute the chosen boot path */
    switch (state)
    {
    case CORE_LOCAL_CONFIGURE:
        core_config_enter_local_configure();
        /* does not return */
        break;

    case CORE_START_PROJECT:
        ESP_LOGI(TAG, "Launching project firmware...");
        err = core_project_launch();
        if (err != ESP_OK)
        {
            /* core_project_launch() only returns on failure.
             * On success it restarts into the project image. */
            ESP_LOGW(TAG, "Falling back to LOCAL_CONFIGURE mode");
            core_config_enter_local_configure();
            /* does not return */
        }
        break;

    default:
        ESP_LOGE(TAG, "Unknown boot state — halting");
        break;
    }
}