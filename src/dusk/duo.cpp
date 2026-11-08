#include "dusk/duo.hpp"
#include "dusk/duo_native.hpp"
#include "dusk/companion.h"
#include "dusk/dualscreen.h"
#include "dusk/settings.h"
#include "dusk/logging.h"
#include "dusk/main.h"
#include <aurora/aux_window.hpp>
#include <aurora/lib/window.hpp>
#include <SDL3/SDL.h>
#include <unordered_set>

namespace {
dusk::duo::Layout s_layout;
std::unordered_set<SDL_FingerID> s_panelFingers;
SDL_FingerID s_primaryFinger = 0;
bool s_primaryDown = false;
bool s_suppressTouchMouse = false;
bool s_touchMouseDown = false;
uint32_t s_lastPixelWidth = 0, s_lastPixelHeight = 0;

void cancelTouch() {
    dusk::companion::cancelTouches();
    s_primaryDown = false;
    // Keep swallowing these finger IDs through their release; a fold must not
    // turn the second half of an inventory drag into a game/menu touch.
}

bool filterInput(const SDL_Event& event) {
    // SDL may synthesize mouse events from the same touch. Consume the paired
    // mouse stream as well, without suppressing real mice or the gamepad.
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event.type == SDL_EVENT_MOUSE_MOTION) {
        const bool motion = event.type == SDL_EVENT_MOUSE_MOTION;
        const SDL_MouseID which = motion ? event.motion.which : event.button.which;
        if (which == SDL_TOUCH_MOUSEID) {
            const auto size = aurora::window::get_window_size();
            const float y = motion ? event.motion.y : event.button.y;
            const bool lower = s_layout.active() && size.height && y / size.height >= s_layout.gameHeight;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                s_touchMouseDown = true;
                s_suppressTouchMouse = lower;
            }
            const bool consume = s_suppressTouchMouse || (motion && !s_touchMouseDown && lower);
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                s_suppressTouchMouse = false;
                s_touchMouseDown = false;
            }
            if (consume) return true;
        }
    }
    switch (event.type) {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        cancelTouch();
        return false;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        break;
    default:
        return false;
    }
    const auto& finger = event.tfinger;
    if (event.type == SDL_EVENT_FINGER_DOWN && s_layout.active() &&
        finger.y >= s_layout.gameHeight) {
        s_panelFingers.insert(finger.fingerID);
        const bool inside = finger.x >= s_layout.x && finger.x < s_layout.x + s_layout.width &&
                            finger.y >= s_layout.y && finger.y < s_layout.y + s_layout.height;
        if (!s_primaryDown && inside) {
            s_primaryFinger = finger.fingerID;
            s_primaryDown = true;
            dusk::companion::touchEvent(0, (finger.x - s_layout.x) / s_layout.width,
                                          (finger.y - s_layout.y) / s_layout.height);
        }
        return true;
    }
    if (!s_panelFingers.contains(finger.fingerID)) return false;
    if (s_primaryDown && finger.fingerID == s_primaryFinger) {
        const int action = event.type == SDL_EVENT_FINGER_MOTION ? 1 :
                           event.type == SDL_EVENT_FINGER_UP ? 2 : 3;
        dusk::companion::touchEvent(action, (finger.x - s_layout.x) / s_layout.width,
                                           (finger.y - s_layout.y) / s_layout.height);
        if (action >= 2) s_primaryDown = false;
    }
    if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)
        s_panelFingers.erase(finger.fingerID);
    return true;
}
}

namespace dusk::duo {
void update() {
    SDL_Window* window = aurora::window::get_sdl_window();
    if (!window) return;
    static bool attached = false;
    if (!attached) {
        attached = attach_observer(window);
        aurora::window::set_input_filter(filterInput);
        aurora::window::set_post_poll_layout_update(update);
    }
    const Metrics metrics = native_metrics();
    const auto next = calculate(metrics,
                                getSettings().game.dualScreen.getValue() &&
                                    (dusk::IsGameLaunched || companion::hudReady()),
                                getSettings().game.duoAutomaticSplit.getValue());
    const auto size = aurora::window::get_window_size();
    if (next == s_layout && size.native_fb_width == s_lastPixelWidth &&
        size.native_fb_height == s_lastPixelHeight) return;
    cancelTouch();
    s_layout = next;
    s_lastPixelWidth = size.native_fb_width;
    s_lastPixelHeight = size.native_fb_height;
    const auto x = static_cast<uint32_t>(std::lround(next.x * size.native_fb_width));
    const auto y = static_cast<uint32_t>(std::lround(next.y * size.native_fb_height));
    const auto right = std::min(size.native_fb_width,
        static_cast<uint32_t>(std::lround((next.x + next.width) * size.native_fb_width)));
    const auto bottom = std::min(size.native_fb_height,
        static_cast<uint32_t>(std::lround((next.y + next.height) * size.native_fb_height)));
    aurora::auxwin::set_embedded_region(x, y, right - x, bottom - y);
    aurora::window::set_content_height_fraction(next.gameHeight);
    dualscreen::setDisplayAvailable(next.active());
    DuskLog.info("Duo companion: active={} automatic={} hinge={} partial={} view={}x{} panel={}x{}",
        next.active(), getSettings().game.duoAutomaticSplit.getValue(), metrics.hingeAvailable,
        metrics.partiallyOpen, metrics.width, metrics.height, right - x, bottom - y);
}
} // namespace dusk::duo
