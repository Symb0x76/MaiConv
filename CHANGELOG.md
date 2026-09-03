# Changelog

## Unreleased - Structural Refactor & C++23

### Changed

- core: deleted dead `media_backend.cpp`; moved shared media implementations into `media_shared.cpp` and removed the three-layer forwarding chain (`media_shared_*` → `media_video_shared_*` → anonymous namespace).
- core: split `assets.cpp` by cohesion into `assets_filter.cpp` (version/filter logic) and `assets_parse.cpp` (Music.xml parsing + asset index), sharing types through `assets_internal.hpp`.
- core: extracted the slide-chain state machine from `compile_chart` into a `SlideChainBuilder` class; `compile_chart` is now a thin orchestrator.
- core: consolidated duplicated string/file helpers (`to_int`, `to_double`, `file_non_empty`, `trim`) into `io.hpp`.
- cli: centralized per-command error handling in `run_and_report`.
- build: raised the project standard to C++23; added `.clang-format` (LLVM) and a CI format check.
- assets: replaced dual hand-maintained version maps with a single `constexpr` version catalog.
- media: removed an obsolete global decode mutex that throttled parallel `.ab` cover exports.

### Added

- assets: implemented `--zip` export (STORE-method zip writer) with unit tests.
- tests: broadened chart and ma2 coverage; added deterministic unit tests for media byte-order readers and mp3/file probing.

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