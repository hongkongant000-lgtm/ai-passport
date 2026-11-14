<p align="right">
  <a href="beatbox-passport.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Beatbox Passport — Offline Studio

Derivative AI Passport application: cycle electronic beatbox styles, adjust
tempo, and start/stop a streaming procedural loop entirely offline. Reuses BSP
audio/display/buttons; ships a redesigned LVGL studio shell (not the baseline
demo menu or `ui_pixel`).

## Screens (milestone 1)

| Element | Behavior |
| --- | --- |
| Passport card | Placeholder name `Guest` and avatar mark `BB` (BLE name/avatar sync later) |
| Style | Trap / House / Lo-fi / Boom Bap with built-in 16-step banks |
| BPM | 60–160 in steps of 5; style change loads a style default tempo unless a loop is already playing |
| Step strip | Visual height encodes kick / snare / hat density |
| Battery | SOC top-right when the gauge is available |

### Three-key controls

| Key | Action |
| --- | --- |
| UP | Cycle style |
| DOWN | Bump tempo |
| OK | Start / stop loop |

## Audio engine

- Sample rate **16 kHz** mono PCM16 via `bsp_audio_*`
- Procedural kick / snare / hat (no large sample RAM bank; 256-sample I2S chunks)
- Continuous render in a FreeRTOS worker; button callbacks only enqueue UI work
- Bluetooth disabled in `sdkconfig.defaults` for this offline build to free RAM

## BPM / pattern analysis (milestone 1 stub)

`beatbox_analyze_bpm()` currently returns `BEATBOX_BPM_DEFAULT` and marks the
result as stubbed. Mic capture + offline onset/autocorrelation analysis is
planned for milestone 2. Treat displayed “BPM analysis” as **Unverified** until
that lands. Style patterns themselves are deterministic built-in banks.

## Validation

```bash
./tools/validate.sh
```

Flash only after explicit approval. Deliver the merged image from offset `0x0`.

## Later milestones

1. ~~UI + style/tempo/loop + offline synth~~ (this document)
2. Mic record + local BPM/pattern analysis wired into generation
3. Optional BLE sync for name/avatar from a phone
