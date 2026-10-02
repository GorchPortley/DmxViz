# DmxViz – engineering guide

Read `docs/ARCHITECTURE.md` before changing code. Requirements: `docs/REQUIREMENTS.md`.

## Build and test

```bash
# configure (DMXVIZ_DEPS_DIR is optional: a directory of pre-cloned deps, avoids network fetches)
cmake -S . -B build -G Ninja -DDMXVIZ_WARNINGS_AS_ERRORS=ON [-DDMXVIZ_DEPS_DIR=/home/user/dmxviz-deps]
cmake --build build
./build/tests/dmxviz_tests                      # unit tests (doctest; -tc="name*" filters)

# headless app screenshot (Linux, needs xvfb + mesa)
xvfb-run -a -s "-screen 0 1600x900x24" ./build/src/app/dmxviz --screenshot shot.png
```

Linux packages: `libgl-dev libx11-dev libxi-dev libxcursor-dev xvfb`.

## Ownership and contracts

| Path | Owner |
|------|-------|
| `src/core/`, `src/assets/` (except model loaders), `src/app/`, CMake, CI, docs | integration |
| `src/dmx/` | WS1 DMX |
| `src/fixtures/`, `data/fixtures/`, `docs/FIXTURE_FORMAT.md` | WS2 Fixtures |
| `src/render/`, `tools/render_sandbox/` | WS3 Render |
| `src/stage/`, `src/assets/ModelLoader.*` | WS4 Stage |
| `src/ui/` | WS5 UI |

* Stay inside your module. Tests go in `tests/<module>/`, test data in `tests/data/<module>/`.
* **Contract headers** (`src/core/*`, `src/assets/AssetLibrary.h`, `src/assets/MeshData.h`,
  `src/render/RenderScene.h`) are shared. Do not change them unilaterally. If you need a
  change, describe it in your hand-off notes; the integration owner applies it.
* Respect the dependency rules in ARCHITECTURE.md §1 (e.g. `fixtures` must not include `dmx/`).

## Code style

* C++20. `.clang-format` is authoritative (4-space indent, 120 columns).
* Namespaces: `dmxviz::<module>` (`dmxviz` for `core`).
* Types `PascalCase`, functions and variables `camelCase`, members `trailingUnderscore_`,
  constants `kPascalCase`, files `PascalCase.h/.cpp`.
* Prefer value types and `std::unique_ptr`; no raw `new`/`delete`. No exceptions across module
  boundaries: return `std::optional`/`bool` + error string.
* Comments explain *why*, not *what*. The owner is a hobbyist C++ developer: favour clear code
  over clever code, small classes with one job, and a short header comment on every class.
* No per-frame heap allocation in hot paths (renderer, fixture runtime, DMX IO).
* Log with `dmxviz::log::info/warn/error("<module>", "fmt {}", args)`.
* The build must stay warning-free with `-DDMXVIZ_WARNINGS_AS_ERRORS=ON` (GCC and MSVC).
  Code must compile on Windows (MSVC) too: guard platform code with `#ifdef _WIN32`,
  include `<winsock2.h>` before `<windows.h>`, define `NOMINMAX` and `WIN32_LEAN_AND_MEAN`.

## sokol notes

The pinned sokol uses the **views API** (`sg_view`, `sg_make_view`, `sg_pass.attachments`
with views, `sg_bindings.views[]`). Older tutorials are wrong for it: read
`<build>/_deps/sokol-src/sokol_gfx.h` (or `$DMXVIZ_DEPS_DIR/sokol/sokol_gfx.h`) for the real API.

## Commits

Small, focused commits with imperative subject lines, e.g. `dmx: add sACN receiver`.
Reference requirement ids (FR-DMX-2) in the body when relevant.
