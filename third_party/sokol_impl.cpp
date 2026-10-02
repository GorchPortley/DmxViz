// Single translation unit that compiles every sokol implementation used by DmxViz.
// Backend selection (SOKOL_GLCORE) comes from the dep_sokol CMake target.
#define SOKOL_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"

#include "imgui.h"
#define SOKOL_IMGUI_IMPL
#include "sokol_imgui.h"
