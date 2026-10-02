# EmbedShaders.cmake – turns every src/render/shaders/*.glsl file into a byte
# array inside one generated C++ header, so the renderer needs no shader files
# at run time.
#
# Run as a script:  cmake -DSHADER_DIR=<dir> -DOUTPUT=<header> -P EmbedShaders.cmake
#
# The sources are written as hex byte arrays instead of string literals because
# MSVC limits string literals to 64 KB (and single pieces to 16 KB).

if(NOT SHADER_DIR OR NOT OUTPUT)
    message(FATAL_ERROR "EmbedShaders.cmake needs -DSHADER_DIR=... and -DOUTPUT=...")
endif()

file(GLOB _files "${SHADER_DIR}/*.glsl")
list(SORT _files)

set(_body "")
set(_table "")
foreach(_file IN LISTS _files)
    get_filename_component(_name "${_file}" NAME)
    string(MAKE_C_IDENTIFIER "${_name}" _ident)
    file(READ "${_file}" _hex HEX)
    # "2376..." -> "0x23,0x76,..." and one output line per source line (0x0a = '\n').
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _hex "${_hex}")
    string(REPLACE "0x0a," "0x0a,\n    " _hex "${_hex}")
    string(APPEND _body "// ${_name}\ninline const char k_${_ident}[] = {\n    ${_hex}0x00};\n\n")
    string(APPEND _table "    {\"${_name}\", k_${_ident}},\n")
endforeach()

set(_content "// Generated from src/render/shaders/*.glsl by src/render/cmake/EmbedShaders.cmake.\n")
string(APPEND _content "// Do not edit: change the .glsl files instead.\n#pragma once\n\n")
string(APPEND _content "namespace dmxviz::render::embedded {\n\n")
string(APPEND _content "struct ShaderFile {\n    const char* name;\n    const char* source;\n};\n\n")
string(APPEND _content "${_body}")
string(APPEND _content "inline const ShaderFile kShaderFiles[] = {\n${_table}};\n\n")
string(APPEND _content "}  // namespace dmxviz::render::embedded\n")

# Only touch the header when the content changed, so unrelated rebuilds stay incremental.
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" _old)
    if(_old STREQUAL _content)
        return()
    endif()
endif()
file(WRITE "${OUTPUT}" "${_content}")
