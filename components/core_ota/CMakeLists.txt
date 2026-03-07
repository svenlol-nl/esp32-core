idf_component_register(
    SRCS "core_ota.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log json freertos esp_http_client esp_https_ota esp_wifi esp_netif esp_event esp_partition app_update esp_app_format mbedtls core_storage core_config core_ble
)
