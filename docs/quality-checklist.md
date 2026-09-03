# Quality Checklist & Review Resolution

Result of the thermo-nuclear code-quality review (2026-09-02) and the fixes
landed through 2026-09-03. Each P0/P1 finding is listed with its resolution
and the commit(s) that closed it.

## Review findings → status

| Finding | Resolution | Commits |
|---|---|---|
| P0-1 dead `media_backend.cpp` (3932 lines, zero callers) | Deleted | a69b342 (replay of 057b2c2) |
| P0-2 media three-layer forwarding + reverse dep | Real impls moved into `media_shared.cpp`; `media_video_shared_*` bridge deleted | a69b342 |
| P0-3 giant files: `assets.cpp` 3011 / `media_video.cpp` 3864 | `assets.cpp` split (filter/parse/internal); `media_video` shrunk to video-only ~866 | Phase 3 commit set; a69b342 |
| P0-4 god functions | `process_track_folder` is now the sole ~700-line orchestration fn in `assets.cpp`; `run_compile_assets` ~396; `compile_chart` split into `SlideChainBuilder` (~103-line orchestrator) | 63d1408 |
| P0-5 duplicated helpers (`to_int`×4, `trim_copy`×2, `file_non_empty`×3) | Canonical versions in `io.hpp`; private copies removed | 1b248b0 |
| P0-6 duplicated root-priority pick logic | `is_more_preferred`/`root_pick_key` shared | 6198ff5 |
| P1-1 style drift / no clang-format | `.clang-format` (LLVM) added, whole tree reformatted, CI format job | 060bc5a, 01a0906 |
| P1-2 `AssetsOptions` 25-field god struct | Deliberately retained — flat option bag is idiomatic C++; splitting into nested structs was judged a fake simplification | — (decision note) |
| P1-3 CLI duplicated core semantics | `trim_copy`/filter parsing removed from CLI; `run_and_report` centralizes error handling | 1b248b0, 5bbdc07 |
| P1-4 `zip_and_remove` empty stub (`--zip` always failed) | Implemented STORE-method zip (`zip_folder_and_remove`) + unit tests | a69b342 |
| P1-5 media_cover global mutex bottleneck | Mutex + 3-retry removed after concurrency testing (unique temp paths + no shared UABE state) | ff8fa1c |
| P1-6 thin chart/ma2 test coverage | +13 chart/ma2 cases (103 total); media_shared byte-order/probe tests added | 7d64ed7, 96b4ad1 |
| (wish) C++23 adoption | Standard bumped; version catalog single-sourced with constexpr | e4b0cfc, b5a78bc |

## Acceptance bar used per refactor

- Build clean under GCC 16.1 with `-Wall -Wextra -Wpedantic -Wconversion`.
- Full test suite green: **109 test cases / 633 assertions** (was 87/455 at
  review time).
- `git diff -w` equals full diff for pure-relocation moves (zero whitespace
  noise).
- Behavior preserved: tests are the executable spec; no "fixing while moving".
- CI (ubuntu/macos/windows build+test) green for pushed history.

## Enforcement

- Pre-commit hook formats staged first-party C/C++ via `.clang-format`.
- CI `format` job fails on any unformatted first-party source.
- C++23 is required (`CMAKE_CXX_STANDARD 23`).
