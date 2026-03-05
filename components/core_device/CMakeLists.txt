idf_component_register(
    SRCS "core_device.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash log esp_hw_support
)
