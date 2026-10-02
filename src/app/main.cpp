#include "app/App.h"

#include "sokol_app.h"
#include "sokol_log.h"

#include <memory>

namespace {
std::unique_ptr<dmxviz::app::App> g_app;
}

sapp_desc sokol_main(int argc, char* argv[]) {
    g_app = std::make_unique<dmxviz::app::App>(dmxviz::app::parseCommandLine(argc, argv));

    sapp_desc desc{};
    desc.init_cb = [] { g_app->init(); };
    desc.frame_cb = [] { g_app->frame(); };
    desc.event_cb = [](const sapp_event* ev) { g_app->event(ev); };
    desc.cleanup_cb = [] {
        g_app->cleanup();
        g_app.reset();
    };
    desc.width = g_app->options().width;
    desc.height = g_app->options().height;
    desc.window_title = "DmxViz";
    desc.high_dpi = true;
    desc.enable_clipboard = true;
    desc.enable_dragndrop = true;
    desc.max_dropped_files = 16;
    desc.logger.func = slog_func;
    desc.gl.major_version = 4;
    desc.gl.minor_version = 3;
    return desc;
}
