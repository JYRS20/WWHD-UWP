# Changelog

## 0.1.2

- Unified USB selection, extraction, translation, compilation, verification and game launch.
- Reuse of completed preparations without recompiling the game.
- Folder browsing and a new English interface with staged progress, prepared for 1080p in 16:9.
- Dynamic loading of verified game objects from USB; the APPX contains no generated game instructions.

Playable at 30 FPS on Xbox Series S, based on user testing. An isolated graphics-driver startup crash has been reported.

## 0.0.2.13

- Added bounded vertex snapshot reuse with exact byte comparisons and submission-generation checks.
- Retained AVX2 texture checks, reduced guest-memory commitment and corrected Xbox controller mappings.
- Added CPU, texture, upload and GPU timing diagnostics.

30 FPS gameplay has been reported on Xbox Series S. This source release contains no installable APPX.
