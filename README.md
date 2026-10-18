# WWHD UWP

UWP adaptation of [ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp) for Xbox Developer Mode.

**Playable at 30 FPS on Xbox Series S, based on user testing.**

## Work included

- UWP application lifecycle, local storage and direct access to extracted game folders on USB.
- Vulkan rendering through Mesa Dozen / Direct3D 12, with CoreWindow presentation.
- Xbox controller mapping fixes and integration of the upstream settings and audio systems.
- Reduced guest-memory commitment, AVX2 texture checks and bounded vertex snapshot reuse.
- Performance diagnostics and regression tests for data integrity and GPU resource lifetime.

## Distribution

This is a source-only adaptation based on upstream commit `f10b56621ed13e0053e7cf8190e740834377e45a`. Use it as an overlay on that upstream source tree; upstream build and game-code generation tooling is not bundled here.

No game dumps, generated game code, encryption keys, credentials or automation scripts are included. The current personal-test APPX is excluded because it contains locally generated game code. A redistributable APPX requires a separate local game-code build workflow.

## Credits

See [CREDITS.md](CREDITS.md). Runtime source is licensed under [MPL-2.0](LICENSE); third-party notices remain with their respective source files.
