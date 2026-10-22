# WWHD UWP

UWP adaptation of [ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp) for Xbox Developer Mode.

**Playable at 30 FPS on Xbox Series S, based on user testing.**

**This port is experimental. Work is ongoing to improve performance and optimize the runtime.**

## Features

- Select your WUX/WUD from any folder on USB.
- Extract, translate, compile and verify your own game directly on the console. Prepared files remain on USB and are reused on later launches.
- Upstream graphics, controller, audio and gameplay settings.
- Vulkan rendering through Mesa Dozen / Direct3D 12 and UWP input, storage and lifecycle support.

## Installation

Download the ZIP from [Releases](https://github.com/JYRS20/WWHD-UWP/releases), install the APPX through Xbox Device Portal and set the application to **Game** in Dev Home.

Keep your original USA WUX/WUD, matching disc `.key` and `common.key` together on USB. Allow at least 3 GiB of additional free space. Choose **Browse USB**, select the image and keep the drive connected during preparation and gameplay. Later, choose **Play game**.

## Source and credits

This repository is a source overlay for upstream commit `f10b56621ed13e0053e7cf8190e740834377e45a`. It includes the UWP adaptation and embedded preparation hosts. Public upstream tooling and external dependencies are required to build it.

No game images, keys, generated game instructions, private certificates or personal automation are included. Game translation and compilation occur locally from the user's copy.

See [CREDITS.md](CREDITS.md). Runtime source is licensed under [MPL-2.0](LICENSE); third-party licenses remain applicable.
