/**
 * core_device.c
 *
 * Generates a deterministic UUIDv4-format device ID from the base
 * MAC address of the ESP32.  The UUID is stored in NVS on first boot
 * so the device keeps the same identity across reboots and firmware
 * updates.
 *
 * UUID construction:
 *   - 16 raw bytes are derived by repeating/hashing the 6-byte MAC.
 *   - Version nibble (bits 48-51) is forced to 0x4 (UUIDv4).
 *   - Variant bits  (bits 62-63) are forced to 0b10 (RFC 4122).
 */

#include "core_device.h"
#include "core_storage.h"

#include <string.h>
#include <stdio.h>
#include "nvs.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "DEVICE";

static char device_id[DEVICE_ID_STR_LEN] = "unknown";

/**
 * Build 16 UUID bytes deterministically from a 6-byte MAC address.
 * The MAC is spread across the 16 bytes to maximise entropy, then
 * the version and variant bits are set per RFC 4122 (UUIDv4 layout).
 */
static void mac_to_uuid_bytes(const uint8_t mac[6], uint8_t uuid[16])
{
    /*
     * Fill 16 bytes from the 6-byte MAC by mixing positions.
     * This is not cryptographic randomness — it is a deterministic
     * mapping that gives every unique MAC a unique UUID.
     */
    uuid[0]  = mac[0];
    uuid[1]  = mac[1];
    uuid[2]  = mac[2];
    uuid[3]  = mac[3];
    uuid[4]  = mac[4];
    uuid[5]  = mac[5];
    uuid[6]  = mac[0] ^ mac[2];
    uuid[7]  = mac[1] ^ mac[3];
    uuid[8]  = mac[2] ^ mac[4];
    uuid[9]  = mac[3] ^ mac[5];
    uuid[10] = mac[4] ^ mac[0];
    uuid[11] = mac[5] ^ mac[1];
    uuid[12] = mac[0] ^ mac[5];
    uuid[13] = mac[1] ^ mac[4];
    uuid[14] = mac[2] ^ mac[3];
    uuid[15] = mac[3] ^ mac[0];

    /* Set version to 4 (bits 48-51) */
    uuid[6] = (uuid[6] & 0x0F) | 0x40;

    /* Set variant to RFC 4122 (bits 62-63 = 10) */
    uuid[8] = (uuid[8] & 0x3F) | 0x80;
}

/**
 * Format 16 raw bytes into a standard UUID string:
 *   xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 */
static void uuid_bytes_to_string(const uint8_t uuid[16], char *out)
{
    snprintf(out, DEVICE_ID_STR_LEN,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid[0], uuid[1], uuid[2],  uuid[3],
             uuid[4], uuid[5], uuid[6],  uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11],
             uuid[12], uuid[13], uuid[14], uuid[15]);
}

esp_err_t core_device_init(void)
{
    ESP_LOGI(TAG, "Initializing device identity...");

    /* Try to load an existing device ID from NVS */
    esp_err_t err = core_storage_read_str(NVS_NAMESPACE_DEVICE, NVS_KEY_DEVICE_ID,
                                          device_id, DEVICE_ID_STR_LEN);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded device ID from NVS");
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* First boot — generate a new device ID from the MAC address */
        uint8_t mac[6];
        err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read MAC address: 0x%x", err);
            return err;
        }

        uint8_t uuid_bytes[16];
        mac_to_uuid_bytes(mac, uuid_bytes);
        uuid_bytes_to_string(uuid_bytes, device_id);

        /* Persist the new ID */
        err = core_storage_write_str(NVS_NAMESPACE_DEVICE, NVS_KEY_DEVICE_ID, device_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store device ID: 0x%x", err);
            return err;
        }

        ESP_LOGI(TAG, "Generated and stored new device ID");
    } else {
        ESP_LOGE(TAG, "Failed to read device ID from NVS: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "Device ID: %s", device_id);
    return ESP_OK;
}

const char *core_device_get_id(void)
{
    return device_id;
}
