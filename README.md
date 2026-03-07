# MaiConv

[CN](./README_CN.md) | EN

Cross-platform C++ reimplementation of [MaichartConverter](https://github.com/Neskol/MaichartConverter).

## TODO

- [ ] Add Simai database -> ma2 assets export

## Features

- C++20 + CMake + git submodule (third_party)
- CLI subcommands:
  - `maiconv ma2`
  - `maiconv simai`
  - `maiconv assets`
  - `maiconv media`
- Core pipeline: Tokenizer -> Parser -> Chart(AST) -> Composer
- Note transforms: rotate + tick shift
- Three-platform CI: Windows / Linux / macOS

## Build

```bash
git submodule update --init --recursive
cmake --preset default
cmake --build --preset default
ctest --preset default
```

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

Rules:
- when `--id` is omitted: export all tracks
- when `--id` is provided and `--difficulty` is omitted: export all difficulties for that id
- when both are provided: export only the selected difficulty

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

Convert jacket AB to PNG:

```bash
maiconv media cover --input /path/to/UI_Jacket_001944.ab --output ./bg.png
```

Convert DAT/USM to MP4:

```bash
maiconv media video --input /path/to/001944.dat --output ./pv.mp4
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

When source media is in original game formats, `assets` converts them via built-in C/C++ code (no external CLI tools required):
- `acb + awb -> track.mp3` (built-in `libvgmstream` + `shine`)
- `ab -> bg.png` (embedded PNG extraction)
- `dat/usm -> pv.mp4` (built-in USM parser + VP9 MP4 mux)

If conversion fails, raw assets are **not** preserved. The song is marked as `_Incomplete` (or the command fails without `--ignore`), and failed source/target paths are written to `_log.txt`.

`assets --layout` supports:
- `flat` (default): `{output}/{id_title}`
- `genre`: `{output}/{genre}/{id_title}`
- `version`: `{output}/{version}/{id_title}`

## Tests

- Unit tests: parser/composer/time/transform/assets/media

## Notes

- Exit code: `0` success, `2` failure.
- Default output file names:
  - Simai: `maidata.txt`
  - Ma2: `result.ma2`
