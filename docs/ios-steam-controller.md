# Steam Controller on iOS

This branch uses SDL 3.4.16's native Bluetooth LE driver for the 2026 Steam
Controller (Triton). No controller bridge or additional app is required.

The iOS presets pin vendored SDL and enable joystick/HIDAPI support.
`game_main()` sets `SDL_HINT_JOYSTICK_HIDAPI_STEAM` before SDL initializes;
the iOS Bluetooth manager reads this hint only once. The app's plist includes
`NSBluetoothAlwaysUsageDescription`. Startup logs report the compiled/runtime
SDL versions and whether discovery is enabled.

Pair or make the controller discoverable before starting Dusklight, then accept
the Bluetooth permission prompt. SDL initially scans for about 20 seconds.
Restart the app if pairing happens after that scan. Check native gamepad input
rather than only mouse/keyboard events from lizard mode.

Native input has been tested on iPhone and iPhone Duo. Gyro, rumble, rear-button
mappings, reconnects and permission-denied behavior need separate validation.
For failures, collect the device/iOS version, controller firmware, permission
state and startup log. Distinguish discovery, device opening and game mapping.

Build with `ios-default` or `x-ios-ci`; see the repository build instructions.
The resulting device app requires Apple signing and a profile covering its
bundle ID and the destination phone. Signing credentials are not stored here.
