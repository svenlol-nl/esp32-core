/**
 * core_ble.c
 *
 * BLE GATT server for device configuration and command control.
 *
 * Provides a custom BLE service ("Device Management") with four
 * characteristics:
 *
 *   config_read   – READ   – returns full config as JSON
 *   config_write  – WRITE  – accepts JSON config payload
 *   command       – WRITE  – accepts JSON command messages
 *   status        – NOTIFY – pushes status updates to the app
 *
 * Uses the NimBLE stack on ESP-IDF.  The module communicates with
 * core_config for configuration access and core_device for the
 * device identity.  It does not access NVS or system internals
 * directly.
 */

#include "core_ble.h"
#include "core_config.h"
#include "core_device.h"
#include "core_storage.h"
#include "core_boot.h"
#include "core_ota.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_att.h"

static const char *TAG = "BLE";

/* ------------------------------------------------------------------ */
/*  BLE connection state                                               */
/* ------------------------------------------------------------------ */

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_notify_enabled = false;

/* ------------------------------------------------------------------ */
/*  Characteristic value handles (filled by NimBLE during registration)*/
/* ------------------------------------------------------------------ */

static uint16_t s_config_read_handle;
static uint16_t s_config_write_handle;
static uint16_t s_command_handle;
static uint16_t s_status_handle;

/* ------------------------------------------------------------------ */
/*  Advertisement device name                                          */
/* ------------------------------------------------------------------ */

/** Short device name for advertising (e.g. "ESP-AB12CD") */
#define BLE_DEVICE_NAME_MAX 16
static char s_device_name[BLE_DEVICE_NAME_MAX];

/* ------------------------------------------------------------------ */
/*  Custom 128-bit UUIDs                                               */
/* ------------------------------------------------------------------ */

/*
 * Device Management Service:
 *   12340001-0000-1000-8000-00805f9b34fb
 *
 * Characteristics:
 *   config_read  : 12340002-...
 *   config_write : 12340003-...
 *   command      : 12340004-...
 *   status       : 12340005-...
 */

static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x01, 0x00, 0x34, 0x12);

static const ble_uuid128_t chr_config_read_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x02, 0x00, 0x34, 0x12);

static const ble_uuid128_t chr_config_write_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x34, 0x12);

static const ble_uuid128_t chr_command_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x04, 0x00, 0x34, 0x12);

static const ble_uuid128_t chr_status_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x05, 0x00, 0x34, 0x12);

static const ble_uuid128_t adv_service_uuids[] = {svc_uuid};

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

static int gap_event_cb(struct ble_gap_event *event, void *arg);
static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static void start_advertising(void);
static void on_sync(void);
static void on_reset(int reason);

/* ------------------------------------------------------------------ */
/*  JSON helpers                                                       */
/* ------------------------------------------------------------------ */

/**
 * Serialize the current configuration to a JSON string.
 * Caller must free() the returned string.
 */
static char *config_to_json(void)
{
    const core_config_t *cfg = core_config_get();

    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;

    /* wifi */
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    if (wifi)
    {
        cJSON_AddStringToObject(wifi, "ssid", cfg->wifi.ssid);
        cJSON_AddStringToObject(wifi, "password", cfg->wifi.password);
    }

    /* firmware */
    cJSON *fw = cJSON_AddObjectToObject(root, "firmware");
    if (fw)
    {
        cJSON_AddStringToObject(fw, "project", cfg->firmware.project);
        cJSON_AddStringToObject(fw, "channel", cfg->firmware.channel);
    }

    /* system */
    cJSON *sys = cJSON_AddObjectToObject(root, "system");
    if (sys)
    {
        cJSON_AddNumberToObject(sys, "local_configure_enabled",
                                cfg->system.local_configure_enabled);
        cJSON_AddNumberToObject(sys, "ota_check_interval_boots",
                                cfg->system.ota_check_interval_boots);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/**
 * Parse a JSON string into a core_config_t.
 * Returns ESP_OK on success, ESP_ERR_INVALID_ARG on parse failure.
 */
static esp_err_t json_to_config(const char *json, size_t len,
                                core_config_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root)
    {
        ESP_LOGW(TAG, "JSON parse error");
        return ESP_ERR_INVALID_ARG;
    }

    /* Start from current config so unspecified fields are preserved */
    memcpy(out, core_config_get(), sizeof(*out));

    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (wifi)
    {
        cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
        if (cJSON_IsString(ssid) && ssid->valuestring)
        {
            strncpy(out->wifi.ssid, ssid->valuestring,
                    sizeof(out->wifi.ssid) - 1);
            out->wifi.ssid[sizeof(out->wifi.ssid) - 1] = '\0';
        }
        cJSON *pass = cJSON_GetObjectItem(wifi, "password");
        if (cJSON_IsString(pass) && pass->valuestring)
        {
            strncpy(out->wifi.password, pass->valuestring,
                    sizeof(out->wifi.password) - 1);
            out->wifi.password[sizeof(out->wifi.password) - 1] = '\0';
        }
    }

    cJSON *fw = cJSON_GetObjectItem(root, "firmware");
    if (fw)
    {
        cJSON *proj = cJSON_GetObjectItem(fw, "project");
        if (cJSON_IsString(proj) && proj->valuestring)
        {
            strncpy(out->firmware.project, proj->valuestring,
                    sizeof(out->firmware.project) - 1);
            out->firmware.project[sizeof(out->firmware.project) - 1] = '\0';
        }
        cJSON *chan = cJSON_GetObjectItem(fw, "channel");
        if (cJSON_IsString(chan) && chan->valuestring)
        {
            strncpy(out->firmware.channel, chan->valuestring,
                    sizeof(out->firmware.channel) - 1);
            out->firmware.channel[sizeof(out->firmware.channel) - 1] = '\0';
        }
    }

    cJSON *sys = cJSON_GetObjectItem(root, "system");
    if (sys)
    {
        cJSON *lcfg = cJSON_GetObjectItem(sys, "local_configure_enabled");
        if (cJSON_IsNumber(lcfg))
        {
            out->system.local_configure_enabled = (uint8_t)lcfg->valueint;
        }

        cJSON *ota_int = cJSON_GetObjectItem(sys, "ota_check_interval_boots");
        if (cJSON_IsNumber(ota_int) && ota_int->valueint > 0)
        {
            out->system.ota_check_interval_boots = (uint32_t)ota_int->valueint;
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Command handler                                                    */
/* ------------------------------------------------------------------ */

/**
 * Handle a command received on the command characteristic.
 * Commands are JSON: {"command": "reboot"}
 */
static void handle_command(const char *data, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root)
    {
        ESP_LOGW(TAG, "Command JSON parse error");
        core_ble_send_status("{\"error\":\"invalid JSON\"}");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(cmd) || !cmd->valuestring)
    {
        ESP_LOGW(TAG, "Missing 'command' field");
        core_ble_send_status("{\"error\":\"missing command field\"}");
        cJSON_Delete(root);
        return;
    }

    const char *command = cmd->valuestring;
    ESP_LOGI(TAG, "Command received: %s", command);

    if (strcmp(command, "reboot") == 0)
    {
        core_ble_send_status("{\"status\":\"rebooting\"}");
        ESP_LOGI("CORE", "Rebooting device");
        /* Brief delay to let the notification go out */
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    else if (strcmp(command, "factory_reset") == 0)
    {
        ESP_LOGI("CORE", "Factory reset requested");
        core_config_factory_reset();
        core_ble_send_status("{\"status\":\"factory reset complete, rebooting\"}");
        ESP_LOGI("CORE", "Rebooting device");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    else if (strcmp(command, "start_ota") == 0)
    {
        ESP_LOGI("OTA", "OTA update requested via BLE");
        core_storage_write_u8(NVS_NAMESPACE_CORE, NVS_KEY_OTA_REQUEST, 1);
        core_ble_send_status("{\"status\":\"OTA requested, rebooting\"}");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    else if (strcmp(command, "enable_local_configure") == 0)
    {
        ESP_LOGI("CORE", "Enabling local configure mode");
        core_config_t cfg;
        memcpy(&cfg, core_config_get(), sizeof(cfg));
        cfg.system.local_configure_enabled = 1;
        core_config_update(&cfg);
        core_config_save();
        core_ble_send_status("{\"status\":\"local configure enabled\"}");
    }
    else if (strcmp(command, "disable_local_configure") == 0)
    {
        ESP_LOGI("CORE", "Disabling local configure mode");
        core_config_t cfg;
        memcpy(&cfg, core_config_get(), sizeof(cfg));
        cfg.system.local_configure_enabled = 0;
        core_config_update(&cfg);
        core_config_save();
        core_ble_send_status("{\"status\":\"local configure disabled\"}");
    }
    else if (strcmp(command, "get_device_info") == 0)
    {
        ESP_LOGI(TAG, "Device info requested");

        char proj_ver[32];
        core_ota_get_project_version(proj_ver, sizeof(proj_ver));

        int64_t uptime_us = esp_timer_get_time();
        uint32_t uptime_s = (uint32_t)(uptime_us / 1000000);

        cJSON *info = cJSON_CreateObject();
        if (info) {
            cJSON_AddStringToObject(info, "device_id", core_device_get_id());
            cJSON_AddStringToObject(info, "core_version", CORE_FW_VERSION);
            cJSON_AddStringToObject(info, "project_version", proj_ver);
            cJSON_AddNumberToObject(info, "uptime_s", uptime_s);
            cJSON_AddNumberToObject(info, "free_heap", esp_get_free_heap_size());

            char *json = cJSON_PrintUnformatted(info);
            cJSON_Delete(info);
            if (json) {
                core_ble_send_status(json);
                free(json);
            }
        }
    }
    else
    {
        ESP_LOGW(TAG, "Unknown command: %s", command);
        core_ble_send_status("{\"error\":\"unknown command\"}");
    }

    cJSON_Delete(root);
}

/* ------------------------------------------------------------------ */
/*  GATT service definition                                            */
/* ------------------------------------------------------------------ */

static const struct ble_gatt_chr_def s_gatt_chrs[] = {
    {
        .uuid = &chr_config_read_uuid.u,
        .access_cb = gatt_access_cb,
        .flags = BLE_GATT_CHR_F_READ,
        .val_handle = &s_config_read_handle,
    },
    {
        .uuid = &chr_config_write_uuid.u,
        .access_cb = gatt_access_cb,
        .flags = BLE_GATT_CHR_F_WRITE,
        .val_handle = &s_config_write_handle,
    },
    {
        .uuid = &chr_command_uuid.u,
        .access_cb = gatt_access_cb,
        .flags = BLE_GATT_CHR_F_WRITE,
        .val_handle = &s_command_handle,
    },
    {
        .uuid = &chr_status_uuid.u,
        .access_cb = gatt_access_cb,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_status_handle,
    },
    {0}};

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = s_gatt_chrs,
    },
    {0}};

/* ------------------------------------------------------------------ */
/*  GATT access callback                                               */
/* ------------------------------------------------------------------ */

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* ---- config_read: READ ---- */
    if (attr_handle == s_config_read_handle &&
        ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {

        ESP_LOGI("CONFIG", "Configuration read requested");

        char *json = config_to_json();
        if (!json)
        {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        int rc = os_mbuf_append(ctxt->om, json, strlen(json));
        free(json);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    /* ---- config_write: WRITE ---- */
    if (attr_handle == s_config_write_handle &&
        ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {

        ESP_LOGI("CONFIG", "Configuration update received");

        /* Extract data from mbuf chain */
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len == 0 || om_len > 1024)
        {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        char buf[1024];
        uint16_t out_len;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &out_len);
        if (rc != 0)
        {
            return BLE_ATT_ERR_UNLIKELY;
        }
        buf[out_len] = '\0';

        /* Parse, validate, and apply */
        core_config_t new_cfg;
        if (json_to_config(buf, out_len, &new_cfg) != ESP_OK)
        {
            core_ble_send_status("{\"error\":\"invalid JSON\"}");
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        if (!core_config_validate(&new_cfg))
        {
            ESP_LOGW("CONFIG", "Validation failed");
            core_ble_send_status("{\"error\":\"validation failed\"}");
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        ESP_LOGI("CONFIG", "Validation successful");

        esp_err_t err = core_config_update(&new_cfg);
        if (err != ESP_OK)
        {
            core_ble_send_status("{\"error\":\"update failed\"}");
            return BLE_ATT_ERR_UNLIKELY;
        }

        ESP_LOGI("CONFIG", "Saving configuration");
        err = core_config_save();
        if (err != ESP_OK)
        {
            core_ble_send_status("{\"error\":\"save failed\"}");
            return BLE_ATT_ERR_UNLIKELY;
        }

        core_ble_send_status("{\"status\":\"configuration updated\"}");
        return 0;
    }

    /* ---- command: WRITE ---- */
    if (attr_handle == s_command_handle &&
        ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {

        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len == 0 || om_len > 512)
        {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        char buf[512];
        uint16_t out_len;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &out_len);
        if (rc != 0)
        {
            return BLE_ATT_ERR_UNLIKELY;
        }
        buf[out_len] = '\0';

        handle_command(buf, out_len);
        return 0;
    }

    /* ---- status: notify-only, no direct read/write ---- */

    return BLE_ATT_ERR_UNLIKELY;
}

/* ------------------------------------------------------------------ */
/*  Status notifications                                               */
/* ------------------------------------------------------------------ */

void core_ble_send_status(const char *status)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_notify_enabled)
    {
        return;
    }

    ESP_LOGI(TAG, "Sending status notification");

    struct os_mbuf *om = ble_hs_mbuf_from_flat(status, strlen(status));
    if (!om)
    {
        ESP_LOGW(TAG, "Failed to allocate mbuf for notification");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_status_handle, om);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "Notification failed: rc=%d", rc);
    }
}

/* ------------------------------------------------------------------ */
/*  Device name from device ID                                         */
/* ------------------------------------------------------------------ */

/**
 * Build a short advertisement name from the device ID.
 * Takes the last 6 hex chars of the UUID and uppercases them.
 * Result: "ESP-AB12CD"
 */
static void build_device_name(void)
{
    const char *id = core_device_get_id();
    size_t id_len = strlen(id);

    /* Extract last 6 hex characters (skip hyphens) */
    char hex[7] = {0};
    int hi = 0;
    for (int i = (int)id_len - 1; i >= 0 && hi < 6; i--)
    {
        if (id[i] != '-')
        {
            hex[hi++] = id[i];
        }
    }
    hex[hi] = '\0';

    /* Reverse to get proper order */
    for (int i = 0; i < hi / 2; i++)
    {
        char tmp = hex[i];
        hex[i] = hex[hi - 1 - i];
        hex[hi - 1 - i] = tmp;
    }

    /* Uppercase */
    for (int i = 0; i < hi; i++)
    {
        if (hex[i] >= 'a' && hex[i] <= 'f')
        {
            hex[i] -= 32;
        }
    }

    snprintf(s_device_name, sizeof(s_device_name), "SVENLOL-%s", hex);
}

/* ------------------------------------------------------------------ */
/*  Advertising                                                        */
/* ------------------------------------------------------------------ */

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    struct ble_hs_adv_fields scan_fields;
    int rc;

    /* ---------- Advertising packet ---------- */

    memset(&adv_fields, 0, sizeof(adv_fields));

    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    adv_fields.uuids128 = adv_service_uuids;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data: rc=%d", rc);
        return;
    }

    /* ---------- Scan response ---------- */

    memset(&scan_fields, 0, sizeof(scan_fields));

    scan_fields.name = (uint8_t *)s_device_name;
    scan_fields.name_len = strlen(s_device_name);
    scan_fields.name_is_complete = 1;

    scan_fields.tx_pwr_lvl_is_present = 1;
    scan_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    rc = ble_gap_adv_rsp_set_fields(&scan_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting scan response data: rc=%d", rc);
        return;
    }

    /* ---------- Start advertising ---------- */

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error starting advertisement: rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "Advertising device %s", s_device_name);
}

/* ------------------------------------------------------------------ */
/*  GAP event handler                                                  */
/* ------------------------------------------------------------------ */

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ESP_LOGI(TAG, "Client connected (handle=%d)",
                     event->connect.conn_handle);
            s_conn_handle = event->connect.conn_handle;

            /* Request a larger MTU for JSON payloads */
            ble_att_set_preferred_mtu(512);
            ble_gattc_exchange_mtu(s_conn_handle, NULL, NULL);
        }
        else
        {
            ESP_LOGW(TAG, "Connection failed: status=%d",
                     event->connect.status);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Client disconnected (reason=%d)",
                 event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete; restarting");
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_status_handle)
        {
            s_notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "Status notifications %s",
                     s_notify_enabled ? "enabled" : "disabled");
        }
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        return 0;

    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  NimBLE host callbacks                                              */
/* ------------------------------------------------------------------ */

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset: reason=%d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error ensuring address: rc=%d", rc);
        return;
    }

    start_advertising();
}

/* ------------------------------------------------------------------ */
/*  NimBLE host task                                                   */
/* ------------------------------------------------------------------ */

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void ble_store_config_init(void);

esp_err_t core_ble_start(void)
{
    ESP_LOGI(TAG, "Starting BLE server");

    /* Build the short device name from the device ID */
    build_device_name();

    /* Initialize NimBLE */
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: 0x%x", ret);
        return ret;
    }

    /* Configure the NimBLE host */
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;

    /* Initialize GATT services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        return ESP_FAIL;
    }

    /* Set the device name for GAP */
    rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "Failed to set GAP device name: rc=%d", rc);
    }

    /* Initialize NimBLE store */
    ble_store_config_init();

    /* Set preferred MTU to 512 for JSON payloads */
    ble_att_set_preferred_mtu(512);

    /* Start the NimBLE host task */
    nimble_port_freertos_init(nimble_host_task);

    return ESP_OK;
}
