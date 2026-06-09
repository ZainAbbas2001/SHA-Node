cmake_minimum_required(VERSION 3.16)

# Register all source component directories before the IDF project() call
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/drivers"
    "${CMAKE_CURRENT_SOURCE_DIR}/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/tasks"
    "${CMAKE_CURRENT_SOURCE_DIR}/net"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(sha-node-firmware
    VERSION 1.0.0
    DESCRIPTION "SHA-Node ESP32-S3 firmware"
)
