# Changelog

All notable changes to this project are documented in this file.

## v0.0.2 - Performance Improvements

### Added

- assets: skip processing for reserved music IDs.
- tests: add coverage for exporting specific music ID and fix related test case.
- release workflow: add dynamic changelog preparation for GitHub Releases.

### Changed

- assets: normalize genre and version layout in asset compilation.
- assets: remove redundant chart converter tool information from maidata document.
- cmake: remove obsolete CMakeLists.txt for vgmstream.
- ci/docs/tests: expand end-to-end coverage for libav and ffmpeg fallback paths.

### Performance

- assets: refactor asset candidate finding logic with hash map for faster lookup.

### Fixed

- assets: update genre mapping to use `&` instead of `＆`.
- media: use global environment for Unix ffmpeg spawn helpers.
