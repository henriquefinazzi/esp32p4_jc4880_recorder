idf_component_register(
    SRCS "audio_in.c"
    INCLUDE_DIRS "include"
    REQUIRES board driver espressif__esp_codec_dev
)
