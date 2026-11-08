#include "dusk/presentation.hpp"

#include "dusk/settings.h"

#include <borealis/presentation.hpp>
#include <aurora/lib/window.hpp>
#include <dolphin/gx/GXAurora.h>

#include <algorithm>

namespace dusk::presentation {
namespace {

float preferred_frame_rate() {
    if (getTransientSettings().turboMode) {
        return 0.0f;
    }

    switch (getSettings().game.enableFrameInterpolation.getValue()) {
    case FrameInterpMode::Off:
        return 30.0f;
    case FrameInterpMode::Capped:
        return static_cast<float>(std::max(getSettings().video.maxFrameRate.getValue(), 1));
    case FrameInterpMode::Unlimited:
    default:
        return 0.0f;
    }
}

}  // namespace

int aspect_ratio_mode() {
    const int mode = getSettings().video.aspectRatio.getValue();
    if (mode < 0 || mode > 2) {
        return getSettings().video.lockAspectRatio.getValue() ? 1 : 0;
    }
    return mode;
}

void apply_aspect_ratio() {
    const int mode = aspect_ratio_mode();
    // The game and touch transforms use this content framebuffer size.
    // Presentation fits it inside the current drawable without distorting it.
    AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);
    aurora::window::set_frame_buffer_aspect_ratio(
        mode == 1 ? 4.0f / 3.0f : mode == 2 ? 16.0f / 9.0f : 0.0f);
}

void update_frame_rate_preference() {
    borealis::presentation::set_preferred_frame_rate(preferred_frame_rate());
}

}  // namespace dusk::presentation
