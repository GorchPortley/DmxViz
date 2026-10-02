dmxviz_add_module(core DEPS dep::glm dep::json)
find_package(Threads REQUIRED)
target_link_libraries(dmxviz_core PUBLIC Threads::Threads)
