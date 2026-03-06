/**
 * core_boot.c
 *
 * Provides boot banner, device information logging, and boot state
 * decision logic.  The boot state determines whether the device enters
 * local configuration mode or attempts to launch a project firmware.
 */

#include "core_boot.h"
#include "core_storage.h"
#include "core_config.h"

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

    const core_config_t *cfg = core_config_get();

    /* 1. local_configure flag explicitly set → configure mode */
    if (cfg->system.local_configure_enabled) {
        ESP_LOGI(TAG, "Local configure flag is set");
        return CORE_LOCAL_CONFIGURE;
    }

    /* 2. No WiFi SSID configured → needs configuration */
    if (!core_config_has_wifi()) {
        ESP_LOGI(TAG, "No WiFi configured — entering local configure");
        return CORE_LOCAL_CONFIGURE;
    }

    /* 3. Configuration looks valid → launch project */
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
