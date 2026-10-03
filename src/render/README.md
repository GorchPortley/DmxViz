# Render module

Deferred renderer on sokol_gfx / OpenGL 4.3 core. The design (passes, formats, rules) is in
[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md) section 7; this file adds the cost model and the tunables.
Public interface: `Renderer.h` (renderer, viewport target), `RenderScene.h` (what the app hands over each frame),
`RenderSettings.h` (per-machine quality settings). Shaders live in `shaders/*.glsl`.

| Pass | Resolution | Draws | Shader |
|------|-----------|-------|--------|
| G-buffer (meshes) | full | one per mesh id | `gbuffer.glsl` |
| Spot lighting (beams on surfaces) | full | 1 instanced | `spot_light.glsl` |
| Haze: depth min/max, ray march, upsample | 1/2 or 1/4 | 3 | `depth_downsample`, `volumetric`, `upsample` |
| Lens glow | full | 1 instanced | `lens_glow.glsl` |
| Bloom (6 levels) | 1/2 and below | 11 | `bloom_down`, `bloom_up` |
| Composite (tonemap, outline, grid, lines) | full | 1-3 | `composite.glsl` |

## Performance

NFR-1: 60 fps with 500 fixtures / 1000 beams / 64 universes on a GTX 1060 class GPU at 1080p, and under 2 ms of CPU
simulation for that rig.

### CPU (measured)

`tools/sim_bench` builds the reference rig (500 fixtures of the eight bundled types: 1200 beams including 100 pixel
bars, 2508 mesh instances per frame, 64 universes with every universe in use), changes the DMX of every fixture
every frame (pan/tilt sweeps, colour fades per pixel, zoom, gobo and prism changes, strobe bursts), and times
`UniverseStore::snapshot`, `Simulation::update` and `BeamPacker::pack`. Heap allocations are counted with a global
`operator new` replacement. No window, no GPU.

```
sim_bench [--frames N] [--warmup N] [--fixtures N] [--universes N] [--no-selection] [--edit] [--hold]
```

Release build (GCC 13, -O3), 4 vCPU Xeon 2.8 GHz, 300 frames; `ms` is per frame, the spread comes from other jobs
running on the same machine:

| Part | avg ms | p95 ms | allocations / frame |
|------|-------:|-------:|--------------------:|
| DMX snapshot (64 universes) | 0.05 | 0.09 | 0 |
| Simulation, everything animated (worst case) | 0.8 - 1.0 | 1.0 - 1.7 | 0 |
| Simulation, DMX held (a show that holds its look) | 0.57 | 0.65 | 0 |
| Simulation, scene edited every frame (`--edit`, gizmo drag) | 1.2 - 1.3 | 1.5 - 1.8 | 28 |
| Beam packer (cull, prisms, gobo layers, coverage estimate) | 0.37 | 0.5 | 0 |

The simulation meets the 2 ms budget with a factor of two to spare and does not allocate in steady state. Where the
time goes (callgrind, animated case, steady state): about 40 % DMX decode, 35 % axis physics and optics
(`computeOptics` alone 26 %), 17 % `emit`, the rest is the scene lookups and the DMX copy. The mesh instance packing of the renderer (one counting-sort pass over about
2500 instances) cannot be separated from the GL objects and was not timed; it is linear and small next to the
beam packer.

What changed in this pass: `FixtureRuntime::setDmx` skips the decode when the footprint bytes are identical to the
previous frame (decode is a pure function of them), which is the normal case in a real show (1.0 -> 0.57 ms with
held DMX). The edit case allocates about 28 times per frame in `Scene::nodesOfKind` / `depthFirst` (they return fresh
vectors) while the scene is being dragged; that is not steady state and lives in `src/stage`.

`render_sandbox --beams 1000` under `xvfb-run` prints the average CPU frame time at exit, but with llvmpipe the
"render record" time contains the software rasteriser's GPU work (about 1 - 2 s per frame at 800x450), so it says
nothing about the CPU cost of the renderer; use the sim_bench numbers above.

### GPU (cost model, not measured)

The development container has no GPU (llvmpipe), so these are estimates for 1920x1080, 1000 beams, GTX 1060
(about 4.4 TFLOP/s, 192 GB/s), to be replaced by measurements (`render_sandbox --beams 1000 --width 1920 --height
1080`, then `--no-volumetrics` and `--vol-res quarter` to take the passes apart).

| Pass | Cost grows with | Estimate |
|------|-----------------|----------|
| G-buffer | mesh instances (2.5k) and 24 B/pixel written | about 1 ms |
| Spot lighting | surface pixels inside beams (hull back faces that pass the depth test) x ~200 ALU and a 16 B blend | 1 - 2 ms |
| Haze march | **covered volume pixels x (80 + steps x 200) flops**, at about 1.5 TFLOP/s effective | see below |
| Haze depth + upsample | volume pixels x block taps; full-res pixels x 9 taps | 0.3 - 0.4 ms |
| Lens glow | sprite area (1000 small quads) | under 0.3 ms |
| Bloom + composite | full-res and half-res pixels | about 0.5 ms |

The haze march dominates, which is why it has the most controls. `BeamPacker` estimates the pixels every beam covers
(`hullCoveragePixels`: side-on trapezoid blended with the far disc by viewing angle, capped at the target size). For
the sim_bench rig and a front-of-house camera this is about 26 million volume pixels at half res (overdraw included;
a pessimistic bound, because the depth and floor clipping that discards fragments before the march is ignored) and
about 6.5 million at quarter res. Plugging that into the model for the pessimistic coverage:

| Haze setting | Steps | Haze march | Whole frame |
|--------------|------:|-----------:|------------:|
| half res, automatic quality off, budget 150 | 5.8 | about 20 ms | about 25 ms |
| half res, steps at the 3-step floor | 3 | about 12 ms | about 17 ms |
| quarter res (automatic quality level 3) | 3 | about 3 ms | about 8 ms |

So by this model the reference rig is at the edge of 60 fps at half res on a GTX 1060 and well inside it at quarter
res; the automatic quality exists to find that out on the actual machine. Real overdraw is lower than the estimate,
and a rig of 1000 narrow pixel beams costs far less than 1000 wide moving heads.

### Cost controls of the haze pass

Already per pixel in `volumetric.glsl`: the beam hull is rasterised (back faces only), so a pixel outside the cone
costs nothing; the view ray is intersected with the cone analytically and the segment is clipped by the scene depth
and the floor before anything is marched (a fully occluded beam discards at that point); the step count follows the
on-screen length of the segment (`marchPixelsPerStep`), so far, short or foreshortened beams march few steps; samples
outside the beam profile are skipped. On the CPU, beams with zero output, outside the frustum or below a pixel never
reach the GPU's haze list.

Added in this pass:

* **Haze resolution** (`RenderSettings::volumetricResolution`): half (default) or quarter. Quarter is about 4x
  cheaper and softer. The depth min/max pass and the bilateral upsample handle the 4x4 blocks.
* **Cost budget** (`volumetricBudget`, millions of volume pixel x march step per view, default 150 = about 15 ms on a
  GTX 1060 by the model): when the estimated coverage x steps exceeds it, `BeamPacker::marchStepCap()` lowers the
  step cap, down to 3 steps (below `minMarchSteps`). It is a safety net: a normal show stays far below it, so the
  default look is unchanged (the sandbox demo renders pixel-identical). `RenderStats` reports divisor, cap, coverage.
* **Automatic quality** (`autoQuality`, `targetFrameMs`; `AutoQuality.h`): watches the time between presented
  frames; while the smoothed frame time is over the target it steps down through level 1 (steps x 0.65, budget x
  0.5), 2 (x 0.4, x 0.25), 3 (quarter res, x 0.65, x 0.15) and 4 (quarter res, x 0.4, x 0.08). With vsync a low
  frame time cannot show headroom, so after 5 s at a reduced level it probes one level up and keeps it if the frames
  hold; failed probes back off (10, 20, ... 60 s). The user's settings are never modified, the renderer works on an
  adjusted copy. Off by default so that screenshots stay reproducible. Stalls above 250 ms (window drag, shader
  compile) are ignored, which also means it does nothing under a software rasteriser.

### Tunables

| Setting | Default | Effect |
|---------|---------|--------|
| `volumetrics` | on | Off removes the haze pass completely |
| `volumetricResolution` | half | Quarter: 4x fewer haze pixels |
| `minMarchSteps` / `maxMarchSteps` | 6 / 20 | Per-pixel step range |
| `marchPixelsPerStep` | 6 | Larger = fewer steps on long beams |
| `volumetricBudget` | 150 | Millions of (pixel x step) per frame, 0 = unlimited |
| `autoQuality` / `targetFrameMs` | off / 16.7 | Automatic quality and its frame time target |
| `maxBeamLength`, `minIlluminance` | 60 m, 0.5 lx | Shorter hulls mean fewer covered pixels |
| `lensGlow`, `bloom` | on | Cheap, but they can be switched off |

The Environment panel (Quality section) shows resolution, automatic quality and the target frame time; the sandbox
takes `--vol-res half|quarter`, `--auto-quality`, `--target-ms` and `--no-volumetrics`.
