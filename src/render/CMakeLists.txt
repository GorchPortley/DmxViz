dmxviz_add_module(render DEPS dmxviz::core dmxviz::assets dep::sokol)

# GLSL sources live in shaders/*.glsl and are embedded into a generated header
# (render/EmbeddedShaders.h) at build time. Editing or adding a .glsl file
# re-runs the step on the next build.
file(GLOB _render_shaders CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl")
set(_render_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_render_shader_header "${_render_gen_dir}/render/EmbeddedShaders.h")
add_custom_command(
    OUTPUT "${_render_shader_header}"
    COMMAND ${CMAKE_COMMAND}
        "-DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "-DOUTPUT=${_render_shader_header}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS ${_render_shaders} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    COMMENT "Embedding render shaders"
    VERBATIM)
target_sources(dmxviz_render PRIVATE "${_render_shader_header}")
target_include_directories(dmxviz_render PRIVATE "${_render_gen_dir}")
