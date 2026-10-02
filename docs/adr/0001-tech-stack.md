# ADR 0001 – Technology stack

**Status**: accepted (2026-10-02)

## Context
DmxViz must render hundreds of volumetric beams at 60 fps, drive USB and
network DMX hardware, run on Windows and Linux, and stay hackable by a hobbyist
C++ developer. Only open-source libraries may be used, and beam rendering should
use light, efficient libraries.

## Decision
| Concern | Choice | Why |
|---------|--------|-----|
| Language | C++20 | Owner's language; zero-overhead; std::format, spans, concepts |
| Window + input | sokol_app | single header, no GLFW/SDL dependency |
| GPU API | sokol_gfx, OpenGL 4.3 core backend | thin (~1 header), instancing, MRT, texture arrays, storage buffers, compute |
| UI | Dear ImGui (docking) + sokol_imgui | immediate-mode editor UI with dockable panels |
| Gizmos | ImGuizmo | standard translate/rotate/scale gizmo for ImGui |
| Math | glm | familiar, header-only |
| JSON | nlohmann/json | readable API for fixture/project files |
| XML (GDTF) | pugixml | small and fast |
| Zip (GDTF) | miniz | single library, no zlib dependency |
| glTF | cgltf | single header |
| Images | stb_image / stb_image_write, nanosvg | single headers, PNG/JPG/SVG gobos |
| File dialogs | portable-file-dialogs | single header, native dialogs |
| Tests | doctest | fastest-compiling C++ test framework |
| Networking / serial | own thin wrappers over BSD sockets, Winsock, termios, Win32 | the protocols are small; no Asio/Boost needed |

Rejected: bgfx (heavier, own build system and shader compiler), Vulkan directly
(far too much code), Qt (heavy, LGPL), Three.js (browser cannot talk to USB DMX
without a helper, less efficient).

## Consequences
* One shader language (GLSL 430) for both platforms; no shader cross-compiler step.
* macOS (GL 4.1 only) and D3D11 would need sokol-shdc; deferred.
* Dependencies are fetched by git at configure time and pinned to exact tags/commits.
