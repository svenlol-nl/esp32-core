/**
 * core_storage.c
 *
 * NVS initialization with error recovery, plus typed read/write helpers.
 * Every helper opens and closes the NVS handle internally so callers
 * don't need to manage handles.
 */

#include "core_storage.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "STORAGE";

/* ------------------------------------------------------------------ */
/*  Initialization                                                     */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/*  Read helpers                                                       */
/* ------------------------------------------------------------------ */

esp_err_t core_storage_read_u8(const char *ns, const char *key, uint8_t *out)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u8(nvs, key, out);
    nvs_close(nvs);
    return err;
}

esp_err_t core_storage_read_str(const char *ns, const char *key,
                                char *out, size_t max_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = max_len;
    err = nvs_get_str(nvs, key, out, &len);
    nvs_close(nvs);
    return err;
}

/* ------------------------------------------------------------------ */
/*  Write helpers                                                      */
/* ------------------------------------------------------------------ */

esp_err_t core_storage_write_u8(const char *ns, const char *key, uint8_t value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open '%s' failed: 0x%x", ns, err);
        return err;
    }

    err = nvs_set_u8(nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t core_storage_write_str(const char *ns, const char *key,
                                 const char *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open '%s' failed: 0x%x", ns, err);
        return err;
    }

    err = nvs_set_str(nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}
