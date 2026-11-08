#pragma once
#include "dusk/duo_layout.hpp"
struct SDL_Window;
namespace dusk::duo {
Metrics native_metrics();
bool attach_observer(SDL_Window* window);
}
