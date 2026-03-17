# MaiConv Detailed TODO

本文件将 README 中的 3 个 TODO 展开为可执行任务清单。

## Scope

- [x] 本地化替换 LZ4（解码 + 编码）
- [ ] 在 assets 工作流中补齐反向导出（png->ab、mp3->acb+awb、mp4->dat）
- [ ] Utage 1P/2P 分离，并在输出名称与 title 元数据追加 (1P)/(2P)

## Milestone A: 契约冻结与设计基线

目标：先锁行为契约，避免编码后反复返工。

- [ ] 定义 LZ4 兼容契约
  - [ ] 流式 callback 行为与位置推进规则（read/write + pos）
  - [ ] 返回值兼容规则（成功/失败边界）
  - [ ] 截断/损坏块/短写等错误语义
- [ ] 定义 png->ab 最小兼容目标
  - [ ] MaiConv 自产 AB 可被 MaiConv 反向读取
  - [ ] 常见 AB 查看器可读（作为加分项，不作为首发阻塞）
- [ ] 定义 Utage 1P/2P 判定规则
  - [ ] 明确可用的判定线索（如文件名、inote等）
  - [ ] 仅部分宴谱有多P，需要详细的判定规则以覆盖现有样本
  - [ ] 无法判定时的兜底行为（如默认为没有多P 并 warning）

验收标准：
- [ ] 形成明确实现文档（规则可直接映射到单测断言）

## Milestone B: LZ4 本地实现替换（编码+解码）

目标：移除对 Unity/UABE 旧 LZ4 路径的运行时依赖，保持行为一致。

### B1. 代码实现

- [x] 新增本地 LZ4 适配层（接口与现有调用兼容）
- [x] 替换以下调用点：
  - [x] src/core 依赖链涉及的 AssetBundle 解包路径
  - [x] ClassDatabase 直接解压路径
  - [x] 相关编码路径（用于打包/写出）
- [x] 保持流式 callback 机制可用（不可退化为仅一次性 buffer 模式）

### B2. 构建与链接

- [x] 调整 CMake，切换 LZ4 源依赖到本地实现
- [ ] 保留 AB 打包链路必需依赖，避免误删导致后续 png->ab 失效

### B3. 测试

- [ ] 扩展 tests/unit/test_uabe_lz4.cpp
  - [ ] 编解码回环
  - [ ] 截断输入
  - [ ] 错误块
  - [ ] 短读/短写
  - [ ] 大缓冲区场景
  - [ ] 返回值与错误码兼容性

验收标准：
- [ ] ctest -R uabe_lz4 全绿
- [ ] 旧行为回归不退化（关键失败场景仍可稳定复现）

## Milestone C: 真正的 png->ab 能力

目标：替换现有占位 copy 逻辑，提供真实 Unity AssetBundle 写出。

### C1. 核心转换 API

- [ ] 在 include/maiconv/core/media.hpp 增加 convert_png_to_ab 声明
- [ ] 在 src/core/media.cpp 实现 convert_png_to_ab
  - [ ] PNG 解码（尺寸/像素格式）
  - [ ] Texture2D 资源序列化
  - [ ] AssetBundle 容器写入（header + entry + data）
  - [ ] 输出文件完整性检查

### C2. 命令接线

- [ ] maiconv media cover 路径改为调用正式 convert_png_to_ab
- [ ] 错误日志包含输入文件、目标文件、失败阶段

### C3. 测试

- [ ] 新增/扩展 cover 回环测试：png->ab->png
- [ ] 校验尺寸一致、可读性一致（允许编码细节差异）

验收标准：
- [ ] 单命令可稳定执行 png->ab
- [ ] 回环测试通过且日志可定位

## Milestone D: assets 反向导出三项接入

目标：在 assets 工作流直接支持三种反向产物生成。

### D1. CLI 与选项结构

- [ ] 扩展 assets 类型解析（src/cli/main.cpp）
  - [ ] 支持 png->ab
  - [ ] 支持 mp3->acb+awb
  - [ ] 支持 mp4->dat
- [ ] 扩展 AssetsOptions（include/maiconv/core/assets.hpp）
  - [ ] 增加反向导出开关/类型集合

### D2. 执行流程

- [ ] 在 run_compile_assets（src/core/assets.cpp）新增反向阶段
- [ ] 扫描标准输出命名并生成反向产物
  - [ ] track.mp3 -> acb+awb
  - [ ] pv.mp4 -> dat
  - [ ] bg.png -> ab
- [ ] 继承现有语义
  - [ ] --resume
  - [ ] --ignore
  - [ ] 日志等级
  - [ ] 失败聚合

### D3. 测试

- [ ] 扩展 tests/unit/test_assets.cpp
  - [ ] 三种类型分别可触发
  - [ ] --types 过滤生效
  - [ ] 音视频回环至少覆盖一组
  - [ ] cover 反向产物可读

验收标准：
- [ ] maiconv assets --types mp3->acb+awb,mp4->dat,png->ab 输出完整
- [ ] 单项失败可定位且不吞掉上下文

## Milestone E: Utage 1P/2P 分离与命名

目标：分离 1P/2P 输出，避免继续混在 difficulty=7 同路径下不可辨识。

### E1. 判定与模型

- [ ] 在 chart 发现阶段加入 1P/2P 判定
- [ ] 在 TrackInfo/映射结构增加 player-side 字段
- [ ] 无法判定时按无多P并 warning

### E2. 输出命名与元数据

- [ ] 输出目录名追加 (1P)/(2P)
- [ ] maidata title 元数据追加 (1P)/(2P)
- [ ] 确保排序稳定，避免覆盖写入

### E3. 筛选兼容

- [ ] 更新筛选相关逻辑，确保 1P/2P 不互相覆盖
- [ ] 文档说明筛选行为变化

### E4. 测试

- [ ] 扩展 tests/unit/test_assets.cpp
  - [ ] 1P/2P 命名断言
  - [ ] title 元数据断言
  - [ ] 筛选行为断言

验收标准：
- [ ] Utage 导出结果可稳定区分 1P/2P 或明确没有多P

## Milestone F: 文档、CI、发布准备

### F1. 文档更新

- [ ] README.md
  - [ ] TODO 状态更新
  - [ ] assets 反向导出示例
  - [ ] Utage 1P/2P 命名说明
  - [ ] LZ4 本地化说明（如需）
- [ ] README_CN.md 同步更新

### F2. CI 验证

- [ ] 复用现有 build/test 分离工作流
- [ ] 新增测试纳入 CTest
- [ ] 确认失败工件可用于 flaky 排查

### F3. 发布检查

- [ ] 关键命令 smoke
- [ ] 关键回环 smoke
- [ ] 变更日志补充

验收标准：
- [ ] CI 全绿
- [ ] 文档命令可复制执行

## Task Board（建议执行顺序）

### P0

- [ ] B: LZ4 本地替换（解码+编码）
- [ ] C: 真正 png->ab

### P1

- [ ] D: assets 反向导出三项接入
- [ ] E: Utage 1P/2P 分离

### P2

- [ ] F: 文档/CI/发布准备

## Risk Register

- [ ] 风险：png->ab 容器写入兼容性不足
  - [ ] 缓解：先保证 MaiConv 自产可回读，再扩展外部兼容
- [ ] 风险：LZ4 替换导致历史边界行为变化
  - [ ] 缓解：保留并增强错误注入测试
- [ ] 风险：Utage 判定来源不稳定
  - [ ] 缓解：优先显式规则，兜底 warning，并补充测试样本

## Rollback Plan

- [ ] LZ4 替换与 assets/Utage 改动拆分提交，确保可按模块回退
- [ ] png->ab 若兼容性问题严重，可临时降级为 feature flag 控制
- [ ] 任何回退后确保 CI 与 CTest 仍可通过

## Definition of Done

- [ ] 三个 README TODO 均可打勾
- [ ] 核心路径有单测覆盖，关键路径有最小端到端验证
- [ ] CI build/test 全平台通过
- [ ] 文档与实际行为一致
