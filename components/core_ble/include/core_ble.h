/**
 * core_ble.h
 *
 * BLE GATT server for device configuration and command control.
 *
 * Provides a custom BLE service with characteristics for:
 *   - Reading device configuration (JSON)
 *   - Writing device configuration (JSON)
 *   - Receiving device commands (JSON)
 *   - Sending status notifications
 *
 * The BLE server only runs when the device is in local_configure_mode.
 * It communicates with core_config to manage configuration and with
 * core_device for device identification.
 */

#ifndef CORE_BLE_H
#define CORE_BLE_H

#include "esp_err.h"

/**
 * Start the BLE GATT server.
 *
 * Initializes NimBLE, registers the device management service,
 * and begins advertising.  The advertisement name includes an
 * identifier derived from the device ID (e.g. "ESP-AB12CD").
 *
 * Must be called after core_device_init() and core_config_init().
 *
 * @return ESP_OK on success.
 */
esp_err_t core_ble_start(void);

/**
 * Send a status notification to the connected BLE client.
 *
 * If no client is connected the call is silently ignored.
 *
 * @param status  Null-terminated status message.
 */
void core_ble_send_status(const char *status);

#endif /* CORE_BLE_H */
