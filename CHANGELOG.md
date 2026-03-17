# Changelog

## v0.0.4 - Assets/Simai Stability & Build/CI Cleanup

### Added

- core/assets: merged and streamlined assets implementation path, reducing split-implementation drift.
- ci: added executable permission restore step for Linux/macOS jobs.

### Changed

- build/core: adjusted UABE/CMake include path handling for more stable cross-module compilation.
- tests: improved structure and readability of assets/media related unit tests.
- ci: restructured workflow into clearer build/test stages for better troubleshooting and maintenance.

### Fixed

- assets: fixed version normalization behavior in export metadata/layout flow.
- ma2/simai: fixed incomplete MA2 composition and Simai parsing issues.
- simai: corrected slide-chain serialization expectations to align with actual compose behavior.

### Build & CI

- ci/todo: refined TODO planning and CI responsibilities to match current pipeline execution.

### Test
- **Full Test passed on real assets**