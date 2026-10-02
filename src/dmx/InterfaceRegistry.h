#pragma once
// InterfaceRegistry: the DMX interface *types* a user can add ("Add interface ->
// Art-Net / sACN / Enttec DMX USB Pro / ..."), each with a factory function.
//
// DmxManager uses it to create interfaces by name, both from the UI and when loading a
// saved configuration. Tests can register their own fake types.

#include "dmx/DmxInterface.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::dmx {

struct InterfaceTypeInfo {
    std::string name;         // equals DmxInterface::typeName() of what create() returns
    std::string description;  // one line for the UI
    Capabilities caps;
    std::function<std::unique_ptr<DmxInterface>()> create;
};

class InterfaceRegistry {
public:
    // Art-Net, sACN, Enttec DMX USB Pro, Enttec Open DMX USB and Loopback.
    static InterfaceRegistry withBuiltinTypes();

    // Adds a type, replacing one with the same name.
    void add(InterfaceTypeInfo type);
    const std::vector<InterfaceTypeInfo>& types() const { return types_; }
    const InterfaceTypeInfo* find(std::string_view name) const;
    // A new, stopped interface of the named type, or nullptr for unknown names.
    std::unique_ptr<DmxInterface> create(std::string_view name) const;

private:
    std::vector<InterfaceTypeInfo> types_;
};

}  // namespace dmxviz::dmx
