idf_component_register(
    SRCS "core_project.c"
    INCLUDE_DIRS "include"
    REQUIRES log esp_partition app_update esp_app_format esp_system core_storage
)
