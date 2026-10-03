# sim_bench: headless CPU benchmark of the per-frame simulation (NFR-1: < 2 ms for 500 fixtures / 1000 beams /
# 64 universes, no heap allocation in steady state). No window and no GPU.
#
# BeamPacker (the CPU half of the volumetric pass) is compiled straight into the tool instead of linking
# dmxviz::render: that library carries the sokol implementation, whose sokol_app defines main() and would
# clash with ours. GoboAtlasStub.cpp supplies the one GoboAtlas function BeamPacker calls.
add_executable(sim_bench
    main.cpp
    AllocCounter.cpp
    RigBuilder.cpp
    DmxAnimator.cpp
    GoboAtlasStub.cpp
    "${PROJECT_SOURCE_DIR}/src/app/Simulation.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/BeamPacker.cpp")
target_link_libraries(sim_bench PRIVATE dmxviz::stage dmxviz::fixtures dmxviz::dmx dmxviz::assets dmxviz::core)
# GoboAtlas.h includes sokol_gfx.h for its type declarations (header only, no GL is touched).
target_include_directories(sim_bench SYSTEM PRIVATE $<TARGET_PROPERTY:dep::sokol,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(sim_bench PRIVATE SOKOL_GLCORE DMXVIZ_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
dmxviz_set_warnings(sim_bench)
