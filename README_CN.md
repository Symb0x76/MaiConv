# MaiConv

CN | [EN](./README.md)

MaiConv 是 [MaichartConverter](https://github.com/Neskol/MaichartConverter) 的跨平台 C++ 重写版本。

## 待办

- [ ] 增加 Simai 数据库 -> ma2 资产导出

## 特性

- C++20 + CMake + git submodule（third_party）
- CLI 子命令：
  - `maiconv ma2`
  - `maiconv simai`
  - `maiconv assets`
  - `maiconv media`
- 核心流程：Tokenizer -> Parser -> Chart(AST) -> Composer
- 谱面变换：旋转 + tick 偏移
- 三平台 CI：Windows / Linux / macOS

## 依赖目录说明

- `third_party/*`：所有第三方依赖均在此目录，并由 git submodule 管理。
- 当前子模块：`CLI11`、`Catch2`、`tinyxml2`、`vgmstream`、`shine`、`minimp4`、`UABE`。

## 构建

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

导出 StreamingAssets 中的全部曲目：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --layout flat
```

导出指定 id（该 id 的全部难度）：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --id 363 --layout flat
```

导出指定 id 的指定难度：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./Output --id 363 --difficulty 3 --layout flat
```

规则：
- 不传 `--id`：导出全部曲目
- 传 `--id` 且不传 `--difficulty`：导出该 id 的全部难度
- 同时传 `--id` 和 `--difficulty`：只导出该难度

`assets` 会自动从 `StreamingAssets` 及其一级子目录识别素材目录：
- 音频：`SoundData`
- 封面：`AssetBundleImages`
- 视频：`MovieData`

如需覆盖自动探测，也可手动指定 `--music`、`--cover`、`--video`。

### media

ACB+AWB 转 MP3：

```bash
maiconv media audio --acb /path/to/music001944.acb --awb /path/to/music001944.awb --output ./track.mp3
```

AB 封面转 PNG：

```bash
maiconv media cover --input /path/to/UI_Jacket_001944.ab --output ./bg.png
```

DAT/USM 转 MP4：

```bash
maiconv media video --input /path/to/001944.dat --output ./pv.mp4
```

## 资产命名兼容

`assets` 已兼容原版游戏常见的 ACB/AWB/AB/DAT 资产命名与目录规则。

- 音频输入候选：
  - `music{dx_id}.mp3/.ogg`
  - `music{non_dx_id}.mp3/.ogg`
  - `music00{non_dx_4}.mp3/.ogg`
  - `music{dx_id}.acb/.awb`
  - `music{non_dx_id}.acb/.awb`
  - `music00{non_dx_4}.acb/.awb`
- 封面输入候选：
  - `UI_Jacket_*.png/.jpg/.jpeg`
  - `ui_jacket_*.png/.jpg/.jpeg/.ab`
  - `AssetBundleImages/jacket/ui_jacket_*.png/.jpg/.jpeg/.ab`
- 视频输入候选：
  - `{id}.mp4/.dat/.usm`
  - `{non_dx_id}.mp4/.dat/.usm`

## Assets 导出目录结构

assets 导出时每首歌必含 `maidata.txt`，媒体文件统一输出为：

```text
{id_title}/
  maidata.txt
  track.mp3
  bg.png
  pv.mp4
```

当源素材是原版游戏格式时，`assets` 的转换策略如下：
- `acb + awb -> track.mp3`（内置 `libvgmstream` + `shine`）
- `ab -> bg.png`（内置 PNG 提取）
- `dat/usm -> pv.mp4`
  - 支持 H.264 流：内置 USM 解析 + MP4 封装

若转换失败，不再保留原始素材文件；该曲目会被标记为 `_Incomplete`（未使用 `--ignore` 时则直接失败），并将失败的源/目标路径写入 `_log.txt`。

`assets --layout` 支持：
- `flat`（默认）：`{output}/{id_title}`
- `genre`：`{output}/{genre}/{id_title}`
- `version`：`{output}/{version}/{id_title}`

## 测试

- 单元测试：parser/composer/time/transform/assets/media

## 说明

- 退出码：`0` 成功，`2` 失败
- 默认输出文件名：
  - Simai：`maidata.txt`
  - Ma2：`result.ma2`

