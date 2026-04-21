cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sonotify_nfc_esp32)

# SPIFFS image (make sure 'data/' exists)
set(SPIFFS_IMAGE_DIR ${CMAKE_SOURCE_DIR}/data)
if(EXISTS ${SPIFFS_IMAGE_DIR})
    spiffs_create_partition_image(spiffs ${SPIFFS_IMAGE_DIR} FLASH_IN_PROJECT)
endif()
