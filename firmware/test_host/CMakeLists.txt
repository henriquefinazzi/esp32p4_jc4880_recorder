cmake_minimum_required(VERSION 3.16)
project(recorder_host_tests C)

set(CMAKE_C_STANDARD 11)
enable_testing()

add_executable(test_wav_writer
    test_wav_writer.c
    ../components/wav_writer/wav_writer.c
)
target_include_directories(test_wav_writer PRIVATE ../components/wav_writer/include)
target_compile_options(test_wav_writer PRIVATE -Wall -Wextra -Werror)

add_test(NAME wav_writer COMMAND test_wav_writer)

set(CJSON_DIR $ENV{HOME}/.platformio/packages/framework-espidf/components/json/cJSON CACHE PATH "Pasta do cJSON")
add_executable(test_settings_file
    test_settings_file.c
    ../components/settings_file/settings_file.c
    ${CJSON_DIR}/cJSON.c
)
target_include_directories(test_settings_file PRIVATE ../components/settings_file/include ${CJSON_DIR})
target_compile_options(test_settings_file PRIVATE -Wall -Wextra)
set_source_files_properties(test_settings_file.c ../components/settings_file/settings_file.c PROPERTIES COMPILE_OPTIONS -Werror)

add_test(NAME settings_file COMMAND test_settings_file)

# Formatos de gravação (WAV, Opus em Ogg, MP3 pelo Shine), com libopus do computador.
find_path(OPUS_INCLUDE opus/opus.h PATHS /opt/homebrew/include /usr/local/include)
find_library(OPUS_LIB opus PATHS /opt/homebrew/lib /usr/local/lib)
file(GLOB SHINE_SRC ../components/shine/src/*.c)
add_executable(test_rec_writer
    test_rec_writer.c opus_shim_host.c
    ../components/rec_writer/rec_writer.c ../components/rec_writer/ogg_opus.c
    ../components/wav_writer/wav_writer.c ${SHINE_SRC}
)
target_include_directories(test_rec_writer PRIVATE ../components/rec_writer/include ../components/rec_writer
    ../components/wav_writer/include ../components/shine/src ${OPUS_INCLUDE})
target_link_libraries(test_rec_writer PRIVATE ${OPUS_LIB} m)
set_source_files_properties(${SHINE_SRC} PROPERTIES COMPILE_OPTIONS -w)
set_source_files_properties(test_rec_writer.c ../components/rec_writer/rec_writer.c ../components/rec_writer/ogg_opus.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
add_test(NAME rec_writer COMMAND test_rec_writer ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME rec_files COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check_rec_files.sh ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(rec_files PROPERTIES DEPENDS rec_writer)

add_executable(test_audio_dsp test_audio_dsp.c ../components/audio_dsp/audio_dsp.c)
target_include_directories(test_audio_dsp PRIVATE ../components/audio_dsp/include)
target_compile_options(test_audio_dsp PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_audio_dsp PRIVATE m)
add_test(NAME audio_dsp COMMAND test_audio_dsp)
