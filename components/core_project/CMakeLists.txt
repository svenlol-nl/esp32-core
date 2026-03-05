idf_component_register(
    SRCS "core_project.c"
    INCLUDE_DIRS "include"
    REQUIRES log esp_partition core_storage
)
