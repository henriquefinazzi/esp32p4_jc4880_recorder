idf_component_register(
    SRCS "i18n.c"
    INCLUDE_DIRS "include"
    REQUIRES json
    PRIV_REQUIRES esp_psram
    EMBED_TXTFILES "../../lang/pt.json" "../../lang/en.json"
)
