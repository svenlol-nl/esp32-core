idf_component_register(
    SRCS "core_config.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos core_storage core_ble
)
