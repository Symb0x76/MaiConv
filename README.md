# MaiConv

[CN](./README_CN.md) | EN

Cross-platform C++ reimplementation of [MaichartConverter](https://github.com/Neskol/MaichartConverter).

## TODO

- [ ] Implement local lz4 decompression in replacement of Unity's LZ4 library (currently used via UABE code, which is only used for ACB+AWB decoding and is not performance-critical)
- [ ] Add png/mp3/mp4 -> ab/awb+acb/dat asset export

## Features

- C++20 + CMake + git submodule (runtime deps in third_party, Catch2 via FetchContent for tests)
- CLI subcommands:
  - `maiconv ma2`
  - `maiconv simai`
  - `maiconv assets`
  - `maiconv media`
- Core pipeline: Tokenizer -> Parser -> Chart(AST) -> Composer
- Note transforms: rotate + tick shift
- Three-platform CI: Windows / Linux / macOS

## Dependency Layout

- `third_party/*`: runtime third-party dependencies are managed by git submodules.
- Test dependency `Catch2` is fetched by CMake FetchContent when `MAICONV_BUILD_TESTS=ON`.
- Current submodules: `CLI11`, `tinyxml2`, `vgmstream`, `shine`.

## Build

```bash
git submodule update --init --recursive
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## FFmpeg Dependency Details

Video-related `maiconv media` features now support two backends:
- In-process `libav` backend (preferred): enabled when `MAICONV_ENABLE_LIBAV_TRANSCODE=ON` and FFmpeg development libraries are detected at configure time.
- External `ffmpeg` backend (fallback): used when in-process `libav` is unavailable.

Default build behavior:
- `MAICONV_ENABLE_LIBAV_TRANSCODE=ON` (auto-detect).
- If detection fails, MaiConv falls back to external `ffmpeg` and still works.

Build examples:

```bash
# default (auto-detect in-process libav)
cmake --preset default

# force fallback backend (no in-process libav)
cmake --preset nolibav
# or: cmake -S . -B build/nolibav -G Ninja -DMAICONV_ENABLE_LIBAV_TRANSCODE=OFF
```

External `ffmpeg` executable requirements (fallback backend):
- `ffmpeg` should be available in `PATH`, or set `MAICONV_FFMPEG` to an absolute executable path.
- `ffprobe` is optional (useful for manual diagnostics).

Required capabilities by feature:
- `dat/usm -> mp4`: requires `libx264` encoder (MaiConv transcodes VP9 IVF into H.264 MP4)
- `mp4 -> dat` (both template and template-free paths): requires `libvpx-vp9` encoder (MaiConv first transcodes to VP9 IVF)

Quick checks (Windows PowerShell):

```powershell
ffmpeg -version
ffmpeg -hide_banner -encoders | Select-String "libx264|libvpx-vp9"
```

Quick checks (Linux/macOS):

```bash
ffmpeg -version
ffmpeg -hide_banner -encoders | grep -E "libx264|libvpx-vp9"
```

If encoders are missing, install an ffmpeg build that includes both `libx264` and `libvpx`.

## CLI

### ma2

```bash
maiconv ma2 --input /path/to/sample.ma2 --format simai --output ./out
```

### simai

```bash
maiconv simai --input /path/to/maidata.txt --difficulty 3 --format ma2 --output ./out
```

### assets

Export all tracks from StreamingAssets:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --layout flat
```

Export one id (all difficulties):

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --id 363 --layout flat
```

Export one id with one difficulty:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --id 363 --difficulty 3 --layout flat
```

Export `maidata.txt` with display levels:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --format maidata --display
```

Resume export and skip already completed tracks:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --layout flat --resume
```

Rules:
- when `--id` is omitted: export all tracks
- when `--id` is provided and `--difficulty` is omitted: export all difficulties for that id
- when both are provided: export only the selected difficulty
- `--difficulty` uses exported `maidata` numbering: standard charts are usually `2..6`, utage is `7`
- `--resume` (`--skip-existing`) skips tracks that already have complete exports, while keeping `_Incomplete` tracks eligible for retry

`assets` auto-detects media folders from `StreamingAssets` and its first-level subdirectories:
- audio: `SoundData`
- cover: `AssetBundleImages`
- video: `MovieData`

You can still override with `--music`, `--cover`, `--video` when needed.

### media

Convert ACB+AWB to MP3:

```bash
maiconv media audio --acb /path/to/music001944.acb --awb /path/to/music001944.awb --output ./track.mp3
```

Pack MP3 into ACB+AWB:

```bash
maiconv media audio --input /path/to/track.mp3 --output-acb ./track.acb --output-awb ./track.awb
```

Convert jacket AB to PNG:

```bash
maiconv media cover --input /path/to/UI_Jacket_001944.ab --output ./bg.png
```

Convert DAT/USM to MP4:

```bash
maiconv media video --input /path/to/001944.dat --output ./pv.mp4
```

Convert MP4 to DAT (template DAT/USM required):

```bash
maiconv media video --input /path/to/pv.mp4 --template /path/to/template_pv.dat --output ./pv.dat
```

Convert MP4 to DAT directly (no template, built-in C++ path):

```bash
maiconv media video --input /path/to/pv.mp4 --output ./pv.dat
```

## Asset Naming Compatibility

`assets` now supports both decoded files and original game asset naming:

- Audio input candidates:
  - `music{dx_id}.mp3/.ogg`
  - `music{non_dx_id}.mp3/.ogg`
  - `music00{non_dx_4}.mp3/.ogg`
  - `music{dx_id}.acb/.awb`
  - `music{non_dx_id}.acb/.awb`
  - `music00{non_dx_4}.acb/.awb`
- Cover input candidates:
  - `UI_Jacket_*.png/.jpg/.jpeg`
  - `ui_jacket_*.png/.jpg/.jpeg/.ab`
  - `AssetBundleImages/jacket/ui_jacket_*.png/.jpg/.jpeg/.ab`
- Video input candidates:
  - `{id}.mp4/.dat/.usm`
  - `{non_dx_id}.mp4/.dat/.usm`

## Assets Export Layout

For assets export, each song folder always contains `maidata.txt`, and media files are normalized to:

```text
{id_title}/
  maidata.txt
  track.mp3
  bg.png
  pv.mp4
```

When source media is in original game formats, `assets` converts them as follows:
- `acb + awb -> track.mp3` (built-in `libvgmstream` + `shine`)
- `ab -> bg.png` (embedded PNG extraction)
- `dat/usm -> pv.mp4`
  - VP9-only path; VP9->H.264 transcode via in-process `libav` when available, otherwise external `ffmpeg`
- `mp4 + template(dat/usm) -> pv.dat`
  - transcodes to VP9 IVF first (in-process `libav` or external `ffmpeg` fallback), then writes back into template DAT/USM packet layout using inverse encryption
- `mp4 -> pv.dat` (template-free)
  - transcodes to VP9 IVF first (in-process `libav` or external `ffmpeg` fallback), then uses MaiConv's built-in C++ packer to emit DAT (`@SFV` packets + compatible encryption)
  - fallback mode requires external `ffmpeg` with `libvpx-vp9` encoder support

If conversion fails, raw assets are **not** preserved. The song is marked as `_Incomplete` (or the command fails without `--ignore`), and failed source/target paths are written to `_log.txt`.

`assets --layout` supports:
- `flat` (default): `{output}/{id_title}`
- `genre`: `{output}/{genre}/{id_title}`
- `version`: `{output}/{version}/{id_title}`

`assets --display` switches `lv_*` export from chart constants like `13.8` to display levels like `13+`.

## Tests

- Unit tests: parser/composer/time/transform/assets/media

## Notes

- Exit code: `0` success, `2` failure.
- Default output file names:
  - Simai: `maidata.txt`
  - Ma2: `result.ma2`

