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
- [ ] Performance pass against NFR-1 (profiling, half-res tuning): CPU side done, GPU side still to be measured on
  real hardware
- [x] Performance: `tools/sim_bench` (500 fixtures / 1200 beams / 64 universes: simulation about 0.9 ms and 0 heap
  allocations per frame, CI step), decode skipped for unchanged DMX, haze cost budget, quarter-resolution haze and
  automatic quality (`src/render/README.md`, section "Performance")
- [x] Correctness review: sanitizer + mutation pass (see Robustness below)
- [ ] Readability review and refactor pass (code clarity for hobbyist contributors)
- [x] User guide (`docs/USER_GUIDE.md`)
- [x] Robustness: ASan/UBSan and TSan clean (CI jobs `linux-sanitizers`, `linux-tsan`); deterministic
  mutation tests for every parser (`tests/robustness/`: Art-Net, sACN, Enttec, native/OFL/GDTF fixtures,
  glTF/OBJ/3DS, PNG/SVG, projects, clipboard, DMX config, settings); thread stress tests for the DMX module;
  hard limits on untrusted input in `src/core/Limits.h` (file sizes, zip entries, image and model sizes,
  node and JSON depth, source counts)

## Known issues
- GDTF: only the first DMX break is imported; wheel shake/half-slot positions not modelled.
- Robustness gaps: the mutation tests are short, deterministic seeds, not a coverage-guided fuzzer (libFuzzer
  targets for the parsers would be the next step). UBSan's `float-cast-overflow` is not enabled in CI (it also
  fires inside nlohmann::json), so JSON-number to int/float conversions are only guarded where the code was
  reviewed (OFL importer, DMX settings); the GDTF and native readers rely on their validation.
- The serial paths (`SerialPortLinux/Win32`, Enttec/Open DMX IO threads) have no sanitizer coverage beyond the
  stream parser: no hardware in CI. TSan covers the DMX module only.
- A flood of spoofed Art-Net/sACN senders is bounded (64 sources per universe, 4096 overall) but can still lock
  out a new legitimate source until the spoofed ones time out; there is no sender authentication.
- Recursion limits (128 nodes / geometry levels) were checked on Linux (8 MB stack); the Windows 1 MB main
  thread stack has not been tried with a maximally deep file.
- The size limits are ceilings, not promises: a model near the 50M-triangle limit needs several GB of RAM.
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
