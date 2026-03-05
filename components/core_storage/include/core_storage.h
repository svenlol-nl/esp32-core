/**
 * core_storage.h
 *
 * NVS (Non-Volatile Storage) initialization and typed read/write helpers.
 * Handles init, full-partition recovery, namespace management, and
 * provides a simple API for reading and writing NVS values by namespace
 * and key.
 */

#ifndef CORE_STORAGE_H
#define CORE_STORAGE_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  NVS Namespaces                                                     */
/* ------------------------------------------------------------------ */

/** Core firmware settings (boot flags, partition config) */
#define NVS_NAMESPACE_CORE   "core"

/** Device metadata (device ID, hardware info) */
#define NVS_NAMESPACE_DEVICE "device"

/* ------------------------------------------------------------------ */
/*  NVS Keys                                                           */
/* ------------------------------------------------------------------ */

/** uint8 flag – set to 1 to force local configure mode on next boot */
#define NVS_KEY_LOCAL_CONFIGURE "local_cfg"

/** string – label of the partition holding the project firmware */
#define NVS_KEY_PROJECT_PARTITION "proj_part"

/** string – the unique device identifier (UUID) */
#define NVS_KEY_DEVICE_ID "device_id"

/* ------------------------------------------------------------------ */
/*  Initialization                                                     */
/* ------------------------------------------------------------------ */

/**
 * Initialize the NVS flash subsystem.
 *
 * Handles the standard recovery cases:
 *  - NVS partition truncated or corrupt → erase and reinit
 *  - NVS partition full → erase and reinit
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t core_storage_init(void);

/* ------------------------------------------------------------------ */
/*  Typed read helpers                                                 */
/* ------------------------------------------------------------------ */

/**
 * Read a uint8 value from NVS.
 *
 * @param ns    NVS namespace (e.g. NVS_NAMESPACE_CORE)
 * @param key   NVS key
 * @param out   Pointer to store the value
 * @return ESP_OK, ESP_ERR_NVS_NOT_FOUND, or another NVS error.
 */
esp_err_t core_storage_read_u8(const char *ns, const char *key, uint8_t *out);

/**
 * Read a string value from NVS.
 *
 * @param ns       NVS namespace
 * @param key      NVS key
 * @param out      Buffer to receive the string
 * @param max_len  Size of the buffer (including null terminator)
 * @return ESP_OK, ESP_ERR_NVS_NOT_FOUND, or another NVS error.
 */
esp_err_t core_storage_read_str(const char *ns, const char *key,
                                char *out, size_t max_len);

/* ------------------------------------------------------------------ */
/*  Typed write helpers                                                */
/* ------------------------------------------------------------------ */

/**
 * Write a uint8 value to NVS (opens, writes, commits, closes).
 *
 * @param ns    NVS namespace
 * @param key   NVS key
 * @param value Value to store
 * @return ESP_OK on success, or an NVS error.
 */
esp_err_t core_storage_write_u8(const char *ns, const char *key, uint8_t value);

/**
 * Write a string value to NVS (opens, writes, commits, closes).
 *
 * @param ns    NVS namespace
 * @param key   NVS key
 * @param value Null-terminated string to store
 * @return ESP_OK on success, or an NVS error.
 */
esp_err_t core_storage_write_str(const char *ns, const char *key,
                                 const char *value);

#endif // CORE_STORAGE_H
