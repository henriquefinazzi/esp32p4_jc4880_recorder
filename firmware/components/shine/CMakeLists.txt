idf_component_register(
    SRCS "src/bitstream.c" "src/huffman.c" "src/l3bitstream.c" "src/l3loop.c" "src/l3mdct.c"
         "src/l3subband.c" "src/layer3.c" "src/reservoir.c" "src/tables.c"
    INCLUDE_DIRS "src"
)
# Código de terceiros: sem -Werror nos avisos dele.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
