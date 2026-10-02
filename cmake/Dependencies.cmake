# Third-party dependencies. All are permissively licensed open source and are
# pinned to an exact tag or commit. See docs/adr/0001-tech-stack.md.
#
# Sources are fetched with git (FetchContent) but never add_subdirectory()'d:
# we define small targets ourselves so the build stays fast and warning-free.
#
# Offline / shared cache: configure with -DDMXVIZ_DEPS_DIR=/path/to/deps where
# each dependency lives in a subdirectory named like the first argument of
# dmxviz_fetch() below (e.g. /path/to/deps/imgui). Missing ones are fetched.

include(FetchContent)

set(DMXVIZ_DEPS_DIR "" CACHE PATH "Optional directory holding pre-cloned dependency sources")

macro(dmxviz_fetch name repo tag)
    string(TOUPPER "${name}" _upper)
    if(DMXVIZ_DEPS_DIR AND EXISTS "${DMXVIZ_DEPS_DIR}/${name}")
        set(FETCHCONTENT_SOURCE_DIR_${_upper} "${DMXVIZ_DEPS_DIR}/${name}")
    endif()
    # A 40-char hex tag is a commit and cannot be fetched shallowly.
    string(LENGTH "${tag}" _taglen)
    if(_taglen EQUAL 40)
        set(_shallow FALSE)
    else()
        set(_shallow TRUE)
    endif()
    FetchContent_Declare(${name}
        GIT_REPOSITORY "${repo}"
        GIT_TAG        "${tag}"
        GIT_SHALLOW    ${_shallow}
        GIT_PROGRESS   FALSE
        SOURCE_SUBDIR  "__dmxviz_no_cmake__")
    FetchContent_MakeAvailable(${name})
endmacro()

dmxviz_fetch(sokol    https://github.com/floooh/sokol.git                    2e75443dbd4940b5aa8d76a8e479f8e4b270b9a3)
dmxviz_fetch(imgui    https://github.com/ocornut/imgui.git                   v1.92.9b-docking)
dmxviz_fetch(imguizmo https://github.com/CedricGuillemet/ImGuizmo.git        18cef5e031d8c6973d80284c67f60549fafd78c1)
dmxviz_fetch(json     https://github.com/nlohmann/json.git                   v3.12.0)
dmxviz_fetch(pugixml  https://github.com/zeux/pugixml.git                    v1.16)
dmxviz_fetch(miniz    https://github.com/richgel999/miniz.git                3.1.2)
dmxviz_fetch(cgltf    https://github.com/jkuhlmann/cgltf.git                 v1.15)
dmxviz_fetch(glm      https://github.com/g-truc/glm.git                      1.0.3)
dmxviz_fetch(stb      https://github.com/nothings/stb.git                    2c980bb59875b0d32144a71867fbdebb2f77cd20)
dmxviz_fetch(nanosvg  https://github.com/memononen/nanosvg.git               239e102ec2c691f2902e20ace2ed36ee4a35cfe6)
dmxviz_fetch(pfd      https://github.com/samhocevar/portable-file-dialogs.git c12ea8c9a727f5320a2b4570aee863bbede2a204)
if(DMXVIZ_BUILD_TESTS)
    dmxviz_fetch(doctest https://github.com/doctest/doctest.git              v2.5.3)
endif()

set(_tp "${PROJECT_SOURCE_DIR}/third_party")

# --- header-only ------------------------------------------------------------
add_library(dep_glm INTERFACE)
target_include_directories(dep_glm SYSTEM INTERFACE "${glm_SOURCE_DIR}")
target_compile_definitions(dep_glm INTERFACE GLM_ENABLE_EXPERIMENTAL GLM_FORCE_SILENT_WARNINGS)
add_library(dep::glm ALIAS dep_glm)

add_library(dep_json INTERFACE)
target_include_directories(dep_json SYSTEM INTERFACE "${json_SOURCE_DIR}/single_include")
add_library(dep::json ALIAS dep_json)

add_library(dep_stb INTERFACE)
target_include_directories(dep_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
add_library(dep::stb ALIAS dep_stb)

add_library(dep_nanosvg INTERFACE)
target_include_directories(dep_nanosvg SYSTEM INTERFACE "${nanosvg_SOURCE_DIR}/src")
add_library(dep::nanosvg ALIAS dep_nanosvg)

add_library(dep_cgltf INTERFACE)
target_include_directories(dep_cgltf SYSTEM INTERFACE "${cgltf_SOURCE_DIR}")
add_library(dep::cgltf ALIAS dep_cgltf)

add_library(dep_pfd INTERFACE)
target_include_directories(dep_pfd SYSTEM INTERFACE "${pfd_SOURCE_DIR}")
add_library(dep::pfd ALIAS dep_pfd)

if(DMXVIZ_BUILD_TESTS)
    add_library(dep_doctest INTERFACE)
    target_include_directories(dep_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
    add_library(dep::doctest ALIAS dep_doctest)
endif()

# --- compiled -----------------------------------------------------------------
# Implementations of stb / nanosvg / cgltf. Built without the project's strict
# warning flags; first-party code only includes their headers.
add_library(dep_impls STATIC "${_tp}/single_header_impls.c")
target_link_libraries(dep_impls PUBLIC dep_stb dep_nanosvg dep_cgltf)
if(MSVC)
    target_compile_definitions(dep_impls PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_library(dep::impls ALIAS dep_impls)

add_library(dep_pugixml STATIC "${pugixml_SOURCE_DIR}/src/pugixml.cpp")
target_include_directories(dep_pugixml SYSTEM PUBLIC "${pugixml_SOURCE_DIR}/src")
add_library(dep::pugixml ALIAS dep_pugixml)

add_library(dep_miniz STATIC
    "${miniz_SOURCE_DIR}/miniz.c"
    "${miniz_SOURCE_DIR}/miniz_tdef.c"
    "${miniz_SOURCE_DIR}/miniz_tinfl.c"
    "${miniz_SOURCE_DIR}/miniz_zip.c")
target_include_directories(dep_miniz SYSTEM PUBLIC "${miniz_SOURCE_DIR}" "${_tp}/miniz")
add_library(dep::miniz ALIAS dep_miniz)

if(DMXVIZ_BUILD_APP)
    add_library(dep_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_demo.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
        "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
    target_include_directories(dep_imgui SYSTEM PUBLIC
        "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/misc/cpp" "${imguizmo_SOURCE_DIR}/src")
    add_library(dep::imgui ALIAS dep_imgui)

    # sokol: one translation unit holds all implementations (third_party/sokol_impl.cpp).
    # Backend: OpenGL 4.3 core on Windows and Linux (see ADR 0001).
    add_library(dep_sokol STATIC "${_tp}/sokol_impl.cpp")
    target_include_directories(dep_sokol SYSTEM PUBLIC "${sokol_SOURCE_DIR}" "${sokol_SOURCE_DIR}/util")
    target_compile_definitions(dep_sokol PUBLIC SOKOL_GLCORE)
    target_link_libraries(dep_sokol PUBLIC dep_imgui)
    if(UNIX AND NOT APPLE)
        find_package(OpenGL REQUIRED)
        find_package(X11 REQUIRED)
        find_package(Threads REQUIRED)
        target_link_libraries(dep_sokol PUBLIC
            OpenGL::GL X11::X11 X11::Xi X11::Xcursor Threads::Threads ${CMAKE_DL_LIBS} m)
    elseif(WIN32)
        target_link_libraries(dep_sokol PUBLIC opengl32 kernel32 user32 gdi32 shell32)
    endif()
    add_library(dep::sokol ALIAS dep_sokol)
endif()
