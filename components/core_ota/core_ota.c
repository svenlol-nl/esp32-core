/**
 * core_ota.c
 *
 * OTA trigger logic, update decision system, and firmware installation engine.
 *
 * Handles:
 *   - OTA request flag (set by project firmware or BLE command)
 *   - Boot counter for scheduled fallback update checks
 *   - Firmware manifest retrieval and version comparison
 *   - Firmware download with progress tracking
 *   - Incremental flash writing via esp_ota APIs
 *   - Image validation and boot partition switching
 *   - BLE progress reporting
 */

#include "core_ota.h"
#include "core_storage.h"
#include "core_config.h"
#include "core_ble.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "OTA";

/** Perform a fallback OTA check every N boots */
#define OTA_BOOT_CHECK_INTERVAL 100

/** Maximum length of a constructed manifest URL */
#define MANIFEST_URL_MAX 512

/** Maximum size of the manifest JSON response body */
#define MANIFEST_BODY_MAX 1024

/** Maximum length of a version string */
#define VERSION_MAX 32

/** Base URL for the firmware server */
#define FIRMWARE_BASE_URL "https://firmware.sven.lol"

/** Default OTA partition label */
#define OTA_PARTITION_LABEL "ota_0"

/** Buffer size for streaming firmware download */
#define OTA_BUF_SIZE 4096

/** Progress reporting interval (percentage points) */
#define OTA_PROGRESS_STEP 20

/* ------------------------------------------------------------------ */
/*  WiFi connection for OTA                                            */
/* ------------------------------------------------------------------ */

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     5

static int s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * Connect to WiFi using credentials from core_config.
 * Blocks until connected or connection fails.
 */
static esp_err_t ota_wifi_connect(void)
{
    const core_config_t *cfg = core_config_get();

    if (cfg->wifi.ssid[0] == '\0') {
        ESP_LOGW(TAG, "No WiFi configured, cannot check for updates");
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi_event_group = xEventGroupCreate();
    s_retry_count = 0;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        vEventGroupDelete(s_wifi_event_group);
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        vEventGroupDelete(s_wifi_event_group);
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: 0x%x", err);
        vEventGroupDelete(s_wifi_event_group);
        return err;
    }

    esp_event_handler_instance_t inst_wifi;
    esp_event_handler_instance_t inst_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL,
                                        &inst_wifi);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL,
                                        &inst_ip);

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, cfg->wifi.ssid,
            sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, cfg->wifi.password,
            sizeof(wifi_cfg.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();

    ESP_LOGI(TAG, "Connecting to WiFi...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));

    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          inst_wifi);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                          inst_ip);
    vEventGroupDelete(s_wifi_event_group);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "WiFi connection failed");
    esp_wifi_stop();
    esp_wifi_deinit();
    return ESP_FAIL;
}

static void ota_wifi_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
}

/* ------------------------------------------------------------------ */
/*  Version comparison                                                 */
/* ------------------------------------------------------------------ */

/**
 * Compare two semver strings (major.minor.patch).
 * @return  > 0 if available is newer,
 *            0 if equal,
 *          < 0 if available is older.
 */
static int compare_versions(const char *current, const char *available)
{
    int cur_major = 0, cur_minor = 0, cur_patch = 0;
    int avl_major = 0, avl_minor = 0, avl_patch = 0;

    sscanf(current, "%d.%d.%d", &cur_major, &cur_minor, &cur_patch);
    sscanf(available, "%d.%d.%d", &avl_major, &avl_minor, &avl_patch);

    if (avl_major != cur_major) return avl_major - cur_major;
    if (avl_minor != cur_minor) return avl_minor - cur_minor;
    return avl_patch - cur_patch;
}

/* ------------------------------------------------------------------ */
/*  Current project firmware version                                   */
/* ------------------------------------------------------------------ */

/**
 * Read the version string from the project firmware partition.
 * Falls back to "0.0.0" if no valid image is present.
 */
static void get_current_project_version(char *out, size_t max)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY,
        OTA_PARTITION_LABEL);

    if (!part) {
        strncpy(out, "0.0.0", max - 1);
        out[max - 1] = '\0';
        return;
    }

    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(part, &desc) != ESP_OK) {
        strncpy(out, "0.0.0", max - 1);
        out[max - 1] = '\0';
        return;
    }

    strncpy(out, desc.version, max - 1);
    out[max - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Manifest fetch                                                     */
/* ------------------------------------------------------------------ */

/**
 * Fetch the firmware manifest JSON from the server.
 * Extracts the "version" and "bin" fields.
 */
static esp_err_t fetch_manifest(const char *url,
                                char *version_out, size_t version_max,
                                char *bin_url_out, size_t bin_url_max)
{
    char *body = malloc(MANIFEST_BODY_MAX);
    if (!body) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: 0x%x", err);
        esp_http_client_cleanup(client);
        free(body);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (status != 200) {
        ESP_LOGW(TAG, "Manifest request failed: HTTP %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(body);
        return ESP_FAIL;
    }

    int body_len = esp_http_client_read(client, body, MANIFEST_BODY_MAX - 1);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (body_len <= 0) {
        ESP_LOGW(TAG, "Empty manifest response");
        free(body);
        return ESP_FAIL;
    }
    body[body_len] = '\0';

    /* Parse JSON manifest */
    cJSON *root = cJSON_Parse(body);
    free(body);

    if (!root) {
        ESP_LOGW(TAG, "Failed to parse manifest JSON");
        return ESP_FAIL;
    }

    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *bin = cJSON_GetObjectItem(root, "bin");

    if (!cJSON_IsString(version) || !version->valuestring ||
        !cJSON_IsString(bin) || !bin->valuestring) {
        ESP_LOGW(TAG, "Invalid manifest format");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(version_out, version->valuestring, version_max - 1);
    version_out[version_max - 1] = '\0';
    strncpy(bin_url_out, bin->valuestring, bin_url_max - 1);
    bin_url_out[bin_url_max - 1] = '\0';

    cJSON_Delete(root);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  BLE status helper                                                  */
/* ------------------------------------------------------------------ */

/**
 * Send an OTA progress update via BLE status notification.
 * Safe to call when BLE is not running (silently ignored).
 */
static void ota_notify_status(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    core_ble_send_status(buf);
}

/* ------------------------------------------------------------------ */
/*  Partition management                                               */
/* ------------------------------------------------------------------ */

/**
 * Find the target OTA partition for writing new firmware.
 *
 * Returns the ota_0 partition (project firmware slot).
 * Refuses to return the factory partition (core firmware).
 */
static const esp_partition_t *ota_get_target_partition(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Current partition: %s", running->label);

    /* The project firmware always lives in ota_0.
     * The core firmware runs from factory.  We only ever
     * write to ota_0, never to factory. */
    const esp_partition_t *target = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);

    if (!target) {
        ESP_LOGE(TAG, "No OTA partition found");
        return NULL;
    }

    /* Safety: never overwrite the factory (core) partition */
    if (target->address == running->address
        && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        ESP_LOGE(TAG, "Refusing to overwrite core firmware partition");
        return NULL;
    }

    ESP_LOGI(TAG, "Next partition: %s", target->label);
    return target;
}

/* ------------------------------------------------------------------ */
/*  OTA firmware installation engine                                   */
/* ------------------------------------------------------------------ */

/**
 * Download firmware, stream it to the OTA partition, validate the
 * image, and switch the boot partition.
 *
 * Steps:
 *   1. Resolve the target OTA partition
 *   2. Open an HTTP stream to the firmware URL
 *   3. Begin OTA write session (esp_ota_begin)
 *   4. Read chunks and write them to flash with progress reporting
 *   5. Finalize the write (esp_ota_end)
 *   6. Validate the new image header
 *   7. Set the new boot partition
 *
 * On any failure the update is aborted and the current firmware
 * remains intact.
 */
static esp_err_t perform_ota_update(const char *firmware_url)
{
    ESP_LOGI(TAG, "Starting firmware download");
    ota_notify_status("{\"ota\":\"download_started\"}");

    /* 1. Find the target partition */
    const esp_partition_t *target = ota_get_target_partition();
    if (!target) {
        ota_notify_status("{\"ota\":\"failed\",\"reason\":\"no OTA partition\"}");
        return ESP_ERR_NOT_FOUND;
    }

    /* 2. Open HTTP connection */
    esp_http_client_config_t http_config = {
        .url = firmware_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        ota_notify_status("{\"ota\":\"failed\",\"reason\":\"HTTP init\"}");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP connection failed: 0x%x", err);
        ota_notify_status("{\"ota\":\"failed\",\"reason\":\"HTTP connect\"}");
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (status != 200) {
        ESP_LOGE(TAG, "Firmware download failed: HTTP %d", status);
        ota_notify_status("{\"ota\":\"failed\",\"reason\":\"HTTP %d\"}", status);
        ESP_LOGI(TAG, "Update aborted");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Downloading firmware (%d bytes)",
             content_length > 0 ? content_length : -1);
    ESP_LOGI(TAG, "Writing firmware to %s", target->label);
    ota_notify_status("{\"ota\":\"downloading\"}");

    /* 3. Begin OTA write session */
    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: 0x%x", err);
        ota_notify_status("{\"ota\":\"failed\",\"reason\":\"flash init\"}");
        ESP_LOGI(TAG, "Update aborted");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    /* 4. Stream download and write to flash */
    char *buf = malloc(OTA_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Buffer allocation failed");
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    int last_progress = 0;

    while (true) {
        int read_len = esp_http_client_read(client, buf, OTA_BUF_SIZE);
        if (read_len < 0) {
            ESP_LOGE(TAG, "Firmware download failed");
            ota_notify_status("{\"ota\":\"failed\",\"reason\":\"download error\"}");
            ESP_LOGI(TAG, "Update aborted");
            free(buf);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (read_len == 0) {
            /* End of stream */
            break;
        }

        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Flash write error: 0x%x", err);
            ota_notify_status("{\"ota\":\"failed\",\"reason\":\"flash write\"}");
            ESP_LOGI(TAG, "Update aborted");
            free(buf);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return err;
        }

        total_read += read_len;

        /* Report progress */
        if (content_length > 0) {
            int progress = (total_read * 100) / content_length;
            if (progress >= last_progress + OTA_PROGRESS_STEP) {
                last_progress = (progress / OTA_PROGRESS_STEP) * OTA_PROGRESS_STEP;
                ESP_LOGI(TAG, "Download progress: %d%%", last_progress);
                ota_notify_status("{\"ota\":\"progress\",\"percent\":%d}",
                                  last_progress);
            }
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Firmware successfully written (%d bytes)", total_read);
    ota_notify_status("{\"ota\":\"flashing\"}");

    /* 5. Finalize OTA — validates CRC and image header */
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Image verification failed: 0x%x", err);
        ota_notify_status("{\"ota\":\"failed\",\"reason\":\"verification\"}");
        ESP_LOGI(TAG, "Update aborted");
        return err;
    }

    ESP_LOGI(TAG, "Flash write complete");
    ESP_LOGI(TAG, "Image verification successful");

    /* 6. Validate the new image descriptor */
    esp_app_desc_t new_desc;
    err = esp_ota_get_partition_description(target, &new_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read new image descriptor: 0x%x", err);
        ota_notify_status("{\"ota\":\"failed\",\"reason\":\"image header\"}");
        return err;
    }
    ESP_LOGI(TAG, "New firmware: %s v%s", new_desc.project_name,
             new_desc.version);

    /* 7. Switch boot partition */
    ESP_LOGI(TAG, "Switching boot partition");
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: 0x%x", err);
        ota_notify_status("{\"ota\":\"failed\",\"reason\":\"boot switch\"}");
        return err;
    }

    ESP_LOGI(TAG, "Boot partition updated");
    ota_notify_status("{\"ota\":\"completed\"}");

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

esp_err_t core_ota_set_request_flag(void)
{
    return core_storage_write_u8(NVS_NAMESPACE_CORE,
                                 NVS_KEY_OTA_REQUEST, 1);
}

uint32_t core_ota_increment_boot_count(void)
{
    uint32_t count = 0;
    core_storage_read_u32(NVS_NAMESPACE_CORE, NVS_KEY_BOOT_COUNT, &count);
    count++;
    core_storage_write_u32(NVS_NAMESPACE_CORE, NVS_KEY_BOOT_COUNT, count);
    return count;
}

esp_err_t core_ota_check_and_update(void)
{
    const core_config_t *cfg = core_config_get();

    /* Connect to WiFi */
    esp_err_t err = ota_wifi_connect();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Checking firmware updates");

    /* Get current project firmware version */
    char current_version[VERSION_MAX] = {0};
    get_current_project_version(current_version, sizeof(current_version));
    ESP_LOGI(TAG, "Current version: %s", current_version);

    /* If bin_url is set, use the direct firmware URL (skip manifest) */
    if (cfg->firmware.bin_url[0] != '\0') {
        ESP_LOGI(TAG, "Using direct firmware URL");
        err = perform_ota_update(cfg->firmware.bin_url);
        ota_wifi_disconnect();
        if (err == ESP_OK) {
            ESP_LOGI("CORE", "Rebooting device");
            esp_restart();
        }
        return err;
    }

    /* Need firmware.project to construct the manifest URL */
    if (cfg->firmware.project[0] == '\0') {
        ESP_LOGW(TAG, "No firmware project configured");
        ota_wifi_disconnect();
        return ESP_ERR_INVALID_STATE;
    }

    /* Construct manifest URL */
    char manifest_url[MANIFEST_URL_MAX];
    snprintf(manifest_url, sizeof(manifest_url),
             FIRMWARE_BASE_URL "/%s/%s/manifest.json",
             cfg->firmware.project, cfg->firmware.channel);

    /* Fetch manifest */
    char available_version[VERSION_MAX] = {0};
    char firmware_url[CONFIG_FW_BIN_URL_MAX] = {0};

    err = fetch_manifest(manifest_url,
                         available_version, sizeof(available_version),
                         firmware_url, sizeof(firmware_url));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to fetch manifest");
        ota_wifi_disconnect();
        return err;
    }

    ESP_LOGI(TAG, "Available version: %s", available_version);

    /* Compare versions */
    if (compare_versions(current_version, available_version) <= 0) {
        ESP_LOGI(TAG, "Firmware is up to date");
        ota_wifi_disconnect();
        return ESP_ERR_NOT_FOUND;
    }

    /* New version available — start OTA */
    ESP_LOGI(TAG, "New version available, starting update");
    err = perform_ota_update(firmware_url);
    ota_wifi_disconnect();

    if (err == ESP_OK) {
        ESP_LOGI("CORE", "Rebooting device");
        esp_restart();
    }

    return err;
}

bool core_ota_run(void)
{
    /* 1. Increment boot counter */
    uint32_t boot_count = core_ota_increment_boot_count();
    ESP_LOGI("CORE", "Boot count: %lu", (unsigned long)boot_count);

    /* 2. Check OTA request flag */
    uint8_t ota_requested = 0;
    core_storage_read_u8(NVS_NAMESPACE_CORE, NVS_KEY_OTA_REQUEST,
                         &ota_requested);

    if (ota_requested) {
        ESP_LOGI(TAG, "OTA request flag detected");

        /* Clear the flag before starting */
        core_storage_erase_key(NVS_NAMESPACE_CORE, NVS_KEY_OTA_REQUEST);

        ESP_LOGI(TAG, "Starting update process");
        esp_err_t err = core_ota_check_and_update();
        if (err == ESP_OK) {
            return true; /* device will restart */
        }

        ESP_LOGW(TAG, "OTA update failed, continuing normal boot");
        return false;
    }

    /* 3. Check scheduled fallback */
    if (boot_count > 0 && (boot_count % OTA_BOOT_CHECK_INTERVAL) == 0) {
        ESP_LOGI(TAG, "Performing scheduled update check");

        esp_err_t err = core_ota_check_and_update();
        if (err == ESP_OK) {
            return true; /* device will restart */
        }

        if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Scheduled update check failed");
        }

        return false;
    }

    ESP_LOGI(TAG, "No update requested");
    return false;
}
