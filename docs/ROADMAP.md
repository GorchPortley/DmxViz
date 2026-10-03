# Roadmap

Development is organised as parallel **work streams** against the module
contracts in [ARCHITECTURE.md](ARCHITECTURE.md), merged by the integration
owner after review.

## Milestone 0 – Foundation ✅
- [x] Requirements and architecture agreed
- [x] CMake project, pinned dependencies, module skeleton, warnings-as-errors
- [x] Shared contracts: `core/SceneTypes.h`, `assets/*`, `render/RenderScene.h`
- [x] App shell: sokol window, ImGui docking, headless screenshot mode
- [x] Unit test harness, CI for Linux + Windows

## Milestone 1 – Subsystems (parallel)
| Stream | Scope | Status |
|--------|-------|--------|
| WS1 DMX | UniverseStore + merge, Art-Net, sACN, Enttec Pro, Open DMX, loopback, serial/NIC enumeration, DmxManager, tests | ✅ |
| WS2 Fixtures | FixtureType model, native format + spec, library, OFL + GDTF import, runtime decode + physics, starter library, tests | ✅ |
| WS3 Render | G-buffer, spot lighting with gobos, volumetric beams, glow, bloom, tonemap, gobo atlas, render sandbox | ✅ |
| WS4 Stage | Scene graph, commands/undo, truss + deck builders, model loaders, picking, project files, tests | ✅ |

## Milestone 2 – Integration & UI
- [x] Simulation glue (stage + fixtures + DMX → RenderScene)
- [x] Viewport camera, selection, gizmos
- [x] Outliner, inspector, stage builder tools
- [x] Fixture library browser + fixture editor
- [x] Patch, DMX interfaces, DMX monitor, test console, environment, log panels
- [x] Demo show in `data/shows/`

## Milestone 3 – Hardening
- [ ] Performance pass against NFR-1 (profiling, half-res tuning)
- [ ] Code review and refactor pass
- [ ] User guide

## Known issues
- GDTF: only the first DMX break is imported; wheel shake/half-slot positions not modelled.
- No unit tests for `Simulation` and `App`: the app is an executable, so only its sokol-free files (ProjectIO,
  UserSettings) are compiled into the test binary.

## Process notes
Agents are tiered to stay within usage limits: the integration owner (Opus) plans, reviews and merges;
implementation agents run on Sonnet; mechanical content (fixture files, artwork, docs) on Haiku. At most
two agents run concurrently, each with a narrow brief, and briefs require filtered build/test output.

## Later
- Beam shadows (shadow-map atlas)
- LED screens / video textures
- MVR (My Virtual Rig) scene import
- D3D11 backend via sokol-shdc
- Multiple universes per fixture (DMX breaks)
