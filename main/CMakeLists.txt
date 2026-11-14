idf_component_register(
    SRCS "main.c"
         "beatbox_engine.c"
         "beatbox_audio.c"
         "beatbox_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash
)
