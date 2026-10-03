# Developer tools. Only built together with the app (they need a GL platform).
add_subdirectory(render_sandbox)
# Headless CPU benchmark of the simulation (NFR-1); no window, but it links the render module for BeamPacker.
add_subdirectory(sim_bench)
