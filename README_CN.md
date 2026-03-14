# MaiConv

CN | [EN](./README.md)

MaiConv 是 [MaichartConverter](https://github.com/Neskol/MaichartConverter) 的跨平台 C++ 重写并增强版本。

## 待办

- [ ] 增加 png/mp3/mp4 -> ab/awb+acb/dat 资产导出
- [ ] 实现本地lz4取代对UABE的依赖以提升跨平台性能

## 特性

- C++20 + CMake + git submodule（运行时依赖在 third_party）
- CLI 子命令：
  - `maiconv ma2`
  - `maiconv simai`
  - `maiconv assets`
  - `maiconv media`
- 谱面变换：旋转 + tick 偏移
- 跨平台：Windows / Linux / macOS

## 依赖目录说明

- `third_party/*`：运行时第三方依赖在此目录，并由 git submodule 管理。
- 测试依赖 `Catch2` 在 `MAICONV_BUILD_TESTS=ON` 时也通过 git submodule 管理。
- 构建必需子模块：`CLI11`、`tinyxml2`（启用测试时还需要 `Catch2`）。

## 构建

```bash
git submodule update --init --recursive
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## FFmpeg 依赖说明

`maiconv media` 的视频相关功能现已统一使用外部 `ffmpeg`。

默认构建行为：
- 不再编译进程内 `libav` 后端

构建示例：

```bash
# 默认
cmake --preset default
```

外部 `ffmpeg` 要求：
- `ffmpeg` 可执行文件在 `PATH` 中可调用，或通过 `MAICONV_FFMPEG` 指向绝对路径
- `ffprobe` 可选（便于手工排查媒体流信息）

必需能力（按功能划分）：
- `dat/usm -> mp4`：需要 `libx264` 编码器（MaiConv 会把 VP9 IVF 转码为 H.264 MP4）
- `mp4 -> dat`（含有模板与无模板两种路径）：需要 `libvpx-vp9` 编码器（MaiConv 会先转码为 VP9 IVF）

可选 ffmpeg 调优设置（前提是你的 ffmpeg 构建支持）：
- `MAICONV_FFMPEG_HWACCEL`：例如 `auto`、`cuda`、`d3d11va`、`qsv`
- `MAICONV_FFMPEG_H264_ENCODER`：例如 `h264_nvenc`、`h264_qsv`、`h264_amf`、`libx264`
- `MAICONV_FFMPEG_VP9_ENCODER`：例如 `vp9_qsv`、`libvpx-vp9`
- `MAICONV_FFMPEG_AUDIO_HWACCEL`：音频 ffmpeg 路径的 hwaccel 提示（取值同上）
- `MAICONV_FFMPEG_MP3_ENCODER`：音频 ffmpeg 路径使用的 mp3 编码器（默认 `libmp3lame`）
- CLI `--gpu` 参数（`assets` 与 `media audio|video`）：自动开启 GPU 提示与编码器回退，减少手动环境变量配置

说明：
- 音频解码转 mp3 现已统一走 ffmpeg；hwaccel 提示是 best-effort，真实提速取决于编解码器与驱动支持。
- `mp4 -> dat` 的瓶颈常在 VP9 编码；只有当 ffmpeg 提供可用的 VP9 硬件编码器时，GPU 收益才会明显。
- `--gpu` 会设置 `MAICONV_FFMPEG_GPU=1`，并且仅在未设置时补 `MAICONV_FFMPEG_HWACCEL/AUDIO_HWACCEL=auto`；你手动设置的环境变量优先级更高。

PowerShell 快速示例（`--gpu` + 手动覆盖）：

```powershell
# 自动 GPU 提示
maiconv media video --input .\pv.mp4 --output .\pv.dat --gpu

# 手动指定优先（覆盖 --gpu 默认）
$env:MAICONV_FFMPEG_HWACCEL="cuda"
$env:MAICONV_FFMPEG_H264_ENCODER="h264_nvenc"
maiconv media video --input .\001944.dat --output .\pv.mp4 --gpu
```

快速自检（Windows PowerShell）：

```powershell
ffmpeg -version
ffmpeg -hide_banner -encoders | Select-String "libx264|libvpx-vp9"
```

快速自检（Linux/macOS）：

```bash
ffmpeg -version
ffmpeg -hide_banner -encoders | grep -E "libx264|libvpx-vp9"
```

若缺少编码器，请安装带完整编码库的 ffmpeg 发行版（包含 `libx264` 与 `libvpx`）。

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
maiconv assets --input /path/to/StreamingAssets --output ./output --layout flat
# 自动启用 GPU 提示 + 编码器回退
# maiconv assets --input /path/to/StreamingAssets --output ./output --layout flat --gpu
```

导出指定 id（该 id 的全部难度）：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --id 114514 --layout flat
```

导出指定 id 的指定难度：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --id 114514 --difficulty 3 --layout flat
```

以显示等级导出 `maidata.txt` 中的 `lv_*`：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --format maidata --display
```

补全导出并跳过已完成曲目：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --layout flat --resume
```

规则：
- 不传 `--id`：导出全部曲目
- 传 `--id` 且不传 `--difficulty`：导出该 id 的全部难度
- 同时传 `--id` 和 `--difficulty`：只导出该难度
- `--difficulty` 使用导出的 `maidata` 难度编号：普通谱通常是 `2..6`，宴谱是 `7`
- `--resume`（`--skip-existing`）会跳过已存在完整导出的曲目；`_Incomplete` 曲目仍会继续尝试补全

`assets` 会自动从 `StreamingAssets` 及其一级子目录识别素材目录：
- 音频：`SoundData`
- 封面：`AssetBundleImages`
- 视频：`MovieData`

如需覆盖自动探测，也可手动指定 `--music`、`--cover`、`--video`。

### media

ACB+AWB 转 MP3：

```bash
maiconv media audio --acb /path/to/music114514.acb --awb /path/to/music114514.awb --output ./track.mp3
```

MP3 打包为 ACB+AWB：

```bash
maiconv media audio --input /path/to/track.mp3 --output-acb ./track.acb --output-awb ./track.awb
```

AB 封面转 PNG：

```bash
maiconv media cover --input /path/to/UI_Jacket_114514.ab --output ./bg.png
```

DAT/USM 转 MP4：

```bash
maiconv media video --input /path/to/114514.dat --output ./pv.mp4
```

MP4 转 DAT：

```bash
maiconv media video --input /path/to/pv.mp4 --output ./pv.dat
# 自动启用 GPU 提示 + 编码器回退
# maiconv media video --input /path/to/pv.mp4 --output ./pv.dat --gpu
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
- `acb + awb -> track.mp3`（统一使用外部 `ffmpeg` 转码）
- `ab -> bg.png`（内置 PNG 提取）
- `dat/usm -> pv.mp4`
  - 仅支持 VP9 流；VP9->H.264 转码使用外部 `ffmpeg`
- `mp4 + template(dat/usm) -> pv.dat`
  - 先转码为 VP9 IVF（外部 `ffmpeg`），再按模板 DAT/USM 的视频包结构回填并走逆向加密写回
- `mp4 -> pv.dat`（无模板）
  - 先转码为 VP9 IVF（外部 `ffmpeg`），再由 MaiConv 内置 C++ 打包器生成 DAT（`@SFV` 分包 + 同步加密）
  - 要求环境中可用 `ffmpeg`（需支持 `libvpx-vp9` 编码）

若转换失败，不再保留原始素材文件；该曲目会被标记为 `_Incomplete`（未使用 `--ignore` 时则直接失败），并将失败的源/目标路径写入 `_log.txt`。

`assets --layout` 支持：
- `flat`（默认）：`{output}/{id_title}`
- `genre`：`{output}/{genre}/{id_title}`
- `version`：`{output}/{version}/{id_title}`

`assets --display` 会把 `lv_*` 的导出从 `13.8` 这类定数切换为 `13+` 这类显示等级。

## 测试

- 单元测试：parser/composer/time/transform/assets/media

## 说明

- 退出码：`0` 成功，`2` 失败
- 默认输出文件名：
  - Simai：`maidata.txt`
  - Ma2：`result.ma2`

