idf_component_register(
    SRCS "core_ble.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bt json freertos log esp_system esp_timer core_config core_device core_storage core_boot core_ota
)
