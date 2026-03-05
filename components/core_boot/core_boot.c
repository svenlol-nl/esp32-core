/**
 * core_boot.c
 *
 * Provides boot banner and device information logging.
 */

#include "core_boot.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"

static const char *TAG = "BOOT";

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
