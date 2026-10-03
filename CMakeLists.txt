cmake_minimum_required(VERSION 3.24)

project(DmxViz
    VERSION 0.1.0
    DESCRIPTION "Real-time DMX lighting visualizer"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(DMXVIZ_BUILD_TESTS "Build unit tests" ON)
option(DMXVIZ_BUILD_APP   "Build the GUI application (needs a GL-capable platform)" ON)
option(DMXVIZ_WARNINGS_AS_ERRORS "Treat compiler warnings in DmxViz code as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Dependencies)
include(CompilerWarnings)

# Every DmxViz module is a static library under src/<module>. Sources are
# globbed so parallel work streams can add files without editing shared lists.
add_subdirectory(src/core)
add_subdirectory(src/assets)
add_subdirectory(src/dmx)
add_subdirectory(src/fixtures)
add_subdirectory(src/stage)

# render and ui are plain static libraries; the tests link ui (headless view-model code), so they are
# built whenever either the app or the tests are.
if(DMXVIZ_BUILD_APP OR DMXVIZ_BUILD_TESTS)
    add_subdirectory(src/render)
    add_subdirectory(src/ui)
endif()

if(DMXVIZ_BUILD_APP)
    add_subdirectory(src/app)
    if(EXISTS "${PROJECT_SOURCE_DIR}/tools/CMakeLists.txt")
        add_subdirectory(tools)
    endif()
endif()

if(DMXVIZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
