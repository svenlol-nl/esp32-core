idf_component_register(
    SRCS "core_ble.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bt json freertos log esp_system core_config core_device
)
