/**
 * core_config.h
 *
 * Device configuration data model, loading, saving, and validation.
 *
 * Responsibilities:
 *   - Define a structured configuration (wifi, firmware, system)
 *   - Load / save the configuration from / to NVS
 *   - Validate configuration fields before accepting updates
 *   - Provide read-only access to the current configuration
 *
 * Designed so that BLE (future) can request the full config,
 * send updates, and trigger a save + reboot.
 */

#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Field size limits                                                   */
/* ------------------------------------------------------------------ */

#define CONFIG_WIFI_SSID_MAX      33   /* 32 chars + null */
#define CONFIG_WIFI_PASSWORD_MAX  65   /* 64 chars + null */
#define CONFIG_FW_PROJECT_MAX     33   /* project name */
#define CONFIG_FW_CHANNEL_MAX     17   /* release channel */
#define CONFIG_FW_BIN_URL_MAX     256  /* optional binary URL */

/* ------------------------------------------------------------------ */
/*  Known firmware channels                                            */
/* ------------------------------------------------------------------ */

#define CONFIG_CHANNEL_STABLE     "stable"
#define CONFIG_CHANNEL_BETA       "beta"
#define CONFIG_CHANNEL_DEV        "dev"

/* ------------------------------------------------------------------ */
/*  Configuration structure                                            */
/* ------------------------------------------------------------------ */

/**
 * WiFi configuration.
 */
typedef struct {
    char ssid[CONFIG_WIFI_SSID_MAX];
    char password[CONFIG_WIFI_PASSWORD_MAX];
} core_config_wifi_t;

/**
 * Firmware / project configuration.
 */
typedef struct {
    char project[CONFIG_FW_PROJECT_MAX];
    char channel[CONFIG_FW_CHANNEL_MAX];
    char bin_url[CONFIG_FW_BIN_URL_MAX];
} core_config_firmware_t;

/**
 * System-level configuration.
 */
typedef struct {
    uint8_t local_configure_enabled;   /**< 1 = force LOCAL_CONFIGURE on boot */
} core_config_system_t;

/**
 * Top-level device configuration.
 *
 * Mirrors a JSON-friendly layout:
 * {
 *   "wifi":     { "ssid": "", "password": "" },
 *   "firmware": { "project": "", "channel": "", "bin_url": "" },
 *   "system":   { "local_configure_enabled": 0 }
 * }
 */
typedef struct {
    core_config_wifi_t     wifi;
    core_config_firmware_t firmware;
    core_config_system_t   system;
} core_config_t;

/* ------------------------------------------------------------------ */
/*  Initialization & lifecycle                                         */
/* ------------------------------------------------------------------ */

/**
 * Initialize the configuration module.
 *
 * Loads the configuration from NVS.  If no configuration has been
 * stored yet the in-memory config is left at defaults (zeroed / empty
 * strings, local_configure_enabled = 1).
 *
 * @return ESP_OK on success.
 */
esp_err_t core_config_init(void);

/* ------------------------------------------------------------------ */
/*  Read access                                                        */
/* ------------------------------------------------------------------ */

/**
 * Get a read-only pointer to the current configuration.
 *
 * The pointer is valid for the lifetime of the application (the
 * config lives in a static buffer).
 */
const core_config_t *core_config_get(void);

/**
 * Check whether a valid WiFi SSID has been configured.
 */
bool core_config_has_wifi(void);

/**
 * Check whether a firmware project name has been configured.
 */
bool core_config_has_project(void);

/* ------------------------------------------------------------------ */
/*  Write access                                                       */
/* ------------------------------------------------------------------ */

/**
 * Validate and apply a new configuration.
 *
 * Runs the full validation suite.  If validation passes the
 * in-memory configuration is updated but NOT yet persisted.
 *
 * @param cfg  Proposed new configuration.
 * @return ESP_OK                on success (in-memory updated).
 * @return ESP_ERR_INVALID_ARG   one or more fields failed validation.
 */
esp_err_t core_config_update(const core_config_t *cfg);

/**
 * Persist the current in-memory configuration to NVS.
 *
 * @return ESP_OK on success, or an NVS error.
 */
esp_err_t core_config_save(void);

/* ------------------------------------------------------------------ */
/*  Validation                                                         */
/* ------------------------------------------------------------------ */

/**
 * Validate a configuration without applying it.
 *
 * Logs each validation error it finds.
 *
 * @param cfg  Configuration to validate.
 * @return true if all checks pass.
 */
bool core_config_validate(const core_config_t *cfg);

/* ------------------------------------------------------------------ */
/*  Local-configure mode                                               */
/* ------------------------------------------------------------------ */

/**
 * Enter local-configure mode.
 *
 * Loads the current configuration, logs it, and waits for
 * configuration updates (BLE in the future).  This function does
 * not return under normal circumstances.
 */
void core_config_enter_local_configure(void);

/**
 * Perform a factory reset.
 *
 * Erases all configuration data from NVS (WiFi, firmware, system
 * settings) while preserving the device identity.
 * Resets the in-memory configuration to safe defaults.
 *
 * @return ESP_OK on success, or an NVS error.
 */
esp_err_t core_config_factory_reset(void);

#endif /* CORE_CONFIG_H */
