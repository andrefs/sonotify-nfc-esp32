cmake_minimum_required(VERSION 3.16)
project(sonotify_dispatch_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

set(MAIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../main")
set(STUB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stubs")

# --- cJSON: prefer the IDF-managed component if present, else system package ---
set(CJSON_MANAGED "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/espressif__cjson/cJSON")
if(EXISTS "${CJSON_MANAGED}/cJSON.h")
  set(CJSON_INCLUDE_DIR "${CJSON_MANAGED}")
  set(CJSON_SOURCE "${CJSON_MANAGED}/cJSON.c")
else()
  find_path(CJSON_INCLUDE_DIR cJSON.h
            PATH_SUFFIXES cjson cJSON)
  find_library(CJSON_LIBRARY NAMES cjson)
  if(NOT CJSON_INCLUDE_DIR OR NOT CJSON_LIBRARY)
    message(FATAL_ERROR
            "cJSON not found. Build once with idf.py (pulls the managed "
            "component) or install libcjson-dev.")
  endif()
endif()

add_executable(dispatch_tests
    dispatch_tests.c
    stubs/stubs.c
    ${MAIN_DIR}/dispatch.c
    ${CJSON_SOURCE})

target_include_directories(dispatch_tests PRIVATE
    ${STUB_DIR}
    ${MAIN_DIR}
    ${CJSON_INCLUDE_DIR})

target_compile_definitions(dispatch_tests PRIVATE
    CONFIG_SONOS_ENTITY_ID="media_player.roam_2")

target_link_libraries(dispatch_tests PRIVATE ${CJSON_LIBRARY} m)

add_test(NAME dispatch_tests COMMAND dispatch_tests)