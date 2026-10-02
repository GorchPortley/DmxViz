# third_party

Glue for external dependencies. The dependencies themselves are fetched by
`cmake/Dependencies.cmake` at configure time and are never committed.

| File | Purpose |
|------|---------|
| `sokol_impl.cpp` | Compiles the sokol single-header implementations (app, gfx, glue, log, time, imgui) |
| `miniz/miniz_export.h` | Stand-in for the header miniz normally generates with its own CMake |
