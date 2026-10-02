# dmxviz_set_warnings(<target>)
# Applies the project warning policy to first-party targets only. Third-party
# code is included as SYSTEM so its warnings never show up.
function(dmxviz_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /wd4100 /wd4201)
        if(DMXVIZ_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter
            -Wno-missing-field-initializers)
        if(DMXVIZ_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# dmxviz_add_module(<name> [DEPS <targets...>] [PRIVATE_DEPS <targets...>])
# Creates static library dmxviz_<name> from every .cpp/.c file under the
# current source directory. Headers are included as "<module>/<File>.h".
function(dmxviz_add_module name)
    cmake_parse_arguments(ARG "" "" "DEPS;PRIVATE_DEPS" ${ARGN})
    file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
    add_library(dmxviz_${name} STATIC ${_sources} ${_headers})
    add_library(dmxviz::${name} ALIAS dmxviz_${name})
    target_include_directories(dmxviz_${name} PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(dmxviz_${name} PUBLIC ${ARG_DEPS} PRIVATE ${ARG_PRIVATE_DEPS})
    target_compile_features(dmxviz_${name} PUBLIC cxx_std_20)
    dmxviz_set_warnings(dmxviz_${name})
endfunction()
