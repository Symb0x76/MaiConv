# MaiConv

CN | [EN](./README.md)

MaiConv 是 [MaichartConverter](https://github.com/Neskol/MaichartConverter) 的跨平台 C++ 重写并增强版本。

## 待办

- [ ] 在 `assets` 流程中补齐反向资产导出（目前 `maiconv media` 已支持：`png->ab`、`mp3->acb+awb`、`mp4->dat`）
- [ ] 实现本地lz4取代对UABE的依赖以提升跨平台性能
- [x] 宴谱分离 1P/2P 并在输出目录名与 `maidata` 的 `&title=` 追加 `(L)/(R)`

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

`maiconv media` 与 `maiconv assets` 的音视频处理统一依赖外部 `ffmpeg`。

外部 `ffmpeg` 要求：
- `ffmpeg` 可执行文件在 `PATH` 中可调用，或通过 `MAICONV_FFMPEG` 指向绝对路径
- `ffprobe` 可选（便于手工排查媒体流信息）

必需能力（按功能划分）：
- `dat/usm/crid -> mp4`：支持 VP9 IVF、H.264 Annex-B、MPEG 视频流；能流复制时会优先流复制，但在回退转码路径上需要 H.264 编码器（`libx264` 或硬件编码器）
- `mp4 -> dat`：需要 VP9 编码器（`libvpx-vp9` 或硬件编码器），因为 MaiConv 会先转码为 VP9 IVF

可选 ffmpeg 调优设置（前提是你的 ffmpeg 构建支持）：
- `MAICONV_FFMPEG_HWACCEL`：例如 `auto`、`cuda`、`d3d11va`、`qsv`
- `MAICONV_FFMPEG_H264_ENCODER`：例如 `h264_nvenc`、`h264_qsv`、`h264_amf`、`libx264`
- `MAICONV_FFMPEG_VP9_ENCODER`：例如 `vp9_qsv`、`libvpx-vp9`
- `MAICONV_FFMPEG_AUDIO_HWACCEL`：音频 ffmpeg 路径的 hwaccel 提示（取值同上）
- `MAICONV_FFMPEG_MP3_ENCODER`：音频 ffmpeg 路径使用的 mp3 编码器（默认 `libmp3lame`）
- CLI `--gpu` 参数（`assets` 与 `media audio|video`）：自动开启 GPU 提示与编码器回退，减少手动环境变量配置

说明：
- hwaccel 提示是 best-effort，真实提速取决于编解码器与驱动支持。
- `mp4 -> dat` 的瓶颈常在 VP9 编码；只有当 ffmpeg 提供可用的 VP9 硬件编码器时，GPU 收益才会明显。
- `--gpu` 会设置 `MAICONV_FFMPEG_GPU=1`，并且仅在未设置时补 `MAICONV_FFMPEG_HWACCEL/AUDIO_HWACCEL=auto`；你手动设置的环境变量优先级更高。

PowerShell 快速示例（`--gpu` + 手动覆盖）：

```powershell
# 自动 GPU 提示
maiconv media video --input .\pv.mp4 --output .\pv.dat --gpu

# 手动指定优先（覆盖 --gpu 默认）
$env:MAICONV_FFMPEG_HWACCEL="cuda"
$env:MAICONV_FFMPEG_H264_ENCODER="h264_nvenc"
maiconv media video --input .\001145.dat --output .\pv.mp4 --gpu
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

若缺少所需编码器，请安装可提供至少一种 H.264 编码器和一种 VP9 编码器的 ffmpeg 发行版（推荐基线：`libx264` + `libvpx-vp9`）。

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

常用命令：

导出全部曲目：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --layout flat
```

导出一个或多个 id（命中 id 的全部难度）：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --id 114514,363 --layout flat
```

导出指定 id 的指定难度（多值）：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --id 114514,363 --difficulty 2,3,7 --layout flat
```

按正则筛选数字：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --id '^11\\d{4}$' --difficulty '^[23]$' --layout flat
```

按版本筛选曲目：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --version '23,^buddies\\s*plus$' --layout flat
```

以显示等级导出 `maidata.txt` 中的 `lv_*`：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --format maidata --display
```

补全导出并跳过已完成曲目：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --layout flat --resume
```

缺失媒体时自动补齐占位文件：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --layout flat --dummy
```

仅导出指定类型：

```bash
maiconv assets --input /path/to/StreamingAssets --output ./output --layout flat --types maidata.txt,track.mp3
```

筛选规则：
- 不传 `--id`：导出全部曲目
- 传 `--id` 且不传 `--difficulty`：导出命中 id 的全部难度
- 同时传 `--id` 和 `--difficulty`：只导出命中 id 的命中难度
- `--id` 和 `--difficulty` 支持逗号分隔多条件，每一项可为数字或正则
- `--version` 支持逗号分隔多条件，每一项可为版本 id（数字）、版本名或正则
- `--difficulty` 使用导出的 `maidata` 难度编号：普通谱通常是 `2..6`，宴谱是 `7`
- 对宴谱而言，若同一谱面目录同时存在 `*_L.ma2` 与 `*_R.ma2`，MaiConv 会拆分导出两份结果，并在目录名与 `maidata` `&title=` 追加 `(L)` / `(R)`
- 对已拆分的宴谱，`--difficulty 7` 会同时命中 `(L)` 与 `(R)` 两份输出
- `--resume`（`--skip-existing`）会跳过已存在完整导出的曲目；`_Incomplete` 曲目仍会继续尝试补全
- `--types` 支持逗号分隔：
  `maidata.txt` / `track.mp3` / `bg.png` / `pv.mp4`
  （别名：`chart|ma2`、`audio|music`、`cover|jacket|bg`、`video|movie|pv`）

目录识别：
- 音频：`SoundData`
- 封面：`AssetBundleImages`
- 视频：`MovieData`
- 如需覆盖自动探测，可手动指定：`--music`、`--cover`、`--video`

`--dummy` 英文固定输出规范：
- 开启方式：`--dummy`
- 若缺少 `track.mp3`：按谱面时长生成静音 `track.mp3`
- 若缺少 `pv.mp4` 且存在 `bg.png`：由 `bg.png` 生成单帧 `pv.mp4`
- 若缺少 `pv.mp4` 且不存在 `bg.png`：生成单帧黑屏 `pv.mp4`

固定标签：
- `MISSING_AUDIO`：`track.mp3` 为 dummy 生成
- `MISSING_VIDEO`：`pv.mp4` 为 dummy 生成
- `SOURCE_BG_PNG`：dummy 视频来源于 `bg.png`
- `BLACK_FRAME`：dummy 视频为黑帧

机器可读双通道：
- 进度行：`[dummy: <TAG>[,<TAG>...]]`
- `Warnings`：`MAICONV_DUMMY:<musicId>:<TAG>`

### media

ACB+AWB 转 MP3：

```bash
maiconv media audio --acb /path/to/music114514.acb --awb /path/to/music114514.awb --output ./track.mp3
```

MP3 打包为 ACB+AWB：

```bash
maiconv media audio --input /path/to/track.mp3 --output-acb ./track.acb --output-awb ./track.awb
```

AB 与图片互转：

```bash
maiconv media cover --input /path/to/UI_Jacket_114514.ab --output ./bg.png
maiconv media cover --input /path/to/bg.png --output ./bg.ab
```

DAT/USM/CRID 转 MP4：

```bash
maiconv media video --input /path/to/114514.dat --output ./pv.mp4
```

- VP9 IVF 流会转码为 H.264 MP4。
- H.264 Annex-B / MPEG 流会先尝试流复制封装（`-c:v copy`），失败后再回退为 H.264 转码。

MP4 转 DAT：

```bash
maiconv media video --input /path/to/pv.mp4 --output ./pv.dat
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
  - `AssetBundleImages/jacket_s/ui_jacket_*_s.png/.jpg/.jpeg/.ab`
- 视频输入候选：
  - `{id}.mp4/.dat/.usm/.crid`
  - `{non_dx_id}.mp4/.dat/.usm/.crid`
  - 同时会结合 `Music.xml` 中的 `movieName` / `cueName` id 做回退匹配

## Assets 导出目录结构

assets 导出时每首歌必含 `maidata.txt`，媒体目标文件名为：

```text
{id_title}/
  maidata.txt
  track.mp3
  bg.png
  pv.mp4
```

当源媒体缺失时，`track.mp3`/`bg.png`/`pv.mp4` 可能不存在（除非启用 `--dummy`）。
对已拆分的宴谱，输出目录会变为 `{id_title} (L)` 与 `{id_title} (R)`，两份 `maidata` 的标题也会带相同后缀。

当源素材是原版游戏格式时，`assets` 的转换策略如下：
- `acb + awb -> track.mp3`（统一使用外部 `ffmpeg` 转码）
- `ab -> bg.png`（内置 PNG 提取）
- `dat/usm/crid -> pv.mp4`
  - 先提取/解密 USM/CRID 内嵌视频流
  - VP9 IVF 流会转码为 H.264 MP4
  - H.264/MPEG 流会先尝试流复制封装，失败后回退为 H.264 转码
  - 若提取/封装路径失败，MaiConv 会回退为对源文件直接执行 `ffmpeg` 转码
- `mp4 -> pv.dat`
  - 先转码为 VP9 IVF（外部 `ffmpeg`），再由 MaiConv 内置 C++ 打包器生成 DAT（`@SFV` 分包 + 同步加密）
  - 要求环境中可用 `ffmpeg`（需支持 `libvpx-vp9` 编码）

失败处理规则：
- 若 `movieName` 为 `DEBUG_*`，缺失视频按可选资源处理
- 其他媒体缺失或转换失败会将曲目标记为 `_Incomplete`（未加 `--ignore` 时命令直接失败）

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

