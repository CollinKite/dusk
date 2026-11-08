#pragma once
#include <SDL3/SDL_platform_defines.h>

namespace dusk::duo {
#ifdef SDL_PLATFORM_IOS
void update();
#else
inline void update() {}
#endif
}
