# DmxViz Architecture

DmxViz is a real-time 3D visualizer for DMX512 lighting. A lighting console (or
the built-in test console) sends DMX over Art-Net, sACN or a USB interface; DmxViz
decodes it through fixture definitions and renders the rig, with volumetric
beams in haze, onto a stage you build in the app.

Requirements live in [REQUIREMENTS.md](REQUIREMENTS.md), the plan in
[ROADMAP.md](ROADMAP.md), and decisions in [adr/](adr/).

---

## 1. Module map

```
                 ┌──────────────────────────── app ────────────────────────────┐
                 │  App (frame loop), Simulation (stage + fixtures + DMX glue)  │
                 └───────┬───────────────┬───────────────┬──────────────┬───────┘
                         │               │               │              │
                        ui ──────────────┼───────────────┼──────────────┤
                         │               │               │              │
                       render          stage            dmx             │
                         │               │                              │
                         │           fixtures                           │
                         │               │                              │
                         └──── assets ───┘                              │
                                  │                                     │
                                 core ──────────────────────────────────┘
```

| Module | Library | Responsibility | Work stream |
|--------|---------|----------------|-------------|
| `core` | `dmxviz_core` | math, ids, logging, **cross-module frame types** (`SceneTypes.h`) | integration |
| `assets` | `dmxviz_assets` | CPU meshes/images, `AssetLibrary`, primitives, image + model loaders | integration / WS4 (model loaders) |
| `dmx` | `dmxviz_dmx` | universes, merging, Art-Net, sACN, USB interfaces, DMX output | WS1 |
| `fixtures` | `dmxviz_fixtures` | fixture type model, native format, OFL + GDTF import, library, runtime decoding + physics | WS2 |
| `render` | `dmxviz_render` | sokol_gfx renderer: surfaces, spot lighting, volumetric beams, post | WS3 |
| `stage` | `dmxviz_stage` | scene graph, undo/redo commands, truss + set builders, picking, project files | WS4 |
| `ui` | `dmxviz_ui` | Dear ImGui panels and editors | WS5 |
| `app` | `dmxviz` exe | sokol_app entry, frame loop, wiring | integration |

**Dependency rules** (enforced by CMake target links):

* `core` depends on nothing but glm / nlohmann_json.
* `dmx` depends only on `core`. It knows nothing about fixtures.
* `fixtures` depends on `core` + `assets`. It does **not** depend on `dmx`: the
  runtime receives raw channel bytes (`std::span<const uint8_t>`).
* `render` depends on `core` + `assets` + sokol. It never sees the scene graph;
  it draws a flat `RenderScene`.
* `stage` depends on `core` + `assets` + `fixtures`.
* Only `ui` and `app` see everything.

Headers are included as `"<module>/<File>.h"`. Sources are globbed per module,
so adding a file needs no CMake edit.

---

## 2. Conventions

* **Coordinates** (ADR 0002): right-handed, **Y up**, metres, radians. Floor at
  `y = 0`, audience toward `+Z`.
* **Fixture local space**: Y up; a fixture is defined *hanging* (base at the
  top) and its beam leaves each Beam geometry along **local −Y** at pan = tilt = 0.
  Local **+Z** of the beam geometry is the "up" reference for gobos, prisms and
  framing shutters. GDTF (Z up, mm) and OFL are converted at import.
* **Colour**: all colours in memory are *linear* RGB. Convert from sRGB at the
  edges (colour pickers, image files).
* **Angles in files**: degrees (human-editable); in memory: radians.
* **Errors**: functions that can fail on user data return `std::optional<T>` or
  `bool` plus an error string. No exceptions across module boundaries
  (nlohmann_json exceptions are caught inside the module that parses).

---

## 3. Frame loop and threading

```
IO threads (one per network socket / serial port)
    └─ parse packets ─► dmx::UniverseStore::submit()      (mutex per store, short critical section)

Main thread, every frame:
    1. dmx::DmxManager::snapshot()      copy merged universes (≈ 512 B × universes)
    2. Simulation::update(dt, time)     for each fixture node:
                                          runtime.setDmx(footprint bytes)
                                          runtime.update(dt, time)        (pan/tilt physics, strobe phase, wheel spin)
                                          runtime.emit(world, meshes, beams)
    3. stage emits static MeshInstances (cached; rebuilt on scene change)
    4. ui builds panels; each 3D viewport calls Renderer::render(target, camera, renderScene)
    5. swapchain pass: ImGui; sg_commit
    6. DMX output (test console → interfaces) is queued to the IO threads
```

Nothing but the main thread touches sokol, ImGui, the scene, or the asset
library. IO threads only ever touch `UniverseStore` and their own sockets.

Performance budget (NFR-1): 60 fps on a mid-range GPU (GTX 1060 / RX 580 /
Iris Xe at 1080p with half-res beams) with **500 fixtures, 1000 beams, 64
universes**. CPU simulation must stay below 2 ms for that rig.

---

## 4. DMX (`src/dmx`, WS1)

### Data model
* `UniverseId` is the logical, 1-based universe the user sees.
* `UniverseStore` keeps, per universe, the latest frame from every **source**
  (an interface + remote sender + protocol universe). Merge rule:
  highest sACN-style priority wins (Art-Net/USB = 100 by default); sources at
  equal priority merge **HTP**. A source times out after 2.5 s without data
  (E1.31 "network data loss"), or immediately on an sACN stream-terminated flag.
* A **local programmer** source (the test console) can be set to `Merge` (HTP
  with inputs) or `Override` (wins at priority 201).
* `DmxSnapshot` is the main-thread copy: `const UniverseData* universe(UniverseId)`,
  `uint8_t channel(UniverseId, uint16_t address1Based)`, plus per-universe
  last-update time and source list for the DMX monitor.

### Interfaces
```cpp
class DmxInterface {                // one configured port/protocol
    virtual std::string typeName() const;   // "Art-Net", "sACN", "Enttec DMX USB Pro", ...
    virtual Capabilities caps() const;      // input / output / both
    virtual bool start(std::string& error); // opens sockets/ports, spawns IO thread
    virtual void stop();
    virtual InterfaceStatus status() const; // state, packets/s, errors, remote senders seen
    virtual void send(UniverseId, const UniverseData&); // output, non-blocking (queued)
    virtual nlohmann::json saveConfig() const;  virtual bool loadConfig(const nlohmann::json&);
};
```
`DmxManager` owns the store, a registry/factory of interface types, the list
of configured interfaces, and output routing (which universes go out where,
at a fixed refresh, default 40 Hz, only when changed + 1 s keep-alive).

Required protocols and devices:

| Interface | In | Out | Notes |
|-----------|----|-----|-------|
| Art-Net 4 | ✔ | ✔ | UDP 6454, ArtDmx, ArtPoll → ArtPollReply (so consoles discover us), ArtSync tolerated; bind to chosen NIC; port-address → universe offset |
| sACN / E1.31 | ✔ | ✔ | UDP 5568, multicast join per subscribed universe on chosen NIC, priority, sequence numbers, preview + terminated flags, universe discovery optional |
| Enttec DMX USB Pro (and compatibles: DMXking ultraDMX, etc.) | ✔ | ✔ | serial widget protocol: label 6 output, label 5 / 9 input, label 8 "receive on change" off |
| Enttec Open DMX USB (FTDI) | – | ✔ | raw serial at 250 kbaud with break; best-effort timing |
| Loopback | ✔ | ✔ | for tests and the test console |

Serial ports: thin wrapper (`termios` on Linux incl. non-standard 250000 baud
via `termios2`/`BOTHER`; Win32 `CreateFile` + `DCB` on Windows) plus port
enumeration (`/dev/serial/by-id`, `/dev/ttyUSB*`, `/dev/ttyACM*`; Windows
SetupAPI or `QueryDosDevice`). Network: thin socket wrapper over BSD sockets /
Winsock, NIC enumeration (`getifaddrs` / `GetAdaptersAddresses`).

---

## 5. Fixtures (`src/fixtures`, WS2)

The fixture model follows **GDTF concepts** so that GDTF imports are lossless
for what we render, and OFL maps onto a subset.

### 5.1 FixtureType
```
FixtureType
├── id                    "manufacturer/model" slug, unique in the library
├── manufacturer, name, shortName, description, source (native|ofl|gdtf), revision
├── physical             weight, power, dimensions
├── wheels[]             Wheel{ name, slots[] }  slot = Open | Color{rgb} | Gobo{image} |
│                                                 Prism{facets[]} | AnimationWheel{image} | Frost
├── emitters[]           optional named emitters (LED colours with their RGB / dominant wavelength)
├── geometry             tree of Geometry nodes (see 5.2)
└── modes[]              DmxMode{ name, footprint, channels[] } (see 5.3)
```

### 5.2 Geometry
A tree; each node has a name, local transform (position + rotation, metres),
an optional **model** (built-in primitive with dimensions, or a mesh asset from
a GDTF/glTF file), and a type:

* `Generic` – static body part.
* `Axis` – rotates around a local axis when driven by an attribute (`Pan`
  rotates about local Y, `Tilt` about local X). Range and max speed come from
  the channel's physical range and the fixture's movement spec.
* `Beam` – a light source. Parameters: `beamType` (Spot/Wash/Beam/PC/Fresnel/
  Rectangle/Glow → `BeamShape`), `lensRadius`, `beamAngle`, `fieldAngle`,
  `luminousFlux` (lm), `colorTemperature` (K), `emitterSize` for rectangles,
  and the default colour of a white source.
* `Cell` / repeated instances – multi-pixel fixtures (LED bars, matrices) are
  expressed as several Beam geometries; GDTF `GeometryReference`s are expanded
  at import.

### 5.3 Modes, channels, channel functions
```
DmxMode { name, footprint, channels[] }
Channel {
    offsets[]         1-based offsets within the mode (coarse, fine, ultra...) → 8/16/24 bit
    geometry          name of the geometry it controls (cell index for multi-cell)
    defaultValue, highlightValue
    functions[]       ChannelFunction (contiguous DMX ranges covering the channel)
}
ChannelFunction {
    attribute         canonical attribute (below)
    dmxFrom, dmxTo    in channel resolution
    physicalFrom, physicalTo     units depend on attribute (degrees, Hz, %, K, ...)
    wheel, slotFrom, slotTo      for wheel functions (indexing, half-slots → split colour)
    kind              Linear | WheelIndex | WheelSpin (CW/CCW speed) | Strobe | Random | Pulse... | NoFeature
}
```

Canonical attributes (an `enum class Attribute` with GDTF names as the string
form): `Dimmer`, `Pan`, `Tilt`, `PanRotate`, `TiltRotate`, `PositionEffect`
(ignored), `ColorAdd_R/G/B/W/WW/CW/A/UV/Lime/Cyan/Magenta/Yellow/Indigo`,
`ColorSub_C/M/Y`, `CTO`, `CTC`, `CTB`, `Color1`, `Color2`, `Color1WheelSpin`...,
`Gobo1`, `Gobo1Pos`, `Gobo1PosRotate`, `Gobo2`..., `AnimationWheel1`,
`AnimationWheel1Pos`..., `Prism1`, `Prism1Pos`, `Prism1PosRotate`, `Zoom`,
`Focus1`, `Iris`, `Frost1`, `Shutter1`, `Shutter1Strobe`, `Shutter1StrobeRandom`,
`Shutter1StrobePulse`..., `Blade1A..4A`, `Blade1B..4B`, `Blade1Rot..4Rot`,
`ShaperRot`, `Control`, `Effects`, `NoFeature`, plus `Unknown` for preserved but
unrendered attributes.

### 5.4 FixtureRuntime (decode + physics)
```cpp
class FixtureRuntime {
    FixtureRuntime(const FixtureType&, const DmxMode&);
    void setDmx(std::span<const uint8_t> footprint);   // raw bytes of this fixture's footprint
    void update(float dt, double timeSeconds);         // physics, strobe, wheel spin integration
    void emit(const glm::mat4& world, NodeId owner,
              std::vector<MeshInstance>& meshes, std::vector<BeamState>& beams) const;
    // UI: current decoded value of every attribute (for the test console and inspector)
    AttributeValues values() const;
};
```
Rules the runtime must implement:
* 8/16/24-bit channels; channel-function lookup by DMX range; physical value
  interpolation.
* **Colour**: additive emitters sum weighted emitter colours; CMY multiplies
  `1 - C/M/Y`; colour wheel slots multiply (half-slot positions split → nearest
  slot for now); CTO/CTB shift white point; result normalised so the max
  component ≤ 1 with the lost energy folded into `intensity`.
* **Dimmer curve** (linear by default, optional square-law), shutter
  open/closed, strobe with frequency + duty (pulse, random, ramp variants).
* **Movement**: pan/tilt follow DMX targets with per-fixture max speed and
  acceleration (defaults ≈ 540°/2.5 s pan); continuous rotation for `PanRotate`.
* **Gobo/prism/animation**: indexed position or continuous spin integrated
  over time; wheel rotation between slots (wheel moves, it does not snap) at
  wheel speed.
* **Zoom/iris/frost/focus** → `beamAngle`, `fieldAngle`, `iris`, `frost`, `focus`.
* Emits `MeshInstance`s for every geometry node with a model (emissive lens
  face/LED pixel material driven by the beam colour × intensity) and one
  `BeamState` per Beam geometry.

### 5.5 Native format and library
* Native fixture file: `*.dmxviz-fixture.json` (schema documented in
  `docs/FIXTURE_FORMAT.md`, versioned with `"formatVersion"`).
* `FixtureLibrary`: loads the bundled `data/fixtures/` directory plus user
  directories; `importFile()` dispatches on content/extension:
  native JSON, OFL JSON (`$schema` contains `open-fixture-library`), `.gdtf`
  (zip: `description.xml` + `models/` + `wheels/`).
* Imported fixtures can be saved as native files, so the fixture editor works
  on one model regardless of source.
* Bundled starter library (native files): generic dimmer, RGB par, RGBW LED par,
  LED wash moving head with zoom, profile spot moving head (2 gobo wheels, prism,
  iris, frost, framing), beam moving head (Sharpy-like), 8-cell RGBW pixel bar,
  LED strobe/blinder (rectangular cells).

---

## 6. Stage (`src/stage`, WS4)

* **Scene graph**: `Scene` owns `Node`s (id, name, parent, children, local
  transform as position + quaternion + scale, visibility, lock, layer). Node
  kinds: `Group`, `Primitive` (box/cylinder/sphere/plane/cone + material),
  `Model` (imported glTF/OBJ/3DS asset), `Truss` (generated), `StageDeck`,
  `Fixture` (type id, mode name, DMX patch `{universe, address}`, fixture id
  number, pan/tilt invert + offset), `Camera` preset, `LedScreen` (later).
* **Commands**: every edit goes through `CommandStack` (`Command{ apply(), revert(), name, merge() }`)
  for undo/redo; gizmo drags merge into a single command.
* **Builders**: truss generator (box/triangle/ladder/flat profiles, straight
  runs with segment lengths, corners, circles/arcs, towers/ground support with
  base plates), stage decks with legs, risers, steps, walls/flats, floor plane.
  Generated geometry is cached by parameters in the `AssetLibrary`.
* **Placement tools**: snapping (grid, angle, vertex/face), *hang on truss*
  (fixture snaps to a truss chord and orients hanging), linear / grid / circular
  arrays, align & distribute, mirror.
* **Picking**: CPU ray casting (BVH over mesh triangles in world space, AABB
  early-out) returning node id + hit point + normal.
* **Project file** (`*.dmxviz` JSON): scene, patch, environment, camera
  presets, DMX interface config, embedded or referenced fixture types,
  referenced model files (relative paths).

---

## 7. Rendering (`src/render`, WS3)

Backend: sokol_gfx on **OpenGL 4.3 core** (Windows + Linux), GLSL `#version 430`
shaders kept in `src/render/shaders/*.glsl` and embedded at build time. Read
the sokol headers in the build tree: the API is the post-2025 *views* API
(`sg_view`, `sg_make_view`), which differs from older tutorials.

### Pipeline (per viewport)
1. **G-buffer** (full res): albedo+roughness (RGBA8), normal+metallic
   (RGBA16F or octahedral RG16), depth (D32F). Instanced draws, one per mesh id.
2. **Spot lighting** (full res, additive into HDR RGBA16F): one instanced draw
   of bounding cones for all beams (prism facets expand to extra instances).
   Fragment reconstructs the position from depth, applies the beam's angular
   profile (beam/field angle, soft edge from focus/frost), gobo projection
   (sampled from a gobo **texture array**, rotated, blurred via mip bias by
   focus/frost), iris, framing blades, inverse-square falloff, Lambert + a
   simple GGX specular. Ambient + emissive added in a full-screen pass.
3. **Volumetric beams** (half res, additive): the same instanced cone hulls.
   Fragment intersects the view ray with the beam cone analytically, clips the
   segment against scene depth (from a half-res depth min/max buffer), and
   ray-marches 12–24 jittered steps through it, sampling the gobo projection,
   haze density (× optional 3D noise for drifting haze), inverse-square falloff
   and a Henyey–Greenstein phase function (bright when looking into the beam).
   Bilateral, depth-aware upsample into the HDR target.
4. **Lens glow / flares**: camera-facing sprites at each lens, scaled by how
   directly the camera looks into the beam; depth-tested softly.
5. **Bloom**: 6-level dual-filter (13-tap down / tent up) on the HDR target.
6. **Tonemap** (ACES fit or AgX) + exposure + dithering into an RGBA8 target that
   the UI shows. Grid, selection outlines and gizmo debug lines are drawn here.

### Rules
* One draw call per pass for beams regardless of count; mesh draws batched by
  mesh id with per-instance matrices from a storage/vertex buffer.
* No per-frame allocation in steady state: grow-only GPU buffers, reuse vectors.
* Gobo images are uploaded once to a `GoboAtlas` (256×256 array layers with
  mips); `ImageId` → layer mapping maintained via `AssetLibrary` revisions.
* Beam shadows (beams cut by truss/scenery) are **phase 2**: a shadow-map atlas
  for the N most important beams.
* `tools/render_sandbox` renders a stress scene headless and writes a PNG; it
  is the render work stream's test bed and benchmark.

---

## 8. UI (`src/ui`, WS5)

Dear ImGui (docking branch) + ImGuizmo for transform gizmos. Panels:

| Panel | Purpose |
|-------|---------|
| Viewport(s) | 3D view, orbit/pan/fly camera, selection, gizmo, drag-drop fixtures from the library |
| Outliner | scene tree, groups, visibility/lock, rename, reparent |
| Inspector | properties of the selection (transform, primitive params, truss params, fixture patch) |
| Stage builder toolbar | add primitives, truss, decks, models; array/align/hang tools |
| Fixture library | browse/search, import OFL/GDTF, drag into scene |
| Fixture editor | edit/create fixture types: geometry tree, beams, wheels + gobo images, modes, channels, functions |
| Patch | spreadsheet of fixtures: id, name, type, mode, universe.address; conflict detection; auto-patch |
| DMX interfaces | add/configure Art-Net, sACN, USB devices; status and packet rates |
| DMX monitor | per-universe 512-cell grid with live values and sources |
| Test console | per-fixture attribute faders (intensity, colour picker, pan/tilt pad, gobo/prism selectors), raw channel faders, output enable |
| Environment | haze, ambient, exposure, bloom, background |
| Log | `log::recent()` |

File dialogs use portable-file-dialogs. Keyboard shortcuts follow common DCC
conventions (W/E/R gizmo modes, F frame selection, Ctrl+Z/Y, Del, Ctrl+D).

---

## 9. Testing

* **Unit tests** (`tests/`, doctest, no GPU): DMX packet encode/decode, merge
  and timeout logic, loopback UDP, fixture decoding, colour mixing, OFL/GDTF
  import of sample files in `tests/data/`, scene commands + undo, project
  round-trips, picking, primitives.
* **Render tests**: `tools/render_sandbox --screenshot` under Xvfb (Mesa
  llvmpipe) in CI; reviewers look at the PNGs.
* **App smoke test**: `dmxviz --screenshot out.png` under Xvfb.
* CI (GitHub Actions) builds Linux (GCC) and Windows (MSVC) with warnings as
  errors and runs the unit tests.
