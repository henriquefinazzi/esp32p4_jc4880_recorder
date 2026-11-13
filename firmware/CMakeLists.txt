cmake_minimum_required(VERSION 3.16.0)
# Binary version: same as APP_VERSION in src/app.h (not the git description).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/src/app.h" _ver REGEX "#define APP_VERSION \"[^\"]+\"")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" PROJECT_VER "${_ver}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32p4_jc4880_recorder)
