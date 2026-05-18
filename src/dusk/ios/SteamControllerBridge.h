#pragma once

// Bridges a Bluetooth LE Steam Controller to SDL as a virtual gamepad.
//
// iOS only; implemented in SteamControllerBridge.mm and compiled in when the
// DUSK_STEAM_CONTROLLER definition is set by the build.

#ifdef __cplusplus
extern "C" {
#endif

// Begins searching for a Steam Controller. Once one connects, it is presented
// to SDL as a standard virtual gamepad and picked up by the normal input path.
// Call once, after SDL has been initialized.
void DuskSteamControllerStart(void);

// Detaches the virtual gamepad and ends the Bluetooth session.
void DuskSteamControllerStop(void);

#ifdef __cplusplus
}
#endif
