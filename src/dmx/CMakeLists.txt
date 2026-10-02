dmxviz_add_module(dmx DEPS dmxviz::core)
if(WIN32)
    target_link_libraries(dmxviz_dmx PUBLIC ws2_32 iphlpapi setupapi)
endif()
