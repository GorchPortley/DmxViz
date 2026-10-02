file(GLOB_RECURSE _app_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
# WIN32: GUI subsystem on Windows; sokol_app supplies WinMain and forwards argv to sokol_main.
add_executable(dmxviz WIN32 ${_app_sources})
target_link_libraries(dmxviz PRIVATE dmxviz::ui dmxviz::render dmxviz::stage dmxviz::fixtures dmxviz::dmx
    dmxviz::assets dmxviz::core dep::sokol dep::imgui)
dmxviz_set_warnings(dmxviz)
# Bundled data (fixture library, gobos, demo shows) is looked up next to the executable.
add_custom_command(TARGET dmxviz POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:dmxviz>/data")
