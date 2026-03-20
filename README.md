# MaiConv

[CN](./README_CN.md) | EN

Cross-platform C++ reimplementation and enhancement of [MaichartConverter](https://github.com/Neskol/MaichartConverter).

## TODO

- [ ] Implement local lz4 decompression in replacement of Unity LZ4 library (currently used via UABE code paths and is not performance-critical)
- [ ] Add reverse asset export in `assets` workflow (currently available in `maiconv media`: `png->ab`, `mp3->acb+awb`, `mp4->dat`)
- [x] Separate 1P/2P Utage charts and append `(L)/(R)` to output folder names and `maidata` `&title=`
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

Audio and Video processing in `maiconv media` and `maiconv assets` uses external `ffmpeg`.

External `ffmpeg` executable requirements:
- `ffmpeg` should be available in `PATH`, or set `MAICONV_FFMPEG` to an absolute executable path.
- `ffprobe` is optional (useful for manual diagnostics).

Required capabilities by feature:
- `dat/usm/crid -> mp4`: supports VP9 IVF, H.264 Annex-B, and MPEG video streams; stream copy is attempted when possible, but an H.264 encoder (`libx264` or hardware alternative) is needed for transcode fallback.
- `mp4 -> dat`: requires a VP9 encoder (`libvpx-vp9` or hardware alternative) because MaiConv first transcodes to VP9 IVF.

Optional ffmpeg tuning settings (when your ffmpeg build supports them):
- `MAICONV_FFMPEG_HWACCEL`: e.g. `auto`, `cuda`, `d3d11va`, `qsv`
- `MAICONV_FFMPEG_H264_ENCODER`: e.g. `h264_nvenc`, `h264_qsv`, `h264_amf`, `libx264`
- `MAICONV_FFMPEG_VP9_ENCODER`: e.g. `vp9_qsv`, `libvpx-vp9`
- `MAICONV_FFMPEG_AUDIO_HWACCEL`: audio ffmpeg path hwaccel hint (same values as above)
- `MAICONV_FFMPEG_MP3_ENCODER`: mp3 encoder for ffmpeg path (default `libmp3lame`)
- CLI `--gpu` flag (for `assets` and `media audio|video`): auto-enables GPU hints and encoder fallback without manual env vars

Notes:
- hwaccel hints are best-effort and real gains still depend on codec/driver support.
- `mp4 -> dat` depends on VP9 encode throughput; GPU benefit depends on whether your ffmpeg provides a VP9 hardware encoder.
- `--gpu` sets `MAICONV_FFMPEG_GPU=1` and fills `MAICONV_FFMPEG_HWACCEL/AUDIO_HWACCEL=auto` only when they are unset, so explicit env vars still win.

PowerShell quick start (`--gpu` + explicit override):

```powershell
# auto GPU hints
maiconv media video --input .\pv.mp4 --output .\pv.dat --gpu

# force your own choice (overrides --gpu defaults)
$env:MAICONV_FFMPEG_HWACCEL="cuda"
$env:MAICONV_FFMPEG_H264_ENCODER="h264_nvenc"
maiconv media video --input .\001145.dat --output .\pv.mp4 --gpu
```

Quick checks:

```bash
ffmpeg -version
ffmpeg -hide_banner -encoders
```

If required encoders are missing, install an ffmpeg build that provides at least one H.264 encoder and one VP9 encoder (recommended baseline: `libx264` + `libvpx-vp9`).

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

Quick commands:

Export all tracks:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --layout flat
```

Export one or more ids (all difficulties):

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --id 363,114514 --layout flat
```

Export selected ids with selected difficulties:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --id 363,114514 --difficulty 2,3,7 --layout flat
```

Export by regex filters:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --id '^11\\d{4}$' --difficulty '^[23]$' --layout flat
```

Export `maidata.txt` with display levels:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --format maidata --display
```

Resume export and skip already completed tracks:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --layout flat --resume
```

Generate placeholders for missing media:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --layout flat --dummy
```

Export only selected output types:

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --layout flat --types maidata.txt,track.mp3
```

Selection rules:
- when `--id` is omitted: export all tracks
- when `--id` is provided and `--difficulty` is omitted: export all difficulties for matched ids
- when both are provided: export only matched difficulties for matched ids
- `--id` and `--difficulty` accept comma-separated filters, and each filter can be an exact number or a regex
- `--difficulty` uses exported `maidata` numbering: standard charts are usually `2..6`, utage is `7`
- for Utage tracks, when both `*_L.ma2` and `*_R.ma2` exist in the same chart folder, MaiConv exports two outputs and appends `(L)` / `(R)` to both folder name and `maidata` `&title=`
- `--difficulty 7` matches both `(L)` and `(R)` outputs for split Utage charts
- `--resume` (`--skip-existing`) skips tracks that already have complete exports, while keeping `_Incomplete` tracks eligible for retry
- `--types` accepts comma-separated values:
  `maidata.txt`/`track.mp3`/`bg.png`/`pv.mp4`
  (aliases: `chart|ma2`, `audio|music`, `cover|jacket|bg`, `video|movie|pv`)

Folder discovery:
- audio: `SoundData`
- cover: `AssetBundleImages`
- video: `MovieData`
- override auto-detection when needed: `--music`, `--cover`, `--video`

Dummy output spec:
- enable with `--dummy`
- if `track.mp3` is missing: generate silent `track.mp3` using chart-derived duration
- if `pv.mp4` is missing and `bg.png` exists: generate one-frame `pv.mp4` from `bg.png`
- if `pv.mp4` is missing and `bg.png` does not exist: generate one-frame black `pv.mp4`

Fixed tags:
- `MISSING_AUDIO`: `track.mp3` was dummy-generated
- `MISSING_VIDEO`: `pv.mp4` was dummy-generated
- `SOURCE_BG_PNG`: dummy video source is `bg.png`
- `BLACK_FRAME`: dummy video source is a black frame

Machine-readable channels:
- progress line: `[dummy: <TAG>[,<TAG>...]]`
- warning line: `MAICONV_DUMMY:<musicId>:<TAG>`

### media

Convert ACB+AWB to MP3:

```bash
maiconv media audio --acb /path/to/music001944.acb --awb /path/to/music001944.awb --output ./track.mp3
```

Pack MP3 into ACB+AWB:

```bash
maiconv media audio --input /path/to/track.mp3 --output-acb ./track.acb --output-awb ./track.awb
```

Convert jacket between AB and image files:

```bash
maiconv media cover --input /path/to/UI_Jacket_001944.ab --output ./bg.png
maiconv media cover --input /path/to/bg.png --output ./bg.ab
```

Convert DAT/USM/CRID to MP4:

```bash
maiconv media video --input /path/to/001944.dat --output ./pv.mp4
```

- VP9 IVF streams are transcoded to H.264 MP4.
- H.264 Annex-B / MPEG streams are remuxed first (`-c:v copy`), then transcoded to H.264 only if remux fails.

Convert MP4 to DAT:

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
  - `AssetBundleImages/jacket_s/ui_jacket_*_s.png/.jpg/.jpeg/.ab`
- Video input candidates:
  - `{id}.mp4/.dat/.usm/.crid`
  - `{non_dx_id}.mp4/.dat/.usm/.crid`
  - fallback based on `movieName` / `cueName` ids in `Music.xml`

## Assets Export Layout

For assets export, each song folder always contains `maidata.txt`, and media target names are:

```text
{id_title}/
  maidata.txt
  track.mp3
  bg.png
  pv.mp4
```

`track.mp3`/`bg.png`/`pv.mp4` may be missing when source media is unavailable (unless `--dummy` is used).
For split Utage tracks, output folder names become `{id_title} (L)` and `{id_title} (R)`, and both `maidata` titles carry the same suffix.

When source media is in original game formats, `assets` converts them as follows:
- `acb + awb -> track.mp3` (always transcoded by external `ffmpeg`)
- `ab -> bg.png` (embedded PNG extraction)
- `dat/usm/crid -> pv.mp4`
  - extracts/decrypts embedded USM/CRID video stream first
  - VP9 IVF streams are transcoded to H.264 MP4
  - H.264/MPEG streams try stream-copy remux first, then fall back to H.264 transcode
  - if extraction/remux path fails, MaiConv falls back to direct `ffmpeg` transcode from the source file
- `mp4 -> pv.dat`
  - transcodes to VP9 IVF first (external `ffmpeg`), then uses MaiConv's built-in C++ packer to emit DAT (`@SFV` packets + compatible encryption)
  - requires external `ffmpeg` with `libvpx-vp9` encoder support

Failure handling:
- if `movieName` is `DEBUG_*`, missing video is treated as optional
- otherwise media conversion/missing files mark the track as `_Incomplete` (or fail the command when `--ignore` is not set)

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
