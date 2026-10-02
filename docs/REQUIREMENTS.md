# Requirements

Scope agreed with the product owner on 2026-10-02:

* **Stack**: C++20 native, sokol + Dear ImGui, open-source dependencies only.
* **Role**: visualizer driven by external DMX, plus a built-in test console. Not a full lighting console (no cues/timeline).
* **Fixture formats**: native format, Open Fixture Library import, GDTF import.
* **Platforms**: Windows and Linux.

IDs are referenced from commits, tests and the roadmap.

## Functional

### DMX input/output
| ID | Requirement |
|----|-------------|
| FR-DMX-1 | Receive Art-Net (ArtDmx) on a selectable network interface; reply to ArtPoll so consoles discover DmxViz. |
| FR-DMX-2 | Receive sACN (E1.31) multicast and unicast, honouring priority, sequence numbers and stream termination. |
| FR-DMX-3 | Receive and send DMX through Enttec DMX USB Pro–compatible widgets. |
| FR-DMX-4 | Send DMX through Enttec Open DMX USB (FTDI) interfaces. |
| FR-DMX-5 | Map protocol universes to logical universes with a per-interface offset. |
| FR-DMX-6 | Merge multiple sources per universe (priority, then HTP) with source timeout. |
| FR-DMX-7 | Send DMX (from the test console) over Art-Net, sACN and USB. |
| FR-DMX-8 | Show live per-universe channel values, active sources and packet rates. |

### Fixtures
| ID | Requirement |
|----|-------------|
| FR-FIX-1 | Define fixture types with a geometry tree (bodies, pan/tilt axes, one or many beams), DMX modes, channels and channel functions. |
| FR-FIX-2 | Support 8/16/24-bit channels and multi-function channels (DMX ranges). |
| FR-FIX-3 | Support modern features: RGB/RGBW/RGBA/RGBAL/UV additive mixing, CMY, CTO/CTB, colour wheels, 1–2 rotating gobo wheels with indexing and spin, animation wheel, prisms (circular and linear, rotating), zoom, focus, iris, frost, framing shutters, shutter/strobe variants, continuous pan/tilt, multi-cell pixel fixtures. |
| FR-FIX-4 | Create and edit fixture types in-app, including gobo images (PNG/JPG/SVG). |
| FR-FIX-5 | Import Open Fixture Library JSON fixtures. |
| FR-FIX-6 | Import GDTF fixtures, including their 3D models and wheel images. |
| FR-FIX-7 | Save any fixture type in the native format. |
| FR-FIX-8 | Ship a starter library covering common fixture categories. |
| FR-FIX-9 | Simulate physical behaviour: pan/tilt speed limits, wheel rotation between slots, strobe timing. |

### Stage builder
| ID | Requirement |
|----|-------------|
| FR-STG-1 | Add, transform, group, duplicate and delete primitives (box, cylinder, sphere, plane, cone). |
| FR-STG-2 | Generate truss (box/triangle/ladder profiles, straight, corners, arcs/circles, towers). |
| FR-STG-3 | Generate stage decks, risers, steps and walls. |
| FR-STG-4 | Import 3D models (glTF/GLB, OBJ, 3DS). |
| FR-STG-5 | Place fixtures by drag-and-drop, hang them on truss, array/align/distribute them. |
| FR-STG-6 | Gizmo editing with grid/angle snapping; multi-selection; undo/redo for every edit. |
| FR-STG-7 | Patch fixtures (universe/address) with conflict detection and auto-patch. |
| FR-STG-8 | Save/load projects; camera presets. |

### Visualization
| ID | Requirement |
|----|-------------|
| FR-VIS-1 | Volumetric beams in adjustable haze, with gobo and prism break-up visible in the beam. |
| FR-VIS-2 | Surfaces lit by every beam, with projected gobos, soft/hard edges by focus/frost/iris, framing. |
| FR-VIS-3 | Lens glow / flare when looking into fixtures; emissive LED pixels. |
| FR-VIS-4 | HDR rendering with bloom, exposure and filmic tonemapping. |
| FR-VIS-5 | Orbit, pan and fly cameras; multiple viewports. |

### Test console
| ID | Requirement |
|----|-------------|
| FR-CON-1 | Control selected fixtures by attribute (intensity, colour, position, beam). |
| FR-CON-2 | Raw channel faders per universe. |
| FR-CON-3 | Merge with or override incoming DMX; optionally output to interfaces. |

## Non-functional
| ID | Requirement |
|----|-------------|
| NFR-1 | 60 fps with 500 fixtures / 1000 beams / 64 universes on a mid-range GPU at 1080p. |
| NFR-2 | Input-to-photon latency under 2 frames from packet arrival. |
| NFR-3 | Only permissively licensed open-source dependencies (MIT, BSD, zlib, Boost, public domain). |
| NFR-4 | Builds with GCC 13+ / Clang 16+ on Linux and MSVC 2022 on Windows from one CMake project. |
| NFR-5 | Unit-tested non-GPU code; CI on both platforms. |
| NFR-6 | Code a hobbyist C++ developer can read: clear module boundaries, small classes, comments explaining *why*. |
