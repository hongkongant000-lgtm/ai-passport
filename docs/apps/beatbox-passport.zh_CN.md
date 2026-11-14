<p align="right">
  <a href="beatbox-passport.md">English</a> · <strong>简体中文</strong>
</p>

# Beatbox Passport — 离线工作室

AI Passport 衍生应用：循环电子节奏风格、调节 BPM，并开始／停止流式程序化循环，全程离线。复用 BSP 音频／显示／按键；自有 LVGL 工作室界面（不使用基线 demo 菜单或 `ui_pixel`）。

## 界面（里程碑 1）

| 元素 | 行为 |
| --- | --- |
| Passport 卡片 | 占位名 `Guest` 与头像标记 `BB`（手机 BLE 同步名字／头像留待后续） |
| 风格 | Trap / House / Lo-fi / Boom Bap，内建 16 步节奏库 |
| BPM | 60–160，步进 5；切风格时载入该风格默认速度（循环播放中则保留用户速度） |
| 步进条 | 高度表示 kick／snare／hat 密度 |
| 电量 | 可用时右上角显示 SOC |

### 三键

| 键 | 动作 |
| --- | --- |
| 上 | 切换风格 |
| 下 | 调速 |
| 确定 | 开始／停止循环 |

## 音频引擎

- 采样率 **16 kHz** 单声道 PCM16，经 `bsp_audio_*`
- 程序化 kick／snare／hat（无大采样 RAM；I2S 每次 256 采样）
- FreeRTOS 工作任务持续渲染；按键回调只投递轻量 UI 事件
- 本离线构建在 `sdkconfig.defaults` 中关闭蓝牙以腾出内部 RAM

## BPM／pattern 分析（里程碑 1 桩）

`beatbox_analyze_bpm()` 目前返回 `BEATBOX_BPM_DEFAULT` 并标记为桩实现。麦克风录音与离线 onset／自相关分析计划在里程碑 2。在此之前，界面上的「BPM analysis」应视为 **Unverified**。风格节奏本身是确定性内建库。

## 验证

```bash
./tools/validate.sh
```

未经明确确认不烧录。交付从 `0x0` 刷入的合并镜像。

## 后续里程碑

1. ~~UI＋风格／速度／循环＋离线合成~~（本文档）
2. 录音＋本地 BPM／pattern 分析接入生成
3. 可选：手机 BLE 同步名字／头像
