/**
 * core_config.c
 *
 * Device configuration management: load, validate, update, save.
 *
 * Configuration is stored in NVS under the "core" namespace using
 * one key per field.  The in-memory representation lives in a static
 * core_config_t so it can be accessed cheaply throughout the firmware.
 *
 * Validation rules:
 *   - wifi.ssid      : must not be empty (if wifi section is used)
 *   - firmware.channel: must be one of "stable", "beta", "dev"
 */

#include "core_config.h"
#include "core_storage.h"
#include "core_ble.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CONFIG";

/* ------------------------------------------------------------------ */
/*  NVS keys for each config field (all in NVS_NAMESPACE_CORE)         */
/* ------------------------------------------------------------------ */

#define NVS_KEY_WIFI_SSID        "wifi_ssid"
#define NVS_KEY_WIFI_PASS        "wifi_pass"
#define NVS_KEY_FW_PROJECT       "fw_proj"
#define NVS_KEY_FW_CHANNEL       "fw_chan"
/* local_configure_enabled reuses NVS_KEY_LOCAL_CONFIGURE ("local_cfg") */

/* ------------------------------------------------------------------ */
/*  Static configuration instance                                      */
/* ------------------------------------------------------------------ */

static core_config_t s_config;
static bool s_loaded = false;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/**
 * Set the configuration to safe defaults (first-boot state).
 */
static void set_defaults(core_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->system.local_configure_enabled = 1;  /* first boot → configure */
    strncpy(cfg->firmware.channel, CONFIG_CHANNEL_STABLE,
            sizeof(cfg->firmware.channel) - 1);
}

/**
 * Try to read a string from NVS; on any error leave `out` unchanged.
 */
static void load_str(const char *key, char *out, size_t max)
{
    core_storage_read_str(NVS_NAMESPACE_CORE, key, out, max);
}

/**
 * Write a string to NVS only if it differs from what was stored.
 */
static esp_err_t save_str(const char *key, const char *value)
{
    return core_storage_write_str(NVS_NAMESPACE_CORE, key, value);
}

/* ------------------------------------------------------------------ */
/*  Initialization                                                     */
/* ------------------------------------------------------------------ */

esp_err_t core_config_init(void)
{
    ESP_LOGI(TAG, "Loading current configuration");

    set_defaults(&s_config);

    /* Read each field from NVS — missing keys leave the default. */
    load_str(NVS_KEY_WIFI_SSID,  s_config.wifi.ssid,
             sizeof(s_config.wifi.ssid));
    load_str(NVS_KEY_WIFI_PASS,  s_config.wifi.password,
             sizeof(s_config.wifi.password));
    load_str(NVS_KEY_FW_PROJECT, s_config.firmware.project,
             sizeof(s_config.firmware.project));
    load_str(NVS_KEY_FW_CHANNEL, s_config.firmware.channel,
             sizeof(s_config.firmware.channel));

    /* local_configure_enabled is stored as u8 */
    core_storage_read_u8(NVS_NAMESPACE_CORE, NVS_KEY_LOCAL_CONFIGURE,
                         &s_config.system.local_configure_enabled);

    s_loaded = true;

    ESP_LOGI(TAG, "Configuration loaded");
    ESP_LOGI(TAG, "  wifi.ssid     : %s",
             s_config.wifi.ssid[0] ? s_config.wifi.ssid : "(not set)");
    ESP_LOGI(TAG, "  firmware.project : %s",
             s_config.firmware.project[0] ? s_config.firmware.project : "(not set)");
    ESP_LOGI(TAG, "  firmware.channel : %s", s_config.firmware.channel);
    ESP_LOGI(TAG, "  system.local_cfg : %u",
             s_config.system.local_configure_enabled);

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Read access                                                        */
/* ------------------------------------------------------------------ */

const core_config_t *core_config_get(void)
{
    return &s_config;
}

bool core_config_has_wifi(void)
{
    return s_config.wifi.ssid[0] != '\0';
}

bool core_config_has_project(void)
{
    return s_config.firmware.project[0] != '\0';
}

/* ------------------------------------------------------------------ */
/*  Validation                                                         */
/* ------------------------------------------------------------------ */

/**
 * Check whether `channel` is one of the known values.
 */
static bool is_valid_channel(const char *channel)
{
    return strcmp(channel, CONFIG_CHANNEL_STABLE) == 0
        || strcmp(channel, CONFIG_CHANNEL_BETA)   == 0
        || strcmp(channel, CONFIG_CHANNEL_DEV)    == 0;
}

bool core_config_validate(const core_config_t *cfg)
{
    bool ok = true;

    /* wifi.ssid — must not be empty */
    if (cfg->wifi.ssid[0] == '\0') {
        ESP_LOGW(TAG, "Invalid configuration: wifi.ssid empty");
        ok = false;
    }

    /* firmware.channel — must be a known value */
    if (cfg->firmware.channel[0] != '\0'
        && !is_valid_channel(cfg->firmware.channel)) {
        ESP_LOGW(TAG, "Invalid configuration: firmware.channel '%s' unknown"
                      " (expected stable|beta|dev)",
                 cfg->firmware.channel);
        ok = false;
    }

    return ok;
}

/* ------------------------------------------------------------------ */
/*  Write access                                                       */
/* ------------------------------------------------------------------ */

esp_err_t core_config_update(const core_config_t *cfg)
{
    if (!core_config_validate(cfg)) {
        ESP_LOGE(TAG, "Configuration update rejected — validation failed");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, cfg, sizeof(s_config));
    ESP_LOGI(TAG, "Configuration updated (in-memory)");
    return ESP_OK;
}

esp_err_t core_config_save(void)
{
    ESP_LOGI(TAG, "Persisting configuration to NVS");

    esp_err_t err;

    err = save_str(NVS_KEY_WIFI_SSID, s_config.wifi.ssid);
    if (err != ESP_OK) return err;

    err = save_str(NVS_KEY_WIFI_PASS, s_config.wifi.password);
    if (err != ESP_OK) return err;

    err = save_str(NVS_KEY_FW_PROJECT, s_config.firmware.project);
    if (err != ESP_OK) return err;

    err = save_str(NVS_KEY_FW_CHANNEL, s_config.firmware.channel);
    if (err != ESP_OK) return err;

    err = core_storage_write_u8(NVS_NAMESPACE_CORE, NVS_KEY_LOCAL_CONFIGURE,
                                s_config.system.local_configure_enabled);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Configuration saved");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Local-configure mode                                               */
/* ------------------------------------------------------------------ */

void core_config_enter_local_configure(void)
{
    ESP_LOGI(TAG, "Entering LOCAL_CONFIGURE mode");

    /* Ensure the config is loaded */
    if (!s_loaded) {
        core_config_init();
    }

    /* Clear the flag so the device does not re-enter configure mode
       on the next reboot unless explicitly requested again. */
    if (s_config.system.local_configure_enabled) {
        s_config.system.local_configure_enabled = 0;
        core_config_save();
    }

    ESP_LOGI(TAG, "Current configuration:");
    ESP_LOGI(TAG, "  wifi.ssid        : %s",
             s_config.wifi.ssid[0] ? s_config.wifi.ssid : "(not set)");
    ESP_LOGI(TAG, "  firmware.project : %s",
             s_config.firmware.project[0] ? s_config.firmware.project : "(not set)");
    ESP_LOGI(TAG, "  firmware.channel : %s", s_config.firmware.channel);
    ESP_LOGI(TAG, "  system.local_cfg : %u",
             s_config.system.local_configure_enabled);

    ESP_LOGI(TAG, "Waiting for configuration updates");

    /* Start the BLE GATT server for mobile configuration */
    esp_err_t err = core_ble_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE start failed (0x%x) — idling", err);
    }

    /* Keep the task alive while BLE handles configuration */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ------------------------------------------------------------------ */
/*  Factory reset                                                      */
/* ------------------------------------------------------------------ */

esp_err_t core_config_factory_reset(void)
{
    ESP_LOGI("CORE", "Clearing configuration storage");

    /* Erase all keys in the core namespace (WiFi, firmware, system) */
    esp_err_t err = core_storage_erase_namespace(NVS_NAMESPACE_CORE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase core namespace: 0x%x", err);
        return err;
    }

    /* Device identity (NVS_NAMESPACE_DEVICE) is preserved */

    /* Reset in-memory configuration to safe defaults */
    memset(&s_config, 0, sizeof(s_config));
    s_config.system.local_configure_enabled = 1;
    strncpy(s_config.firmware.channel, CONFIG_CHANNEL_STABLE,
            sizeof(s_config.firmware.channel) - 1);

    ESP_LOGI("CORE", "Configuration cleared");
    return ESP_OK;
}
