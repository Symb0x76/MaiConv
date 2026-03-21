# Changelog

## v0.0.5 - Utage Handling & Simai Rendering Improvements

### Added

- assets: introduced Utage L/R chart split workflow and output naming support.
- tests/assets: added unit tests covering Utage L/R related export behavior.

### Changed

- simai/parser: refactored parsing flow to track source bar count and improve slide notation handling consistency.
- simai/compiler: reworked slide rendering logic with compact note handling and better internal state transitions.
- docs: updated `README.md`, `README_CN.md`, and `TODO.md` to reflect the new Utage/Simai behavior and roadmap state.

### Fixed

- assets/utage: adjusted post-split behavior to keep L/R charts in a single output with corrected difficulty handling.
- tests/simai: aligned expectations for updated Utage/Simai behavior to prevent regression drift.