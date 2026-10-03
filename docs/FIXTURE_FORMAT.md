# DmxViz native fixture format

Reference for `*.dmxviz-fixture.json`, the format the fixture library reads and the fixture
editor writes. It is what Open Fixture Library (OFL) and GDTF files are converted to on
import, so every fixture, whatever its origin, can be stored, edited and shared in this form.
The code is `src/fixtures/NativeFormat.cpp`; this document describes exactly what it reads
and writes. The "Complete example" at the end is loaded by the unit tests.

* [Conventions](#conventions)
* [File layout](#file-layout) - [Resources](#resources) - [Emitters](#emitters) - [Wheels](#wheels)
* [Geometry](#geometry) - [Geometry groups](#geometry-groups)
* [Modes, channels and functions](#modes-channels-and-functions)
* [Attributes](#attributes)
* [Validation rules and error messages](#validation-rules-and-error-messages)
* [Saving](#saving)
* [Complete example](#complete-example)

## Conventions

| Topic | Rule |
|---|---|
| File name | `<anything>.dmxviz-fixture.json`, UTF-8 JSON. The library also accepts a `.json` file that has a `"formatVersion"` key. |
| Versioning | `"formatVersion": 1`. A reader refuses a larger number. Keys it does not know are ignored (and dropped when the file is saved again). |
| Names | Case-sensitive. Wheels, geometries, channels (within a mode), resources and emitters are referenced by name, so each name must be unique in its list. |
| Lengths | metres |
| Angles | **degrees** in files (radians in memory). Angular speeds: degrees per second. |
| Coordinates | Right-handed, **Y up**. A fixture is defined *hanging*: its base is at the top and the beam of a `beam` geometry leaves along local **-Y** at pan = tilt = 0. Local **+Z** of a beam geometry is the "up" reference for gobos, prisms and framing shutters. |
| Colours | `"#rrggbb"` (sRGB, what colour pickers give) or `[r, g, b]` (**linear** RGB, 0..1). The writer always writes linear arrays. |
| DMX values | Integers in the *resolution of their channel*: 0..255 for one offset, 0..65535 for two, 0..16777215 for three. |
| Wheel slots | 1-based positions (slot 1 is the first slot of the wheel). Fractional values lie between two slots. |
| Key order | The writer follows the order of this document so files read top-down: identity, physical data, emitters, wheels, geometry, groups, modes, resources last (embedded base64 is long). |

`dimmer`, `shutter`, `strobe`, ... are not special in the file: everything a channel does is
expressed with *attributes* (see [Attributes](#attributes)), which follow GDTF naming.

## File layout

Top-level object. Required keys are marked **req**.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `formatVersion` | integer **req** | - | `1` |
| `id` | string **req** | - | Unique id in the library, by convention `"manufacturer-slug/model-slug"` (`"clay-paky/sharpy"`). Loading a second file with the same id replaces the first. |
| `manufacturer` | string | `""` | Display name, `"Clay Paky"`. |
| `name` | string **req** | - | Model name, `"Sharpy"`. |
| `shortName` | string | `""` | Short label for tight spaces. |
| `description` | string | `""` | Free text. |
| `source` | `"native"` \| `"ofl"` \| `"gdtf"` | `"native"` | Where the data came from. Informational. |
| `revision` | string | `""` | Free text version of the definition. |
| `categories` | string[] | `[]` | OFL vocabulary: `"Moving Head"`, `"Color Changer"`, `"Pixel Bar"`, ... |
| `physical` | object | see below | Weight, power, dimensions, dimmer curve and movement limits. |
| `emitters` | object[] | `[]` | Named LED colours, see [Emitters](#emitters). |
| `wheels` | object[] | `[]` | Colour / gobo / prism / animation wheels, see [Wheels](#wheels). |
| `geometry` | object **req** | - | Root of the geometry tree, see [Geometry](#geometry). |
| `geometryGroups` | object[] | `[]` | Named sets of geometries a channel can control together. |
| `modes` | object[] **req** | - | DMX modes, at least one. See [Modes, channels and functions](#modes-channels-and-functions). |
| `resources` | object[] | `[]` | Gobo images and 3D model files, see [Resources](#resources). |

### `physical`

All keys optional; the whole object may be left out.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `weight` | number | 0 | kg |
| `power` | number | 0 | W |
| `dimensions` | [x, y, z] | `[0, 0, 0]` | m: width (X), height (Y), depth (Z) |
| `dimmerCurve` | `"linear"` \| `"squareLaw"` | `"linear"` | How the dimmer value maps to light output. |
| `movement` | object | below | Speed limits of the moving parts. |

`movement` (all numbers must be > 0; unspecified keys keep their default):

| Key | Default | Unit | Meaning |
|---|---|---|---|
| `panMaxSpeed` | 216 | deg/s | Fastest pan (540 degrees in 2.5 s) |
| `tiltMaxSpeed` | 180 | deg/s | Fastest tilt |
| `panAcceleration` | 600 | deg/s^2 | |
| `tiltAcceleration` | 600 | deg/s^2 | |
| `wheelSlotsPerSecond` | 10 | slots/s | How fast colour / gobo / prism wheels index between slots |
| `indexRotationSpeed` | 720 | deg/s | Speed of gobo and prism index rotation |

### Resources

Binary files that belong to the fixture: gobo images (`png`, `svg`, `jpg`, `bmp`, `tga`, `gif`)
and 3D models (`glb`, `gltf`, `3ds`, `obj`; a `.gltf` finds its `.bin` buffers as further resources).
After loading, a fixture always holds the bytes itself.

| Key | Type | Meaning |
|---|---|---|
| `name` **req** | string | Unique key, referenced by a wheel slot's `image` or a geometry's `model.mesh`. |
| `format` **req** | string | File extension without the dot, lower case. |
| `base64` | string | The file embedded as base64 (whitespace is ignored). Wins if `file` is also given. |
| `file` | string | Path to the file: relative to the folder of the fixture file (`"../../gobos/star.svg"`) or absolute. Use `/` as separator. |

One of `base64` or `file` is required. Gobo images are light masks: white passes light, black blocks it.
SVGs are rasterised when the fixture is loaded.

### Emitters

Optional named light sources, for LED fixtures whose colours are not plain red, green and blue.
A `ColorAdd_*` channel function can name one in its `emitter` key; without it the attribute's
standard colour is used.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` **req** | string | - | |
| `color` | colour | white | Colour of the LED at full output |
| `wavelength` | number | 0 | Dominant wavelength in nm, 0 = unknown |

### Wheels

`wheels` is an array of `{"name": "...", "slots": [...]}` (**both req**, `slots` non-empty).
Channel functions select a slot by wheel name and 1-based slot number.

Slot keys:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `type` **req** | string | - | `open`, `color`, `gobo`, `prism`, `animation` or `frost` |
| `name` | string | `""` | Label for the UI |
| `color` | colour | white | Transmission of the filter (white = passes everything). Always written for `color` slots. |
| `image` | string | `""` | Name of an image resource. **Required** for `gobo` and `animation` slots. |
| `facets` | [x, y][] | - | `prism`: for every facet the angle of its sub-beam from the main axis, in degrees; x = right, y = up when looking along the beam |
| `facetCount` | integer | - | Shorthand for hand-written files, replaces `facets`: a regular pattern of that many facets |
| `deflection` | number | 6 | With `facetCount`: angle in degrees between each sub-beam and the axis |
| `linear` | boolean | false | With `facetCount`: facets in a row instead of a ring |
| `frost` | number | 0 | `frost` slots: diffusion 0..1 (set it for frost slots; always written for them) |

A `prism` slot needs `facets` or `facetCount` (at most 16 facets). The writer always writes `facets`.

## Geometry

The fixture body is a tree of geometries. Each node:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` **req** | string | - | Unique in the whole tree; channels refer to it. |
| `type` | `"generic"` \| `"axis"` \| `"beam"` | `"generic"` | `generic`: static part. `axis`: rotates when driven by `Pan` (about its local **Y**) or `Tilt` (about local **X**). `beam`: a light source. |
| `position` | [x, y, z] | `[0, 0, 0]` | m, relative to the parent |
| `rotation` | [x, y, z] | `[0, 0, 0]` | Degrees, relative to the parent. Euler angles with the rotation matrix `R = Rx(x) * Ry(y) * Rz(z)`. |
| `model` | object | none | What is drawn, see below. Without a model the node is invisible. |
| `beam` | object | defaults | Only for `"type": "beam"`, see below. |
| `children` | object[] | `[]` | Child nodes (same structure) |

A moving head is `Base` (generic) -> `Yoke` (axis, pan) -> `Head` (axis, tilt) -> `Beam` (beam).
A pan channel's `geometry` is the yoke, a tilt channel's the head. Multi-cell fixtures (LED bars,
matrices) have one `beam` node per cell; pixel colours are driven by channels whose `geometry`
names the cell (or a group of cells).

`model`:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `primitive` | string | `"none"` | Built-in shape drawn when there is no mesh (or it fails to load): `none`, `box`, `cylinder` (axis along Y), `sphere`, `base` (moving head base), `yoke` (U shape hanging down), `head` (cylinder along Y, lens bezel at -Y), `conventional` (par can or profile body, lens at -Y) |
| `mesh` | string | `""` | Name of a 3D model resource. GDTF imports use this with `3ds` (Z-up millimetres) or `glb`/`gltf`; the primitive stays as fallback. |
| `size` | [x, y, z] | `[0, 0, 0]` | m, bounding box of the part along local X, Y, Z. A mesh is stretched to it. |
| `color` | colour | `[0.025, 0.025, 0.025]` | Albedo of primitives (and meshes without material) |

`beam` (all keys optional):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `type` | string | `"wash"` | `spot`, `wash`, `beam`, `pc`, `fresnel`, `rectangle` or `glow` |
| `lensRadius` | number | 0.05 | m; beams start as a disc of this radius |
| `beamAngle` | number | 25 | Degrees, full angle at 50 % intensity, before zoom. Must be in (0, 180). |
| `fieldAngle` | number | 1.2 x `beamAngle` | Degrees, full angle at 10 % intensity. Must not be smaller than `beamAngle`. |
| `luminousFlux` | number | 5000 | lm at full output |
| `colorTemperature` | number | 6500 | K, 1000..25000. Colour of the "white" source and the reference for CTO/CTB. |
| `emitterSize` | [w, h] | `[0.1, 0.1]` | m, size of a `rectangle` emitter (always written for rectangles) |

### Geometry groups

`"geometryGroups": [{"name": "Left half", "members": ["Pixel 1", "Pixel 2"]}]` names a set of
geometries. A channel may use the group name as its `geometry` to control all members. A group
name must not be the name of a geometry.

## Modes, channels and functions

```
mode     { name, description, footprint, geometryRoot, channels[] }
channel  { name, offsets[], geometry, default, highlight, functions[] }
function { name, attribute, kind, dmx, physical, wheel, slot|slots, emitter, modeMaster, modeRange, sets[] }
```

### Mode

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` **req** | string | - | Unique among the modes, e.g. `"16 channel"` |
| `description` | string | `""` | |
| `footprint` | integer | highest offset | DMX slots the mode occupies (at most 512). Raised to the highest offset if smaller. |
| `geometryRoot` | string | `""` | Name of a geometry: only this subtree is the fixture in this mode (GDTF fixtures whose modes use different bodies) |
| `channels` | object[] **req** | - | |

### Channel

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` **req** | string | - | Unique within the mode (`modeMaster` refers to it) |
| `offsets` **req** | integer[] | - | 1-based slots in the mode, **coarse byte first**: `[5]` is 8 bit, `[1, 2]` 16 bit, `[1, 2, 3]` 24 bit (at most 3). Each slot is 1..512 and used by one channel only. `[]` is a *virtual channel* that has no DMX slot and always holds its `default`. |
| `geometry` | string | `""` | Geometry node or group the channel controls. Empty = the whole fixture. |
| `default` | integer | 0 | The channel's default DMX value (what the fixture's slots are set to before anything is programmed), in channel resolution |
| `highlight` | integer | none | Value for the "highlight" / locate button |
| `functions` | object[] **req** | - | At least one |

### Function

A function says what a DMX range of the channel does.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | `""` | Label ("Strobe slow -> fast") |
| `attribute` **req** | string | - | What it controls, see [Attributes](#attributes). A name that is not in the list is kept (and shown) but not rendered. |
| `kind` | string | the attribute's default | How the range is interpreted: `linear`, `wheelSlot`, `spin`, `shutterOpen`, `shutterClosed`, `strobe`, `strobePulse`, `strobePulseOpen`, `strobePulseClose`, `strobeRandom`, `strobeRampUp`, `strobeRampDown`, `strobeRampUpDown`, `strobeLightning`, `noFeature`. Only needed when it differs from the default of the attribute. |
| `dmx` | [from, to] | whole channel | Inclusive range in channel resolution. May be left out only if the channel has exactly one function. Ranges of one channel normally do not overlap; if they do, all matching functions apply (used for "prism in + rotate" ranges). |
| `physical` | [from, to] | the attribute's default | Value at `dmx` from and to, interpolated linearly. Units depend on the attribute (see the table below); angles are degrees. Not written for `wheelSlot`, `noFeature`, `shutterOpen` and `shutterClosed` functions. |
| `wheel` | string | `""` | Wheel the function uses. **Required** for `wheelSlot`; also used by wheel spin functions. |
| `slot` | number | - | `wheelSlot`: wheel position for the whole range (1-based) |
| `slots` | [from, to] | - | `wheelSlot`: wheel position at `dmx` from and to (fractional = between slots). One of `slot` / `slots` is required for `wheelSlot` functions; the writer always writes `slots`. |
| `emitter` | string | `""` | `ColorAdd_*`: name of an entry of `emitters` |
| `modeMaster` | string | `""` | Name of another channel of the mode. The function is only active while that channel's value is inside `modeRange` (switching channels). |
| `modeRange` | [from, to] | - | Range of the master channel, in *its* resolution. **Required** together with `modeMaster`. |
| `sets` | object[] | `[]` | Named sub-ranges for the UI: `{"name": "Slow", "dmx": [10, 40]}` (both req) |

Notes:

* A 16-bit channel's function ranges are 16-bit values (`"dmx": [0, 65535]`); an 8-bit
  definition imported into a 16-bit mode is scaled by the importer.
* Pan and tilt `physical` values are the angle of the axis in degrees, 0 being the home position (beam
  straight down at pan = tilt = 0): `[-270, 270]` gives the usual 540 degree pan, the middle of the
  range being home.
* Dimmer, colour levels, iris, frost, focus and blade insertion are ratios 0..1. Iris counts the open
  fraction (default `[1, 0]`: DMX 0 is wide open).
* `Shutter1` functions have no physical value: use `kind` `shutterClosed` / `shutterOpen`. Strobing
  uses `Shutter1Strobe` (and the other `Shutter1Strobe*` attributes) with `physical` in Hz.
* A gobo wheel normally has one `wheelSlot` function per slot (or per range of slots), a gobo
  rotation channel has a `Gobo1Pos` (index angle) and a `Gobo1PosRotate` (speed) function.

## Attributes

Attribute names follow GDTF (DIN SPEC 15800). The exact, case-sensitive spelling is required.
"Default `physical`" is used when a function has no `physical` key; units are file units
(degrees, degrees/s, ratio, Hz, K or s as listed).

| Attributes | Group | Unit of `physical` | Default `kind` | Default `physical` |
|---|---|---|---|---|
| `NoFeature` | Control | - | noFeature | - |
| `Dimmer` | Dimmer | ratio | linear | 0 .. 1 |
| `Pan` | Position | degrees | linear | -270 .. 270 |
| `Tilt` | Position | degrees | linear | -135 .. 135 |
| `PanRotate`, `TiltRotate` | Position | degrees/s | spin | -360 .. 360 |
| `PositionEffect` | Position | - | linear | 0 .. 1 |
| `PositionMSpeed` | Position | s | linear | 0 .. 1 |
| `ColorAdd_R`, `ColorAdd_G`, `ColorAdd_B`, `ColorAdd_C`, `ColorAdd_M`, `ColorAdd_Y`, `ColorAdd_RY`, `ColorAdd_GY`, `ColorAdd_GC`, `ColorAdd_BC`, `ColorAdd_BM`, `ColorAdd_RM`, `ColorAdd_W`, `ColorAdd_WW`, `ColorAdd_CW`, `ColorAdd_UV`, `ColorAdd_A`, `ColorAdd_Lime`, `ColorAdd_Indigo`, `ColorSub_C`, `ColorSub_M`, `ColorSub_Y` | Color | ratio | linear | 0 .. 1 |
| `CTO` | Color | K | linear | 6500 .. 3200 |
| `CTC` | Color | K | linear | 2700 .. 8000 |
| `CTB` | Color | K | linear | 3200 .. 8000 |
| `ColorMacro1`, `Color1`, `Color2`, `Color3` | Color | - | wheelSlot | - |
| `Color1WheelSpin`, `Color2WheelSpin`, `Color3WheelSpin` | Color | degrees/s | spin | -360 .. 360 |
| `Gobo1`, `Gobo2` | Gobo | - | wheelSlot | - |
| `Gobo1Pos`, `Gobo2Pos` | Gobo | degrees | linear | 0 .. 360 |
| `Gobo1PosRotate`, `Gobo2PosRotate`, `Gobo1WheelSpin`, `Gobo2WheelSpin` | Gobo | degrees/s | spin | -360 .. 360 |
| `AnimationWheel1` | Gobo | - | wheelSlot | - |
| `AnimationWheel1Pos` | Gobo | degrees | linear | 0 .. 360 |
| `AnimationWheel1PosRotate` | Gobo | degrees/s | spin | -360 .. 360 |
| `Prism1`, `Prism2` | Beam | - | wheelSlot | - |
| `Prism1Pos`, `Prism2Pos` | Beam | degrees | linear | 0 .. 360 |
| `Prism1PosRotate`, `Prism2PosRotate` | Beam | degrees/s | spin | -360 .. 360 |
| `Zoom` | Beam | degrees | linear | 5 .. 50 |
| `Focus1` | Focus | ratio | linear | 0 .. 1 |
| `Iris` | Beam | ratio | linear | 1 .. 0 |
| `Frost1`, `Frost2` | Beam | ratio | linear | 0 .. 1 |
| `Shutter1` | Shutter | - | shutterOpen | - |
| `Shutter1Strobe` | Shutter | Hz | strobe | 1 .. 20 |
| `Shutter1StrobePulse` | Shutter | Hz | strobePulse | 0.5 .. 10 |
| `Shutter1StrobePulseClose` | Shutter | Hz | strobePulseClose | 0.5 .. 10 |
| `Shutter1StrobePulseOpen` | Shutter | Hz | strobePulseOpen | 0.5 .. 10 |
| `Shutter1StrobeRandom` | Shutter | Hz | strobeRandom | 1 .. 20 |
| `Shutter1StrobeRandomPulse` | Shutter | Hz | strobeRandom | 0.5 .. 10 |
| `Shutter1StrobeRampUp` | Shutter | Hz | strobeRampUp | 0.5 .. 10 |
| `Shutter1StrobeRampDown` | Shutter | Hz | strobeRampDown | 0.5 .. 10 |
| `Shutter1StrobeRampUpDown` | Shutter | Hz | strobeRampUpDown | 0.5 .. 10 |
| `Shutter1StrobeLightning`, `Shutter1StrobeSpikes` | Shutter | Hz | strobeLightning | 0.5 .. 5 |
| `Shutter1StrobeEffect` | Shutter | Hz | strobe | 1 .. 20 |
| `StrobeFrequency` | Shutter | Hz | linear | 0 .. 25 |
| `StrobeDuration` | Shutter | s | linear | 0 .. 0.5 |
| `Blade1A`, `Blade2A`, `Blade3A`, `Blade4A`, `Blade1B`, `Blade2B`, `Blade3B`, `Blade4B` | Shutter | ratio | linear | 0 .. 1 |
| `Blade1Rot`, `Blade2Rot`, `Blade3Rot`, `Blade4Rot`, `ShaperRot` | Shutter | degrees | linear | -45 .. 45 |
| `Control`, `Effects1` | Control | - | linear | 0 .. 1 |
| `Effects1Rate` | Control | ratio | linear | 0 .. 1 |
| `Effects1Fade` | Control | s | linear | 0 .. 1 |
| `Effects1Adjust1` | Control | ratio | linear | 0 .. 1 |

`Unknown` is not a name you write: attributes that are not in this table are kept as they are
(with a warning in the log) so files from newer versions or other tools do not lose data.

## Validation rules and error messages

Loading stops at the first structural problem and reports it with the JSON path of the field:

```
modes[0].channels[2].functions[1].dmx: expected [from, to]
geometry.beam.beamAngle: must be in (0, 180) degrees
resources[0].file: cannot open /.../gobos/star.svg
```

Structural problems are wrong types, missing required keys, unknown enum names (slot `type`, geometry
`type`, `primitive`, beam `type`, `kind`, `source`, `dimmerCurve`) and values out of range.
When the file parses, the whole fixture is checked; the first problem is reported and the number of
the others is appended ("... (and 2 more problems)"):

* `id` and `name` are not empty; resource, wheel, geometry, group, mode and (per mode) channel names are unique.
* Every `image` and `model.mesh` names an existing resource; a wheel has at least one slot and a prism slot at most 16 facets.
* `fieldAngle` is not smaller than `beamAngle`.
* Group members are existing geometries; a group name is not also a geometry name.
* At least one mode; `footprint` covers the highest offset and is at most 512; `geometryRoot` is an existing geometry.
* A channel has at most 3 offsets, every offset is 1 or more and used by one channel only; `geometry` is an existing
  geometry or group; `default` fits the channel's resolution.
* A function's `dmx` range is ordered and fits the channel's resolution; `wheelSlot` functions name an existing wheel and
  slot positions are within the wheel (0 .. number of slots + 1); `emitter` names an existing emitter; `modeMaster` an existing channel of the mode.

The importers (OFL, GDTF) use the same checks and only warn, so an imported fixture can be
loaded and repaired in the editor even if it is not perfect.

## Saving

`FixtureSerializer::saveFile(type, path, &error, options)` and `FixtureLibrary::saveNative()` write this format.

* Numbers are written with at most 6 significant digits; angles are converted back to degrees; rotations are written as
  canonical Euler angles, so saving a loaded file again produces the same text.
* `embedResources = true` (default) writes one self-contained file with `base64` resources.
  `embedResources = false` writes the files to `<file name without extension>.resources/<resource name>.<format>` next to
  the fixture file and references them with a relative `file`.
* Hand-written shorthands (`#rrggbb` colours, `facetCount`, `slot`, a missing `dmx` on single-function channels, a
  missing `fieldAngle`) are expanded when saved.

## Complete example

A moving head spot with two modes: 16-bit pan/tilt with gobo, prism, zoom and a mode-dependent
channel, and a compact 8-bit RGB mode. The gobo image is embedded; to keep a library tidy,
reference shared files instead (`{"name": "dots", "format": "svg", "file": "../../gobos/dots.svg"}`).

```json
{
  "formatVersion": 1,
  "id": "acme/example-spot",
  "manufacturer": "Acme",
  "name": "Example Spot",
  "shortName": "ExSpot",
  "description": "LED moving head spot, written by hand to show every part of the format.",
  "source": "native",
  "revision": "1",
  "categories": ["Moving Head"],
  "physical": {
    "weight": 14.5,
    "power": 320,
    "dimensions": [0.3, 0.55, 0.25],
    "dimmerCurve": "linear",
    "movement": {
      "panMaxSpeed": 216,
      "tiltMaxSpeed": 180,
      "panAcceleration": 600,
      "tiltAcceleration": 600,
      "wheelSlotsPerSecond": 10,
      "indexRotationSpeed": 720
    }
  },
  "emitters": [
    {"name": "Deep Red", "color": "#ff1500", "wavelength": 640}
  ],
  "wheels": [
    {
      "name": "Gobos",
      "slots": [
        {"type": "open", "name": "Open"},
        {"type": "gobo", "name": "Dots", "image": "dots"}
      ]
    },
    {
      "name": "Prism",
      "slots": [
        {"type": "open", "name": "Open"},
        {"type": "prism", "name": "3-facet", "facetCount": 3, "deflection": 5}
      ]
    }
  ],
  "geometry": {
    "name": "Base",
    "type": "generic",
    "model": {"primitive": "base", "size": [0.3, 0.15, 0.25]},
    "children": [
      {
        "name": "Yoke",
        "type": "axis",
        "position": [0, -0.15, 0],
        "model": {"primitive": "yoke", "size": [0.36, 0.3, 0.08]},
        "children": [
          {
            "name": "Head",
            "type": "axis",
            "position": [0, -0.2, 0],
            "model": {"primitive": "head", "size": [0.22, 0.35, 0.22], "color": [0.02, 0.02, 0.02]},
            "children": [
              {
                "name": "Beam",
                "type": "beam",
                "position": [0, -0.175, 0],
                "beam": {
                  "type": "spot",
                  "lensRadius": 0.07,
                  "beamAngle": 14,
                  "fieldAngle": 22,
                  "luminousFlux": 9000,
                  "colorTemperature": 7200
                }
              }
            ]
          }
        ]
      }
    ]
  },
  "geometryGroups": [
    {"name": "Lens", "members": ["Beam"]}
  ],
  "modes": [
    {
      "name": "Standard",
      "description": "16-bit pan and tilt",
      "footprint": 15,
      "channels": [
        {
          "name": "Pan",
          "offsets": [1, 2],
          "geometry": "Yoke",
          "default": 32768,
          "functions": [
            {"attribute": "Pan", "physical": [-270, 270]}
          ]
        },
        {
          "name": "Tilt",
          "offsets": [3, 4],
          "geometry": "Head",
          "default": 32768,
          "functions": [
            {"attribute": "Tilt", "physical": [-135, 135]}
          ]
        },
        {
          "name": "Dimmer",
          "offsets": [5],
          "geometry": "Lens",
          "default": 0,
          "highlight": 255,
          "functions": [
            {"attribute": "Dimmer", "physical": [0, 1]}
          ]
        },
        {
          "name": "Shutter",
          "offsets": [6],
          "default": 32,
          "functions": [
            {"name": "Closed", "attribute": "Shutter1", "kind": "shutterClosed", "dmx": [0, 31]},
            {"name": "Open", "attribute": "Shutter1", "kind": "shutterOpen", "dmx": [32, 63]},
            {"name": "Strobe", "attribute": "Shutter1Strobe", "dmx": [64, 255], "physical": [1, 20]}
          ]
        },
        {
          "name": "Red",
          "offsets": [7],
          "geometry": "Beam",
          "functions": [
            {"attribute": "ColorAdd_R", "emitter": "Deep Red"}
          ]
        },
        {
          "name": "Green",
          "offsets": [8],
          "geometry": "Beam",
          "functions": [
            {"attribute": "ColorAdd_G"}
          ]
        },
        {
          "name": "Blue",
          "offsets": [9],
          "geometry": "Beam",
          "functions": [
            {"attribute": "ColorAdd_B"}
          ]
        },
        {
          "name": "Gobo",
          "offsets": [10],
          "functions": [
            {"name": "Open", "attribute": "Gobo1", "dmx": [0, 15], "wheel": "Gobos", "slot": 1},
            {"name": "Dots", "attribute": "Gobo1", "dmx": [16, 255], "wheel": "Gobos", "slot": 2}
          ]
        },
        {
          "name": "Gobo rotation",
          "offsets": [11],
          "functions": [
            {"name": "Index", "attribute": "Gobo1Pos", "dmx": [0, 127], "physical": [0, 360]},
            {"name": "Spin", "attribute": "Gobo1PosRotate", "dmx": [128, 255], "physical": [-180, 180]}
          ]
        },
        {
          "name": "Prism",
          "offsets": [12],
          "functions": [
            {"name": "Out", "attribute": "Prism1", "dmx": [0, 127], "wheel": "Prism", "slot": 1},
            {"name": "In", "attribute": "Prism1", "dmx": [128, 255], "wheel": "Prism", "slot": 2}
          ]
        },
        {
          "name": "Zoom",
          "offsets": [13],
          "functions": [
            {"attribute": "Zoom", "physical": [10, 40]}
          ]
        },
        {
          "name": "Control",
          "offsets": [14],
          "functions": [
            {"name": "Normal", "attribute": "NoFeature", "dmx": [0, 199]},
            {
              "name": "Reset",
              "attribute": "Control",
              "dmx": [200, 255],
              "sets": [
                {"name": "Reset pan/tilt", "dmx": [200, 229]},
                {"name": "Reset all", "dmx": [230, 255]}
              ]
            }
          ]
        },
        {
          "name": "Effect",
          "offsets": [15],
          "functions": [
            {"attribute": "Effects1", "modeMaster": "Control", "modeRange": [200, 255]}
          ]
        }
      ]
    },
    {
      "name": "Compact",
      "description": "8-bit pan and tilt, RGB",
      "channels": [
        {
          "name": "Pan",
          "offsets": [1],
          "geometry": "Yoke",
          "default": 128,
          "functions": [
            {"attribute": "Pan", "physical": [-270, 270]}
          ]
        },
        {
          "name": "Tilt",
          "offsets": [2],
          "geometry": "Head",
          "default": 128,
          "functions": [
            {"attribute": "Tilt", "physical": [-135, 135]}
          ]
        },
        {
          "name": "Dimmer",
          "offsets": [3],
          "functions": [
            {"attribute": "Dimmer"}
          ]
        },
        {
          "name": "Red",
          "offsets": [4],
          "geometry": "Beam",
          "functions": [
            {"attribute": "ColorAdd_R", "emitter": "Deep Red"}
          ]
        },
        {
          "name": "Green",
          "offsets": [5],
          "geometry": "Beam",
          "functions": [
            {"attribute": "ColorAdd_G"}
          ]
        },
        {
          "name": "Blue",
          "offsets": [6],
          "geometry": "Beam",
          "functions": [
            {"attribute": "ColorAdd_B"}
          ]
        }
      ]
    }
  ],
  "resources": [
    {
      "name": "dots",
      "format": "svg",
      "base64": "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSI2NCIgaGVpZ2h0PSI2NCIgdmlld0JveD0iMCAwIDY0IDY0Ij48cmVjdCB3aWR0aD0iNjQiIGhlaWdodD0iNjQiIGZpbGw9IiMwMDAiLz48Y2lyY2xlIGN4PSIzMiIgY3k9IjMyIiByPSIxNCIgZmlsbD0iI2ZmZiIvPjwvc3ZnPg=="
    }
  ]
}
```
