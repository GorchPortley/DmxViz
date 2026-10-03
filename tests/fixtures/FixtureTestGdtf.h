#pragma once
// Builds small GDTF archives in memory for the fixture tests (importer and library tests).

#include "assets/ImageLoader.h"
#include "fixtures/Archive.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace dmxviz::fixtures::test {

// A 16 x 16 greyscale PNG with a white disc, encoded through the assets module.
inline std::vector<std::uint8_t> makeGoboPng() {
    assets::ImageData image;
    image.width = 16;
    image.height = 16;
    image.channels = 1;
    image.pixels.assign(256, 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            if ((x - 8) * (x - 8) + (y - 8) * (y - 8) < 36) image.pixels[static_cast<std::size_t>(y * 16 + x)] = 255;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "dmxviz_test_gobo.png";
    std::vector<std::uint8_t> bytes;
    if (assets::writePng(path, image)) {
        if (auto read = readFileBytes(path)) bytes = std::move(*read);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return bytes;
}

// GDTF matrix text: identity rotation and the given translation (metres, GDTF axes).
inline std::string gdtfPosition(double x, double y, double z) {
    return std::format("{{1,0,0,0}}{{0,1,0,0}}{{0,0,1,0}}{{{},{},{},1}}", x, y, z);
}

// The same with a rotation of 90 degrees about GDTF Z (x -> y).
inline std::string gdtfPositionRotZ90(double x, double y, double z) {
    return std::format("{{0,1,0,0}}{{-1,0,0,0}}{{0,0,1,0}}{{{},{},{},1}}", x, y, z);
}

// A moving head in the style of a GDTF Builder export: base, pan yoke, tilt head, one beam;
// 16-bit pan/tilt, dimmer, colour wheel, gobo wheel, prism, zoom, shutter/strobe, a mode master
// pair, a virtual channel and one attribute DmxViz does not know. Mode "Standard" has 13 slots, "Reduced" 1.
inline std::string movingHeadXml() {
    std::string xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<GDTF DataVersion="1.1">
 <FixtureType Name="Test Spot 700" ShortName="TS700" LongName="Test Spot 700 Profile" Manufacturer="Acme Lighting"
              Description="Moving head for the importer tests" FixtureTypeID="00000000-0000-0000-0000-000000000001">
  <AttributeDefinitions>
   <ActivationGroups/>
   <FeatureGroups><FeatureGroup Name="Dimmer" Pretty="Dimmer"><Feature Name="Dimmer"/></FeatureGroup></FeatureGroups>
   <Attributes>
    <Attribute Name="Dimmer" Pretty="Dim" Feature="Dimmer.Dimmer"/>
    <Attribute Name="Pan" Pretty="P" Feature="Position.PanTilt"/>
    <Attribute Name="Tilt" Pretty="T" Feature="Position.PanTilt"/>
    <Attribute Name="MyCustomAttr" Pretty="Custom" Feature="Control.Control"/>
   </Attributes>
  </AttributeDefinitions>
  <Wheels>
   <Wheel Name="Color Wheel 1">
    <Slot Name="Open" Color="0.3127,0.3290,100.0"/>
    <Slot Name="Red" Color="0.64,0.33,21.26"/>
    <Slot Name="Blue" Color="0.15,0.06,7.22"/>
   </Wheel>
   <Wheel Name="Gobo Wheel 1">
    <Slot Name="Open" Color="0.3127,0.3290,100.0"/>
    <Slot Name="Dots" Color="0.3127,0.3290,100.0" MediaFileName="dots"/>
   </Wheel>
   <Wheel Name="Prism Wheel">
    <Slot Name="Open"/>
    <Slot Name="3-Facet Prism">
     <Facet Color="0.3127,0.3290,100.0" Rotation="{1,0,0}{0,0.9961947,0.0871557}{0,-0.0871557,0.9961947}"/>
     <Facet Color="0.3127,0.3290,100.0" Rotation="{0.9961947,0,-0.0871557}{0,1,0}{0.0871557,0,0.9961947}"/>
     <Facet Color="0.3127,0.3290,100.0" Rotation="{1,0,0}{0,0.9961947,-0.0871557}{0,0.0871557,0.9961947}"/>
    </Slot>
   </Wheel>
  </Wheels>
  <PhysicalDescriptions>
   <Properties><Weight Value="21.5"/><PowerConsumption Value="650"/></Properties>
  </PhysicalDescriptions>
  <Models>
   <Model Name="Base" Length="0.3" Width="0.25" Height="0.2" PrimitiveType="Base" File="base"/>
   <Model Name="Yoke" Length="0.4" Width="0.1" Height="0.3" PrimitiveType="Yoke"/>
   <Model Name="Head" Length="0.25" Width="0.25" Height="0.4" PrimitiveType="Head"/>
   <Model Name="Lens" Length="0.2" Width="0.2" Height="0.01" PrimitiveType="Cylinder"/>
   <Model Name="Lost" Length="0.1" Width="0.1" Height="0.1" PrimitiveType="Undefined" File="not-in-archive"/>
  </Models>
  <Geometries>
   <Geometry Name="Body" Model="Base" Position=")xml";
    xml += gdtfPosition(0, 0, 0) + "\">\n    <Axis Name=\"Yoke\" Model=\"Yoke\" Position=\"" + gdtfPosition(0.05, 0.02, -0.1) +
           "\">\n     <Axis Name=\"Head\" Model=\"Head\" Position=\"" + gdtfPositionRotZ90(0, 0, -0.2) +
           "\">\n      <Beam Name=\"Beam\" Model=\"Lens\" LampType=\"Discharge\" PowerConsumption=\"650\" "
           "LuminousFlux=\"14000\" ColorTemperature=\"7000\" BeamAngle=\"12\" FieldAngle=\"22\" BeamRadius=\"0.08\" "
           "BeamType=\"Spot\" Position=\"" + gdtfPosition(0, 0, -0.2) +
           "\"/>\n      <Display Name=\"Screen\" Model=\"Lost\" Position=\"" + gdtfPosition(0.1, 0, 0) +
           "\"/>\n      <Bogus Name=\"Mystery\"/>\n     </Axis>\n    </Axis>\n   </Geometry>\n  </Geometries>\n";
    xml += R"xml(  <DMXModes>
   <DMXMode Name="Standard" Geometry="Body">
    <DMXChannels>
     <DMXChannel DMXBreak="1" Offset="1,2" InitialFunction="Yoke_Pan.Pan.Pan 1" Highlight="None" Geometry="Yoke">
      <LogicalChannel Attribute="Pan" Snap="No" Master="None" MibFade="0" DMXChangeTimeLimit="0">
       <ChannelFunction Name="Pan 1" Attribute="Pan" DMXFrom="0/2" Default="32768/2" PhysicalFrom="-270" PhysicalTo="270"
                        RealFade="2.7" RealAcceleration="0.5"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="3,4" InitialFunction="Head_Tilt.Tilt.Tilt 1" Geometry="Head">
      <LogicalChannel Attribute="Tilt">
       <ChannelFunction Name="Tilt 1" Attribute="Tilt" DMXFrom="0/2" Default="32768/2" PhysicalFrom="-135" PhysicalTo="135"
                        RealFade="1.35"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="5" Geometry="Beam" Highlight="255/1">
      <LogicalChannel Attribute="Dimmer">
       <ChannelFunction Name="Dimmer 1" Attribute="Dimmer" DMXFrom="0/1" Default="0/1" PhysicalFrom="0" PhysicalTo="1"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="6" Geometry="Beam">
      <LogicalChannel Attribute="Color1">
       <ChannelFunction Name="Open" Attribute="Color1" DMXFrom="0/1" Wheel="Color Wheel 1">
        <ChannelSet Name="Open" DMXFrom="0/1" WheelSlotIndex="1"/>
       </ChannelFunction>
       <ChannelFunction Name="Red" Attribute="Color1" DMXFrom="10/1" Wheel="Color Wheel 1">
        <ChannelSet Name="Red" DMXFrom="10/1" WheelSlotIndex="2"/>
       </ChannelFunction>
       <ChannelFunction Name="Blue" Attribute="Color1" DMXFrom="20/1" Wheel="Color Wheel 1">
        <ChannelSet Name="Blue" DMXFrom="20/1" WheelSlotIndex="3"/>
       </ChannelFunction>
       <ChannelFunction Name="Rainbow" Attribute="Color1WheelSpin" DMXFrom="30/1" Wheel="Color Wheel 1"
                        PhysicalFrom="-90" PhysicalTo="90"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="7" Geometry="Beam">
      <LogicalChannel Attribute="Gobo1">
       <ChannelFunction Name="Open" Attribute="Gobo1" DMXFrom="0/1" Wheel="Gobo Wheel 1">
        <ChannelSet Name="Open" DMXFrom="0/1" WheelSlotIndex="1"/>
       </ChannelFunction>
       <ChannelFunction Name="Dots" Attribute="Gobo1" DMXFrom="16/1" Wheel="Gobo Wheel 1">
        <ChannelSet Name="Dots" DMXFrom="16/1" WheelSlotIndex="2"/>
        <ChannelSet Name="Dots shake" DMXFrom="40/1" WheelSlotIndex="2"/>
       </ChannelFunction>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="8" Geometry="Beam">
      <LogicalChannel Attribute="Prism1">
       <ChannelFunction Name="Out" Attribute="Prism1" DMXFrom="0/1" Wheel="Prism Wheel">
        <ChannelSet Name="Out" DMXFrom="0/1" WheelSlotIndex="1"/>
       </ChannelFunction>
       <ChannelFunction Name="3-Facet" Attribute="Prism1" DMXFrom="128/1" Wheel="Prism Wheel">
        <ChannelSet Name="3-Facet" DMXFrom="128/1" WheelSlotIndex="2"/>
       </ChannelFunction>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="9" Geometry="Beam">
      <LogicalChannel Attribute="Zoom">
       <ChannelFunction Name="Zoom 1" Attribute="Zoom" DMXFrom="0/1" Default="0/1" PhysicalFrom="8" PhysicalTo="42"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="10" Geometry="Beam" InitialFunction="Beam_Shutter1.Shutter1.Open">
      <LogicalChannel Attribute="Shutter1">
       <ChannelFunction Name="Closed" Attribute="Shutter1" DMXFrom="0/1" PhysicalFrom="0" PhysicalTo="0"/>
       <ChannelFunction Name="Open" Attribute="Shutter1" DMXFrom="32/1" Default="32/1" PhysicalFrom="1" PhysicalTo="1"/>
      </LogicalChannel>
      <LogicalChannel Attribute="Shutter1Strobe">
       <ChannelFunction Name="Strobe" Attribute="Shutter1Strobe" DMXFrom="64/1" PhysicalFrom="1" PhysicalTo="20"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="11" Geometry="Body">
      <LogicalChannel Attribute="Control">
       <ChannelFunction Name="Idle" Attribute="NoFeature" DMXFrom="0/1"/>
       <ChannelFunction Name="Reset" Attribute="Control" DMXFrom="200/1"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="12" Geometry="Body">
      <LogicalChannel Attribute="Effects1">
       <ChannelFunction Name="Macro" Attribute="Effects1" DMXFrom="0/1" ModeMaster="Body_Control.Control.Reset"
                        ModeFrom="200/1" ModeTo="255/1"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="13" Geometry="Body">
      <LogicalChannel Attribute="MyCustomAttr">
       <ChannelFunction Name="Custom" Attribute="MyCustomAttr" DMXFrom="0/1"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel DMXBreak="1" Offset="None" Geometry="Body">
      <LogicalChannel Attribute="Dimmer">
       <ChannelFunction Name="Virtual" Attribute="Dimmer" DMXFrom="0/1" Default="255/1" PhysicalFrom="0" PhysicalTo="100"/>
      </LogicalChannel>
     </DMXChannel>
    </DMXChannels>
   </DMXMode>
   <DMXMode Name="Reduced" Geometry="Body">
    <DMXChannels>
     <DMXChannel DMXBreak="1" Offset="1" Geometry="Beam">
      <LogicalChannel Attribute="Dimmer">
       <ChannelFunction Name="Dimmer 1" Attribute="Dimmer" DMXFrom="0/1" PhysicalFrom="0" PhysicalTo="1"/>
      </LogicalChannel>
     </DMXChannel>
    </DMXChannels>
   </DMXMode>
  </DMXModes>
 </FixtureType>
</GDTF>
)xml";
    return xml;
}

// A 4-cell RGB bar: every cell is a GeometryReference to the "Cell" geometry, with its own DMX offset.
// Slot 1 is a master dimmer on the body, the cells use slots 2..13.
inline std::string pixelBarXml() {
    std::string xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<GDTF DataVersion="1.2">
 <FixtureType Name="Pixel Bar 4" Manufacturer="Acme Lighting">
  <PhysicalDescriptions>
   <Emitters>
    <Emitter Name="Red" Color="0.68,0.31,22.3" DominantWaveLength="620"/>
   </Emitters>
  </PhysicalDescriptions>
  <Models>
   <Model Name="Housing" Length="0.4" Width="0.1" Height="0.08" PrimitiveType="Cube"/>
   <Model Name="CellModel" Length="0.1" Width="0.1" Height="0.02" PrimitiveType="Cube"/>
  </Models>
  <Geometries>
   <Geometry Name="Body" Model="Housing" Position=")xml";
    xml += gdtfPosition(0, 0, 0) + "\">\n";
    const double xs[4] = {-0.15, -0.05, 0.05, 0.15};
    for (int i = 0; i < 4; ++i)
        xml += std::format("    <GeometryReference Name=\"Cell {}\" Geometry=\"Cell\" Position=\"{}\">"
                           "<Break DMXOffset=\"{}\" DMXBreak=\"1\"/></GeometryReference>\n",
                           i + 1, gdtfPosition(xs[i], 0, -0.04), 2 + 3 * i);
    xml += R"xml(   </Geometry>
   <Geometry Name="Cell" Model="CellModel">
    <Beam Name="CellBeam" BeamType="Rectangle" BeamAngle="40" FieldAngle="50" BeamRadius="0.04" LuminousFlux="800" Position=")xml";
    xml += gdtfPosition(0, 0, -0.01) + R"xml("/>
   </Geometry>
  </Geometries>
  <DMXModes>
   <DMXMode Name="Pixel RGB" Geometry="Body">
    <DMXChannels>
     <DMXChannel Offset="1" Geometry="Body">
      <LogicalChannel Attribute="Dimmer">
       <ChannelFunction Name="Dimmer" Attribute="Dimmer" DMXFrom="0/1" PhysicalFrom="0" PhysicalTo="1"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel Offset="1" Geometry="CellBeam">
      <LogicalChannel Attribute="ColorAdd_R">
       <ChannelFunction Name="R" Attribute="ColorAdd_R" DMXFrom="0/1" Emitter="Red" PhysicalFrom="0" PhysicalTo="1"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel Offset="2" Geometry="CellBeam">
      <LogicalChannel Attribute="ColorAdd_G">
       <ChannelFunction Name="G" Attribute="ColorAdd_G" DMXFrom="0/1" PhysicalFrom="0" PhysicalTo="1"/>
      </LogicalChannel>
     </DMXChannel>
     <DMXChannel Offset="3" Geometry="CellBeam">
      <LogicalChannel Attribute="ColorAdd_B">
       <ChannelFunction Name="B" Attribute="ColorAdd_B" DMXFrom="0/1" PhysicalFrom="0" PhysicalTo="1"/>
      </LogicalChannel>
     </DMXChannel>
    </DMXChannels>
   </DMXMode>
  </DMXModes>
 </FixtureType>
</GDTF>
)xml";
    return xml;
}

// Zip archive with description.xml and optional extra files.
inline std::vector<std::uint8_t> makeGdtfArchive(const std::string& descriptionXml, bool withGobo = true,
                                                 bool withModel = true) {
    ZipWriter zip;
    zip.add("description.xml", descriptionXml);
    if (withGobo) zip.add("wheels/dots.png", makeGoboPng());
    if (withModel) zip.add("models/3ds/base.3ds", std::string_view("fake 3ds data"));
    return zip.finish();
}

}  // namespace dmxviz::fixtures::test
