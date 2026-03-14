# MaiConv

[CN](./README_CN.md) | EN

Cross-platform C++ reimplementation and enhancement of [MaichartConverter](https://github.com/Neskol/MaichartConverter).

## TODO

- [ ] Implement local lz4 decompression in replacement of Unity LZ4 library (currently used via UABE code paths and is not performance-critical)
- [ ] Add png/mp3/mp4 -> ab/awb+acb/dat asset export

## Features

- C++20 + CMake + git submodule (runtime deps in third_party)
- CLI subcommands:
  - `maiconv ma2`
  - `maiconv simai`
  - `maiconv assets`
  - `maiconv media`
- Note transforms: rotate + tick shift
- Cross-platform: Windows / Linux / macOS

## Dependency Layout

- `third_party/*`: runtime third-party dependencies are managed by git submodules.
- Test dependency `Catch2` is also managed as a git submodule when `MAICONV_BUILD_TESTS=ON`.
- Required build submodules: `CLI11`, `tinyxml2` (plus `Catch2` when tests are enabled).

## Build

```bash
git submodule update --init --recursive
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## FFmpeg Dependency Details

Video-related `maiconv media` features use external `ffmpeg` only.

Default build behavior:
- no in-process `libav` backend is compiled.

Build examples:

```bash
# default
cmake --preset default
```

External `ffmpeg` executable requirements:
- `ffmpeg` should be available in `PATH`, or set `MAICONV_FFMPEG` to an absolute executable path.
- `ffprobe` is optional (useful for manual diagnostics).

Required capabilities by feature:
- `dat/usm -> mp4`: requires `libx264` encoder (MaiConv transcodes VP9 IVF into H.264 MP4)
- `mp4 -> dat` (both template and template-free paths): requires `libvpx-vp9` encoder (MaiConv first transcodes to VP9 IVF)

Optional ffmpeg tuning settings (when your ffmpeg build supports them):
- `MAICONV_FFMPEG_HWACCEL`: e.g. `auto`, `cuda`, `d3d11va`, `qsv`
- `MAICONV_FFMPEG_H264_ENCODER`: e.g. `h264_nvenc`, `h264_qsv`, `h264_amf`, `libx264`
- `MAICONV_FFMPEG_VP9_ENCODER`: e.g. `vp9_qsv`, `libvpx-vp9`
- `MAICONV_FFMPEG_AUDIO_HWACCEL`: audio ffmpeg path hwaccel hint (same values as above)
- `MAICONV_FFMPEG_MP3_ENCODER`: mp3 encoder for ffmpeg path (default `libmp3lame`)
- CLI `--gpu` flag (for `assets` and `media audio|video`): auto-enables GPU hints and encoder fallback without manual env vars

Notes:
- Audio decode -> mp3 now always routes through ffmpeg; hwaccel hints are best-effort and real gains still depend on codec/driver support.
- `mp4 -> dat` often still depends on VP9 encode throughput; GPU benefit depends on whether your ffmpeg provides a VP9 hardware encoder.
- `--gpu` sets `MAICONV_FFMPEG_GPU=1` and fills `MAICONV_FFMPEG_HWACCEL/AUDIO_HWACCEL=auto` only when they are unset, so explicit env vars still win.

PowerShell quick start (`--gpu` + explicit override):

```powershell
# auto GPU hints
maiconv media video --input .\pv.mp4 --output .\pv.dat --gpu

# force your own choice (overrides --gpu defaults)
$env:MAICONV_FFMPEG_HWACCEL="cuda"
$env:MAICONV_FFMPEG_H264_ENCODER="h264_nvenc"
maiconv media video --input .\001944.dat --output .\pv.mp4 --gpu
```

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
# auto GPU hint + encoder fallback
# maiconv assets --input /path/to/StreamingAssets --output ./Output --layout flat --gpu
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
# auto GPU hint + encoder fallback
# maiconv media video --input /path/to/pv.mp4 --output ./pv.dat --gpu
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
- `acb + awb -> track.mp3` (always transcoded by external `ffmpeg`)
- `ab -> bg.png` (embedded PNG extraction)
- `dat/usm -> pv.mp4`
  - VP9-only path; VP9->H.264 transcode uses external `ffmpeg`
- `mp4 + template(dat/usm) -> pv.dat`
  - transcodes to VP9 IVF first (external `ffmpeg`), then writes back into template DAT/USM packet layout using inverse encryption
- `mp4 -> pv.dat` (template-free)
  - transcodes to VP9 IVF first (external `ffmpeg`), then uses MaiConv's built-in C++ packer to emit DAT (`@SFV` packets + compatible encryption)
  - requires external `ffmpeg` with `libvpx-vp9` encoder support

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

