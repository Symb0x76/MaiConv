# MaiConv Architecture

Refactor snapshot: 2026-09-03 (post structural refactor). First-party C/C++ is
~9.5k lines across `src/core`, `src/cli`, `include/maiconv`. Vendored trees
(`src/uabe`, `include/uabe`, `src/lz4`, `third_party`) are not first-party.

## Module map (first-party sources)

| Source | Responsibility |
|---|---|
| `include/maiconv/core/assets.hpp` | Public API: `AssetsOptions`, `run_compile_assets` |
| `include/maiconv/core/assets_internal.hpp` | Internal shared types: `TrackInfo`, `NumericFilterSet`, `VersionFilterSet`, `AssetIndex`, inline `path_to_utf8`/`path_to_generic_utf8`, cross-file fn decls |
| `include/maiconv/core/chart.hpp` | Domain model: `Chart`, `Note`, `BpmChange`, `MeasureChange` |
| `include/maiconv/core/format.hpp` | Enums: `ChartFormat`, `FlipMethod`, `SpecialState`, `NoteType` |
| `include/maiconv/core/io.hpp` | Canonical string/file helpers: `trim`, `lower`, `to_int`, `to_double`, `file_non_empty`, `pad_music_id`, `sanitize_folder_name`, path<->utf8 |
| `include/maiconv/core/ma2.hpp` | MA2 tokenizer/parser/composer API |
| `include/maiconv/core/simai/*` | simai tokenizer/parser/compiler API |
| `include/maiconv/core/media/*` | audio/cover/video/shared media API |
| `include/maiconv/core/zip_util.hpp` | `zip_folder_and_remove` |
| `src/core/assets.cpp` | Orchestration: `process_track_folder`, `run_compile_assets`, output/report/compose helpers (~1950 lines) |
| `src/core/assets_filter.cpp` | Version catalog + filter compilation (`compile_*_filters`, `matches_version_filter`) |
| `src/core/assets_parse.cpp` | `Music.xml` parsing + cache, asset-index build/cache, difficulty inference |
| `src/core/chart.cpp` | `Chart` time/duration math, normalize, rotate, shift |
| `src/core/io.cpp` | Canonical helper implementations |
| `src/core/ma2.cpp` | MA2 line parser + composer |
| `src/core/media_shared.cpp` | Shared media infra: ffmpeg wrappers, UTF/ACB/AFS2/HCA parsers, temp workspace pool, byte-order readers |
| `src/core/media_video.cpp` | Video-only: USM/DAT<->MP4, VP9/H264 stream handling |
| `src/core/media_audio.cpp` | Audio transcode entry points |
| `src/core/media_cover.cpp` | Unity `.ab` texture -> PNG |
| `src/core/simai_compiler.cpp` | simai composer: `SlideChainBuilder` + bar serialization |
| `src/core/simai_parser.cpp` / `simai_tokenizer.cpp` | simai parse pipeline |
| `src/core/unity_assetbundle.cpp` | Texture decode from Unity AssetBundle (UABE) |
| `src/core/zip_util.cpp` | STORE-method zip writer |
| `src/cli/main.cpp` | CLI11 subcommands; per-command handlers wrapped by `run_and_report` |

## Key design decisions

1. **Media shared layer owns real implementations.** `media_shared.cpp` holds
   the ffmpeg/ACB/AFS2/HCA machinery behind a `media_shared_*` bridge; the old
   `media_shared_* -> media_video_shared_* -> anonymous-ns` three-layer chain
   was deleted. `media_video.cpp` is video-only (~866 lines today).
2. **assets.cpp split by cohesion.** Orchestration stayed in `assets.cpp`;
   version/filter logic went to `assets_filter.cpp`; XML/index parsing to
   `assets_parse.cpp`. Shared types live in `assets_internal.hpp`.
3. **Slide-chain state machine is a real class.** `SlideChainBuilder`
   (simai_compiler.cpp) owns the open-chain maps and per-note render pass;
   `compile_chart` is a thin ~100-line orchestrator.
4. **Canonical helpers in `io.hpp`.** `to_int`/`to_double`/`trim`/`lower`/
   `file_non_empty` have exactly one implementation; private copies were
   removed from ma2/simai/assets/cli.
5. **Single exit-code/error boundary in CLI.** `run_and_report(fn)` owns the
   try/catch + uniform stderr prefix; 9 handlers delegate to it.
6. **One constexpr version catalog.** id<->name relations (incl. deluxe
   aliases) are defined once in `assets_filter.cpp`; no dual hand-maintained
   unordered_maps.
7. **zip export is real.** `zip_folder_and_remove` writes a STORE-method zip
   and is unit-tested (Python `zipfile` cross-validated).

## Formatting / toolchain

- `.clang-format` at repo root pins LLVM style. Pre-commit hook formats staged
  first-party C/C++; CI runs `clang-format --dry-run --Werror` (job `format`).
- C++23 (`CMAKE_CXX_STANDARD 23`). GCC 16.1 locally; CI validates GCC/Clang/MSVC.
- Build presets: `default` (ninja+gcc/clang), `msvc`, `asan`.

## Layout conventions

- `category_folder` for assets export is derived per `--layout`
  (`flat|genre|version`); per-track output goes under it.
- Track media resolution builds candidate file names (`music<id>.mp3`,
  `UI_Jacket_<id>.png`, ...) against per-root asset indexes; higher root
  priority wins with path tie-break (see `is_more_preferred`/`root_pick_key`).
