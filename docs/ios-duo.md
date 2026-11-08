# iPhone Duo support

This branch integrates native Steam Controller support, resize fixes and an
optional map/items companion into Dusklight. It is not a separately loaded mod.

## Settings

- **Video > Screen Aspect Ratio:** Fill Screen, 4:3 or 16:9.
- **Gameplay > Companion Screen:** Off (default), Map + Items or Full HUD.
- **Automatic Fold Split:** off by default. Leave it off for fully unfolded
  portrait; enable it to require a partially open hinge.

The companion places the game above the horizontal division and map/inventory
below it. Landscape, a closed hinge or insufficient space restores the full game.
The launcher stays full-window. Touch the lower tabs to change pages; drag an
owned inventory item to an item slot to equip it.

## Build

Use Xcode 27.1+, CMake 3.25+, Ninja, Python and stable Rust on macOS:

```sh
git submodule update --init --recursive
rustup target add aarch64-apple-ios
cmake --preset ios-duo -B build/ios-duo \
  -DRust_TOOLCHAIN=stable \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/ios-duo/install"
cmake --build build/ios-duo --target install --parallel 6
```

Use a fresh build directory when changing SDK, deployment target or dependency
versions. The simulator preset is `ios-duo-simulator` and needs Rust's
`aarch64-apple-ios-sim` target; native mods are disabled there.

CMake applies `cmake/patches/aurora-duo.patch` to the pinned Aurora checkout.
Repeated configuration is safe. If the patch conflicts with local dependency
edits, configuration stops without resetting them. A fresh recursive checkout
is the simplest way to switch between patched and upstream branches.

The GitHub **iPhone Duo** workflow builds an unsigned IPA and matching SDK.
Download its artifact and sign the IPA with your own Apple identity/profile or
sideloading tool. Native libraries in `Frameworks` also need signing. No game
image, save data, provisioning profile or signing key is included.

## Native mods

The extra item slots change game layouts and button masks. This branch uses
game-service ABI epoch 3, so native mods with `FEATURES game` must be rebuilt
against its SDK. Bundled mods are rebuilt with the app. Asset-only and
service-only mods do not require the game ABI. Do not mix game SDKs from forks
based only on their version number.

## Implementation and validation

SDL 3.4.16 provides native Steam Controller BLE. The early Steam hint and Bluetooth
usage description are documented in [ios-steam-controller.md](ios-steam-controller.md).
A targeted SDL UIKit patch uses current window bounds instead of a legacy global
orientation workaround. Aurora observes scene-local view bounds, display scale,
safe areas and hinge updates. The companion shares the existing Metal surface.
Layout updates after event polling; stale render rectangles are rejected, and
resize/focus changes cancel active touches.

Controller input and repeated folding/rotation were confirmed on an iPhone Duo.
The companion passed simulator checks for portrait layout, tab touches,
landscape/closed fallback, reopening, automatic hinge switching and closed startup.
Actual item equipping, holding a touch through a resize, Full HUD mode and
controller coexistence still need device testing. Report the commit, SDK/device
versions, controller firmware, settings and relevant app logs with failures.

## Credits

The companion is adapted from [igawa6/dusklight, dual-screen-v2](https://github.com/igawa6/dusklight/tree/edaf1f405ee8477d2074c36a3fbf0a32aade75c0)
(CC0-1.0) and [igawa6/aurora](https://github.com/igawa6/aurora/tree/d5d8939c367ef709d1b023d915ea2fc929be42bc)
(MIT). Existing license notices remain. The inherited experimental phone-network
feature is compiled out by default; it is not needed for the iOS companion.
