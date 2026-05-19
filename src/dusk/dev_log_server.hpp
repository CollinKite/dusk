#pragma once

#include <SDL3/SDL_events.h>

// Developer log server.
//
// Captures the app's log output into an in-memory ring buffer and serves a
// live, filterable web view of it over the local network. Built into the iOS
// and tvOS builds, where attaching a console to a sideloaded app is awkward.
//
// All functions are safe to call from any thread.

namespace dusk::dev_log {

// Records one log line into the ring buffer. `level` uses the AuroraLogLevel
// scale (0 = debug ... 4 = fatal).
void record(int level, const char* module, const char* message, unsigned int len);

// Logs notable controller input (gamepad buttons, axis thresholds, hotplug)
// to the "dusk.input" channel so it can be watched and filtered on its own.
void note_sdl_event(const SDL_Event& event);

// Starts the HTTP log server on a background thread (no-op if already up).
// Also routes SDL's own log output into the ring buffer.
void start();

// Stops the server and joins its thread. Safe to call when stopped.
void stop();

}  // namespace dusk::dev_log
