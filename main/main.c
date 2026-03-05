/**
 * app_main.c
 *
 */

#include "core_boot.h"
#include "core_storage.h"
#include "core_device.h"

#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    core_boot_print_banner();

    /* 2. Initialize NVS storage */
    esp_err_t err = core_storage_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Storage init failed — halting");
        return;
    }

    err = core_device_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Device init failed — halting");
        return;
    }

    core_boot_print_device_info();

    ESP_LOGI(TAG, "Core firmware ready");
}