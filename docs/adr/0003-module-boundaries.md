# ADR 0003 – Module boundaries and parallel development

**Status**: accepted (2026-10-02)

## Decision
* One static library per module (`core`, `assets`, `dmx`, `fixtures`, `stage`,
  `render`, `ui`) plus the `dmxviz` executable. Allowed dependencies are listed
  in ARCHITECTURE.md §1 and enforced through CMake target links.
* Cross-module per-frame data uses plain structs in `core/SceneTypes.h`
  (`MeshInstance`, `BeamState`). The fixture runtime produces them; the renderer
  consumes them; neither knows the other exists.
* The renderer draws a flat `RenderScene`, never the scene graph.
* The fixture runtime takes raw DMX bytes, not DMX module types.
* Sources are globbed per module so parallel work streams do not conflict on
  build files.
* Contract headers (`core/*`, `assets/AssetLibrary.h`, `assets/MeshData.h`,
  `render/RenderScene.h`) change only through the integration owner.

## Consequences
Work streams can be developed and tested in isolation. The cost is one copy of
per-frame data from fixture runtimes into flat arrays, which is negligible
(≈ 300 bytes per beam).
