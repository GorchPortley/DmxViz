#pragma once
// ContentEditors: the inspector widgets for each kind of node content.
//
// Every function draws ImGui widgets that edit a *copy* of the content and
// reports changes to the EditSession, which turns them into one undo step (see
// EditSession.h). They never touch the scene themselves.

#include "fixtures/FixtureLibrary.h"
#include "stage/NodeContent.h"
#include "ui/EditSession.h"

namespace dmxviz::ui::editors {

// Albedo (sRGB picker), roughness, metallic, emissive colour + strength.
void material(EditSession& edit, const char* title, Material& material);

void primitive(EditSession& edit, stage::PrimitiveContent& content);
void truss(EditSession& edit, stage::TrussContent& content);
void stageDeck(EditSession& edit, stage::StageDeckContent& content);
void steps(EditSession& edit, stage::StepsContent& content);
void wall(EditSession& edit, stage::WallContent& content);
void referenceFigure(EditSession& edit, stage::ReferenceFigureContent& content);
void cameraPreset(EditSession& edit, stage::CameraPresetContent& content);
// Import settings of a model (axis and unit); the file path is handled by the inspector.
void modelSettings(EditSession& edit, stage::ModelContent& content);
void fixture(EditSession& edit, const fixtures::FixtureLibrary& library, stage::FixtureContent& content);

}  // namespace dmxviz::ui::editors
