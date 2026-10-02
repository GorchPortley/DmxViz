#pragma once
// Renderer quality/performance tunables. Unlike Environment (which belongs to
// the show and is saved with it), these are per-machine settings: the UI can
// expose them in a "Render settings" panel, the sandbox sets them from flags.
//
// OWNER: render work stream (WS3).

namespace dmxviz::render {

enum class Tonemapper {
    AcesHuePreserving,  // ACES-fit curve applied to the brightest channel: saturated beams stay saturated
    AcesPerChannel,     // classic per-channel ACES fit (bright colours drift to white)
};

struct RenderSettings {
    // --- volumetric beams ---------------------------------------------------------
    bool volumetrics = true;
    int minMarchSteps = 6;          // per beam per pixel; small/distant beams use the minimum
    int maxMarchSteps = 20;         // upper bound for beams that cover a lot of the screen
    float marchPixelsPerStep = 6.0f;// one step per this many half-res pixels of on-screen beam length
    float hazePhaseG = 0.7f;        // forward lobe of the haze phase function (Henyey-Greenstein g, 0..0.95);
                                    // 30 % of the scattering is isotropic on top of it

    // --- beam extent (applies to surface lighting and haze) -----------------------
    float maxBeamLength = 60.0f;    // m, hard cap on the length of a beam volume
    float minIlluminance = 0.5f;    // lux; a beam ends where its axis illuminance drops below this
    bool clipBeamsAtFloor = true;   // the y = 0 floor stops beams (cheap stand-in until beam shadows exist)

    // --- post ------------------------------------------------------------------------
    bool lensGlow = true;
    bool bloom = true;
    Tonemapper tonemapper = Tonemapper::AcesHuePreserving;
};

}  // namespace dmxviz::render
