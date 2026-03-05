/**
 * core_boot.c
 *
 * Provides boot banner, device information logging, and boot state
 * decision logic.  The boot state determines whether the device enters
 * local configuration mode or attempts to launch a project firmware.
 */

#include "core_boot.h"
#include "core_storage.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "nvs.h"

static const char *TAG = "CORE";

void core_boot_print_banner(void)
{
    printf("\n");
    printf("============================================\n");
    printf("  ESP32 Core Firmware | v1.0.0\n");
    printf("  Copyright (c) 2026 Svenlol\n");
    printf("  https://github.com/Svenlol-nl/esp32-core\n");
    printf("  IDF: %s\n", esp_get_idf_version());
    printf("============================================\n");
    printf("\n");

    ESP_LOGI(TAG, "Boot started");
}

void core_boot_print_device_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "---- Device Info ----");
    ESP_LOGI(TAG, "Chip model   : %s",
             chip_info.model == CHIP_ESP32 ? "ESP32" : chip_info.model == CHIP_ESP32S2 ? "ESP32-S2"
                                                   : chip_info.model == CHIP_ESP32S3   ? "ESP32-S3"
                                                   : chip_info.model == CHIP_ESP32C3   ? "ESP32-C3"
                                                   : chip_info.model == CHIP_ESP32H2   ? "ESP32-H2"
                                                                                       : "Unknown");
    ESP_LOGI(TAG, "Chip revision: %d", chip_info.revision);
    ESP_LOGI(TAG, "CPU cores    : %d", chip_info.cores);
    ESP_LOGI(TAG, "Free heap    : %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "---------------------");
}

core_boot_state_t core_boot_resolve_state(void)
{
    ESP_LOGI(TAG, "Checking configuration...");

    /* Check if local_configure flag is explicitly set */
    uint8_t local_cfg = 0;
    esp_err_t err = core_storage_read_u8(NVS_NAMESPACE_CORE, NVS_KEY_LOCAL_CONFIGURE, &local_cfg);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* No configuration exists at all → first boot, enter local configure */
        ESP_LOGI(TAG, "No configuration found (first boot)");
        return CORE_LOCAL_CONFIGURE;
    }

    if (err == ESP_OK && local_cfg == 1) {
        /* Flag is explicitly set → enter local configure */
        ESP_LOGI(TAG, "Local configure flag is set");
        return CORE_LOCAL_CONFIGURE;
    }

    /* Configuration exists and local_configure is not set → launch project */
    return CORE_START_PROJECT;
}

const char *core_boot_state_name(core_boot_state_t state)
{
    switch (state) {
        case CORE_BOOT:            return "BOOT";
        case CORE_LOCAL_CONFIGURE: return "LOCAL_CONFIGURE";
        case CORE_START_PROJECT:   return "START_PROJECT";
        default:                   return "UNKNOWN";
    }
}
