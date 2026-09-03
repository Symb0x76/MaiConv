# C++23 Migration Notes

Documented 2026-09-03, after the structural refactor completed. Records what
was adopted, what was deliberately skipped, and why — so a future reader does
not re-litigate the same decisions.

## Baseline change

`CMAKE_CXX_STANDARD`: 20 → 23 (commit `e4b0cfc`). No source edits needed;
GCC 16.1 builds clean, full suite passes, CI (GCC/Clang/MSVC) validates on push.

## Adopted

| Feature | Where | Why / commit |
|---|---|---|
| C++23 standard | root CMakeLists | Foundation for everything below (`e4b0cfc`) |
| `constexpr` array version catalog | `assets_filter.cpp` | Removed two hand-maintained `unordered_map` literals (26 + 28 entries) that could drift apart; single `VersionEntry{id,name,alias}` table + linear lookup. Alias (`deluxe`/`deluxeplus`) semantics preserved (`b5a78bc`) |

## Deliberately NOT adopted (with rationale)

These were evaluated against the review bar "deletes complexity, does not just
rearrange it" and rejected:

- **`std::format` / `std::print` for logging/JSON**: The existing code builds
  strings with `+=` on `std::string` (efficient, no allocation churn) or short
  `std::ostringstream` blocks. Replacing them with `std::format(...)` is a
  syntax change that adds a format-string compilation surface and a
  `std::format_error` path, without reducing the number of concepts a reader
  holds. `std::print` additionally requires MSVC ≥ 19.38 and only replaces
  `std::cout <<` (already fine). Not worth a churn-only diff.
- **`std::expected<T,E>`**: `TrackProcessResult` is a genuine result struct
  with many fields (warnings, timing, side-car outputs); a monadic `expected`
  wrapper would push those into `error`/`value` branches and make the 
  `emit_*` consumers more convoluted, not less.
- **`std::jthread` + stop_token**: the existing worker pool is a flat
  `atomic<size_t>` index + `vector<thread>` with a stop flag; there is no
  cooperative-cancellation need. `jthread` would only rename the primitives.
- **`std::ranges` pipelines**: candidate-name generation is already expressed
  with small named helpers (`append_suffix_candidates`, ...). Piping the same
  loops would inline them into call sites and hurt readability.
- **`std::mdspan` / `std::span` in texture decode**: the UABE texture path
  works on `std::vector<uint8_t>` + explicit stride math that mirrors the
  on-disk layout; a view type adds a layer without changing the invariants.
  `unity_assetbundle.cpp` is vendored-adjacent glue.
- **deducing this / `std::flat_map` / `std::inplace_vector` / C++26**: either
  MSVC support is incomplete (deducing this) or the payoff is marginal.
  Contracts/reflection are unavailable across the CI matrix and rejected.

## Decision rule used

For each candidate feature: "does this remove a moving part, or just rename
one?" Adopt only the former. The C++23 bump plus the single-source version
catalog both remove moving parts (two tables that could disagree). Everything
else on the earlier wish-list was a rename and was skipped to keep the diff
honest.

## CI matrix note

Local builds use MinGW GCC 16.1. The CI matrix is ubuntu-latest (GCC),
macos-latest (Clang), windows-latest (MSVC) — all default 2026 runners. A
feature is only "adopted" when it compiles on all three; any future C++23+
adoption must land behind a `push` CI run that goes green on all three before
it is considered done.
