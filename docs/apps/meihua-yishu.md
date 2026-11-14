<p align="right">
  <a href="meihua-yishu.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Mei Hua Yi Shu — Instant Ask

Derivative AI Passport application: pick a short question, cast from the
present civil time, and show the primary, nuclear, and changed hexagrams plus
body/use roles with a brief verdict.

## Screens (milestone 1)

| Screen | Controls |
| --- | --- |
| Ask | UP/DOWN cycle the question bank; OK casts immediately |
| Result | Three hexagram columns, body/use, short verdict; OK returns; OK long-press casts again |

Battery SOC appears top-right when the gauge is available. The UI is a new
ink-on-paper LVGL shell; it does not use the baseline demo menu or `ui_pixel`.

## Time-based casting method

This firmware uses a documented classical time-casting procedure (Shao Yong
Mei Hua Yi Shu style), applied to the device civil clock:

1. **Year number** — earthly-branch ordinal of the civil year:
   `((year - 4) % 12) + 1` (Zi=1 … Hai=12). Example: 1984 → 1, 2026 → 7 (Wu).
2. **Month / day** — civil month (1–12) and day-of-month (1–31).
3. **Hour number** — Chinese double-hour 1–12 from the local clock hour
   (Zi straddles 23:00–00:59).
4. **Upper trigram** — `(year + month + day) % 8`, remainder `0` → `8` (Kun).
5. **Lower trigram** — `(year + month + day + hour) % 8`, `0` → `8`.
6. **Moving line** — `(year + month + day + hour) % 6`, `0` → `6` (bottom=1).

Trigram numbering is the Earlier Heaven set used by Mei Hua Yi Shu:
Qian1 Dui2 Li3 Zhen4 Xun5 Kan6 Gen7 Kun8.

The **nuclear hexagram** uses lines 2–3–4 as the lower trigram and 3–4–5 as
the upper. The **changed hexagram** flips the moving line of the primary.
**Body/use**: if the moving line is in the lower trigram (1–3), lower=use and
upper=body; if in the upper (4–6), upper=use and lower=body.

Milestone-1 verdicts are short five-phase body/use lines (same / body generates
use / use generates body / body overcomes use / use overcomes body). Richer
oracle text and Flash history are planned for a later milestone.

If the RTC is unset (`year < 2020`), the app falls back to the documented
civil stamp `2026-11-15 10:00` so offline casts remain deterministic; set the
clock for live timing.

## Chinese font

Milestone 1 ships `assets/fonts/meihua_font_16.c`: a 16 px / 4 bpp LVGL subset
of Adobe Source Han Sans SC (OFL) covering the ask/result UI strings, trigram
names, ASCII digits, and Chinese punctuation. Regenerate with `lv_font_conv`
when strings change. Device rendering of every label remains an on-device check.

## Validation

```bash
./tools/validate.sh
```

Flash only after explicit approval. Deliver the merged image from offset `0x0`.
