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
| WS2 Fixtures | FixtureType model, native format + spec, library, OFL + GDTF import, runtime decode + physics, starter library, tests | ✅ (7 more starter fixtures pending) |
| WS3 Render | G-buffer, spot lighting with gobos, volumetric beams, glow, bloom, tonemap, gobo atlas, render sandbox | ✅ |
| WS4 Stage | Scene graph, commands/undo, truss + deck builders, model loaders, picking, project files, tests | ✅ |

## Milestone 2 – Integration & UI
- [ ] Simulation glue (stage + fixtures + DMX → RenderScene)
- [ ] Viewport camera, selection, gizmos
- [ ] Outliner, inspector, stage builder tools
- [ ] Fixture library browser + fixture editor
- [ ] Patch, DMX interfaces, DMX monitor, test console, environment, log panels
- [ ] Demo show in `data/shows/`

## Milestone 3 – Hardening
- [ ] Performance pass against NFR-1 (profiling, half-res tuning)
- [ ] Code review and refactor pass
- [ ] User guide

## Known issues
- Prism facet x axis: fixtures write facet x to beam-local +X, the renderer's "right" is direction × up = local −X.
  Asymmetric prism patterns render mirrored. Fix in the fixtures module (negate x) with a test.
- GDTF: only the first DMX break is imported; wheel shake/half-slot positions not modelled.
- `ReparentCommand` may misplace sibling order when several dragged siblings share a parent (outliner drag).
- Render quality settings are saved in the project (`environment.quality`) but are per-machine; move them to a
  local settings file.
- "Hang on truss → spread evenly" uses the truss bounding-box axis; approximate on arcs and circles.
- No unit tests for app/ui code (Simulation, ProjectIO): the test binary does not link `ui`/`app`.

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
