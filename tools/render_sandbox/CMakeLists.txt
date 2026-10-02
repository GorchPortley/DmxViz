# render_sandbox: standalone test bed and benchmark for the render module.
# Builds a demo stage by hand (no fixtures/stage modules) and can write a PNG.
file(GLOB _sandbox_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
# WIN32: GUI subsystem on Windows; sokol_app supplies WinMain and forwards argv to sokol_main.
add_executable(render_sandbox WIN32 ${_sandbox_sources})
target_link_libraries(render_sandbox PRIVATE dmxviz::render dmxviz::assets dmxviz::core dep::sokol dep::imgui)
dmxviz_set_warnings(render_sandbox)
