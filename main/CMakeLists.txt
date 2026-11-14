idf_component_register(
    SRCS "main.c"
         "meihua_cast.c"
         "meihua_ui.c"
         "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/meihua_font_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash
)
