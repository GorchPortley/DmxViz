dmxviz_add_module(dmx DEPS dmxviz::core)
if(WIN32)
    # Winsock, adapter enumeration (GetAdaptersAddresses), serial port enumeration
    # (SetupAPI) and the registry (port names).
    target_link_libraries(dmxviz_dmx PUBLIC ws2_32 iphlpapi setupapi advapi32)
endif()
