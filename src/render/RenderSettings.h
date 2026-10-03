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

// Size of the volumetric beam target relative to the viewport. The haze pass shades every pixel of every
// beam hull, so its cost is proportional to the number of volume pixels: quarter = 4x cheaper than half.
enum class VolumetricResolution {
    Half,     // 1/2 width and height (default)
    Quarter,  // 1/4 width and height: for slow GPUs and very large rigs, softer haze
};

struct RenderSettings {
    // --- volumetric beams ---------------------------------------------------------
    bool volumetrics = true;
    VolumetricResolution volumetricResolution = VolumetricResolution::Half;
    int minMarchSteps = 6;          // per beam per pixel; small/distant beams use the minimum
    int maxMarchSteps = 20;         // upper bound for beams that cover a lot of the screen
    float marchPixelsPerStep = 6.0f;  // one step per this many volume-target pixels of on-screen beam length
    float volumetricBudget = 24.0f;  // cost cap in millions of (volume pixel x march step) per view and frame; when the
                                     // beams on screen would cost more, the step counts shrink (down to 3, even
                                     // below minMarchSteps; 0 = no cap). About 5 ms of haze on a GTX 1060.
    float hazePhaseG = 0.7f;        // forward lobe of the haze phase function (Henyey-Greenstein g, 0..0.95);
                                    // 30 % of the scattering is isotropic on top of it

    // --- automatic quality ---------------------------------------------------------
    // Lowers the march steps and then the haze resolution while frames take longer than targetFrameMs, and
    // raises them again when the frames fit (AutoQuality.h). Off by default so that renders are reproducible.
    bool autoQuality = false;
    float targetFrameMs = 16.7f;  // frame time to stay under (16.7 = 60 fps, 33.3 = 30 fps)

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
