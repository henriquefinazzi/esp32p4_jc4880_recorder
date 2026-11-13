cmake_minimum_required(VERSION 3.16.0)
# Versão do binário: a mesma de APP_VERSION em src/app.h (e não a descrição do git).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/src/app.h" _ver REGEX "#define APP_VERSION \"[^\"]+\"")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" PROJECT_VER "${_ver}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32p4_jc4880_recorder)
