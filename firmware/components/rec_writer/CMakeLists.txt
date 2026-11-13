idf_component_register(
    SRCS "rec_writer.c" "ogg_opus.c" "opus_shim_esp.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES wav_writer
    PRIV_REQUIRES shine espressif__esp_audio_codec
)
