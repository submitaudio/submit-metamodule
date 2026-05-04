cmake_minimum_required(VERSION 3.22)

# Use METAMODULE_SDK_DIR if set, otherwise use default location
if(DEFINED METAMODULE_SDK_DIR)
    include(${METAMODULE_SDK_DIR}/plugin.cmake)
else()
    include($ENV{HOME}/metamodule-plugin-sdk/plugin.cmake)
endif()

project(Submit VERSION 1.0.0 DESCRIPTION "Submit plugin for MetaModule" LANGUAGES C CXX ASM)

add_library(Submit STATIC)

target_sources(Submit PRIVATE
    src/plugin.cpp
    src/Drift13.cpp
    src/Impact.cpp
    src/Chrono.cpp
    src/Chain.cpp src/Shape.cpp src/Master.cpp
    src/Squeeze.cpp
    src/Gain.cpp
    src/Sweep.cpp)

target_include_directories(Submit PRIVATE src)

create_plugin(
    SOURCE_LIB      Submit
    PLUGIN_NAME     Submit
    PLUGIN_JSON     ${CMAKE_CURRENT_LIST_DIR}/plugin.json
    SOURCE_ASSETS   ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION     ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins
)
