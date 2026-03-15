# Changelog

## v0.0.3 - Audio/Video Conversion Improvements

### Added

- assets: support comma-separated and regex filters for `--id` and `--difficulty`.
- assets: add `--types` to export selected outputs only (`maidata.txt`/`track.mp3`/`bg.png`/`pv.mp4`).
- assets/media: improve media fallback handling, including better jacket fallback behavior and stricter incomplete output marking.
- media: improve ACB/AWB parsing and HCA decode flow to reduce noisy audio decode fallbacks.
- media: expand video conversion path support and conversion strategy handling across DAT/USM/CRID inputs.

### Changed

- cli: enable unit buffering for stdout/stderr for more predictable command output behavior.
- ffmpeg/tooling: streamline ffmpeg setup and add `--gpu` flow improvements in CLI-facing behavior.
- media: remove legacy template-based `mp4 -> dat` path in favor of the built-in conversion pipeline.

### Fixed

- assets: ensure incomplete track progress is logged before failing when `--ignore` is not enabled.
- assets: fix missing version mapping and improve export folder creation fallback logic.

### Build & CI

- build: remove unused `vgmstream` dependency.
- release workflow: improve checkout strategy with submodules and fetch-depth handling.
