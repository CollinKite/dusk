#pragma once

namespace dusk::presentation {

void update_frame_rate_preference();

// The saved legacy 4:3 toggle is honored until an explicit mode is selected.
int aspect_ratio_mode();
void apply_aspect_ratio();

}  // namespace dusk::presentation
