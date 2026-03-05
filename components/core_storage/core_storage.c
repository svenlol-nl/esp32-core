/**
 * core_storage.c
 *
 * NVS initialization with error recovery.
 * Erases and reinitializes the NVS partition when it is
 * corrupt, truncated, or full.
 */

#include "core_storage.h"

#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "STORAGE";

esp_err_t core_storage_init(void)
{
    ESP_LOGI(TAG, "Initializing NVS...");

    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase (err=0x%x), erasing...", err);
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: 0x%x", err);
            return err;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "NVS ready");
    return ESP_OK;
}
