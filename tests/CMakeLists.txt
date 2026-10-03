# One doctest executable for all non-GPU modules. Each work stream adds files
# under tests/<module>/; they are globbed automatically.
file(GLOB_RECURSE _test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(dmxviz_tests ${_test_sources})
target_link_libraries(dmxviz_tests PRIVATE dep::doctest
    dmxviz::core dmxviz::assets dmxviz::dmx dmxviz::fixtures dmxviz::stage dmxviz::ui)
# The app is an executable, so its sokol-free parts are compiled straight into the tests.
target_sources(dmxviz_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app/ProjectIO.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/UserSettings.cpp"
    # Render logic without GPU calls (the render module itself needs sokol).
    "${PROJECT_SOURCE_DIR}/src/render/AutoQuality.cpp")
target_compile_definitions(dmxviz_tests PRIVATE
    # Let doctest include <ostream>/<string> itself; MSVC does not pull them in transitively.
    DOCTEST_CONFIG_USE_STD_HEADERS
    DMXVIZ_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    DMXVIZ_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
dmxviz_set_warnings(dmxviz_tests)
add_test(NAME dmxviz_tests COMMAND dmxviz_tests --duration=true)
set_tests_properties(dmxviz_tests PROPERTIES TIMEOUT 240)
