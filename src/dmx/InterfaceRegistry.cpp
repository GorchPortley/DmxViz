#include "dmx/InterfaceRegistry.h"

#include "dmx/interfaces/ArtNetInterface.h"
#include "dmx/interfaces/EnttecProInterface.h"
#include "dmx/interfaces/LoopbackInterface.h"
#include "dmx/interfaces/OpenDmxInterface.h"
#include "dmx/interfaces/SacnInterface.h"

#include <algorithm>

namespace dmxviz::dmx {
namespace {

template <typename T>
InterfaceTypeInfo typeInfo(Capabilities caps, std::string description) {
    InterfaceTypeInfo info;
    info.name = T::kTypeName;
    info.description = std::move(description);
    info.caps = caps;
    info.create = [] { return std::unique_ptr<DmxInterface>(std::make_unique<T>()); };
    return info;
}

}  // namespace

InterfaceRegistry InterfaceRegistry::withBuiltinTypes() {
    InterfaceRegistry registry;
    constexpr Capabilities inOut{true, true};
    constexpr Capabilities outOnly{false, true};
    registry.add(typeInfo<ArtNetInterface>(inOut, "Art-Net 4 over UDP: input, output, discovery by consoles"));
    registry.add(typeInfo<SacnInterface>(inOut, "sACN / E1.31 over UDP multicast or unicast: input and output"));
    registry.add(typeInfo<EnttecProInterface>(inOut, "Enttec DMX USB Pro and compatible widgets: input and output"));
    registry.add(typeInfo<OpenDmxInterface>(outOnly, "Enttec Open DMX USB and other FTDI cables: output only"));
    registry.add(typeInfo<LoopbackInterface>(inOut, "Feeds sent universes back as input (testing)"));
    return registry;
}

void InterfaceRegistry::add(InterfaceTypeInfo type) {
    const auto it =
        std::find_if(types_.begin(), types_.end(), [&](const InterfaceTypeInfo& t) { return t.name == type.name; });
    if (it != types_.end())
        *it = std::move(type);
    else
        types_.push_back(std::move(type));
}

const InterfaceTypeInfo* InterfaceRegistry::find(std::string_view name) const {
    for (const InterfaceTypeInfo& type : types_)
        if (type.name == name) return &type;
    return nullptr;
}

std::unique_ptr<DmxInterface> InterfaceRegistry::create(std::string_view name) const {
    const InterfaceTypeInfo* type = find(name);
    return type && type->create ? type->create() : nullptr;
}

}  // namespace dmxviz::dmx
