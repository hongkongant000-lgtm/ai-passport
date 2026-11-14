#include "meihua_cast.h"

#include <stddef.h>
#include <string.h>

/* Trigram binary: bit0 = bottom line. 1=yang, 0=yin. Indices 1..8. */
static const uint8_t s_trigram_bits[9] = {
    0,
    0x7, /* 乾 111 */
    0x3, /* 兑 011 */
    0x5, /* 离 101 */
    0x1, /* 震 001 */
    0x6, /* 巽 110 */
    0x2, /* 坎 010 */
    0x4, /* 艮 100 */
    0x0, /* 坤 000 */
};

static const meihua_element_t s_trigram_element[9] = {
    MEIHUA_ELEMENT_EARTH,
    MEIHUA_ELEMENT_METAL, /* 乾 */
    MEIHUA_ELEMENT_METAL, /* 兑 */
    MEIHUA_ELEMENT_FIRE,  /* 离 */
    MEIHUA_ELEMENT_WOOD,  /* 震 */
    MEIHUA_ELEMENT_WOOD,  /* 巽 */
    MEIHUA_ELEMENT_WATER, /* 坎 */
    MEIHUA_ELEMENT_EARTH, /* 艮 */
    MEIHUA_ELEMENT_EARTH, /* 坤 */
};

static const char *const s_trigram_name[9] = {
    "?", "乾", "兑", "离", "震", "巽", "坎", "艮", "坤",
};

static const char *const s_trigram_symbol[9] = {
    "?", "☰", "☱", "☲", "☳", "☴", "☵", "☶", "☷",
};

static uint8_t rem_or_full(unsigned sum, unsigned mod)
{
    unsigned r = sum % mod;
    return (uint8_t)(r == 0 ? mod : r);
}

static bool valid_trigram(meihua_trigram_t t)
{
    return t >= MEIHUA_TRIGRAM_QIAN && t <= MEIHUA_TRIGRAM_KUN;
}

static void pack_lines(meihua_trigram_t lower, meihua_trigram_t upper, uint8_t lines[6])
{
    uint8_t lo = s_trigram_bits[lower];
    uint8_t hi = s_trigram_bits[upper];
    for (int i = 0; i < 3; i++) {
        lines[i] = (uint8_t)((lo >> i) & 1u);
        lines[i + 3] = (uint8_t)((hi >> i) & 1u);
    }
}

static meihua_trigram_t bits_to_trigram(uint8_t bits3)
{
    for (meihua_trigram_t t = MEIHUA_TRIGRAM_QIAN; t <= MEIHUA_TRIGRAM_KUN; t++) {
        if (s_trigram_bits[t] == (bits3 & 0x7u)) {
            return t;
        }
    }
    return MEIHUA_TRIGRAM_KUN;
}

static void lines_to_trigrams(const uint8_t lines[6],
                              meihua_trigram_t *lower,
                              meihua_trigram_t *upper)
{
    uint8_t lo = (uint8_t)((lines[0]) | (lines[1] << 1) | (lines[2] << 2));
    uint8_t hi = (uint8_t)((lines[3]) | (lines[4] << 1) | (lines[5] << 2));
    *lower = bits_to_trigram(lo);
    *upper = bits_to_trigram(hi);
}

uint8_t meihua_year_zhi_num(int civil_year)
{
    /* 1984 = 甲子 → 子=1. (year - 4) % 12 == 0 → 子. */
    int z = (civil_year - 4) % 12;
    if (z < 0) {
        z += 12;
    }
    return (uint8_t)(z + 1);
}

uint8_t meihua_shichen_num(int hour_0_23)
{
    /* 子时: 23:00–00:59 → 1; 丑 1–2 → 2; … 亥 21–22 → 12. */
    if (hour_0_23 < 0) {
        hour_0_23 = 0;
    }
    hour_0_23 %= 24;
    if (hour_0_23 == 23 || hour_0_23 == 0) {
        return 1;
    }
    return (uint8_t)((hour_0_23 + 1) / 2 + 1);
}

meihua_element_t meihua_trigram_element(meihua_trigram_t t)
{
    if (!valid_trigram(t)) {
        return MEIHUA_ELEMENT_EARTH;
    }
    return s_trigram_element[t];
}

meihua_relation_t meihua_ti_yong_relation(meihua_trigram_t ti, meihua_trigram_t yong)
{
    static const meihua_element_t generates[5] = {
        /* metal→water, wood→fire, water→wood, fire→earth, earth→metal */
        MEIHUA_ELEMENT_WATER,
        MEIHUA_ELEMENT_FIRE,
        MEIHUA_ELEMENT_WOOD,
        MEIHUA_ELEMENT_EARTH,
        MEIHUA_ELEMENT_METAL,
    };
    static const meihua_element_t overcomes[5] = {
        /* metal→wood, wood→earth, water→fire, fire→metal, earth→water */
        MEIHUA_ELEMENT_WOOD,
        MEIHUA_ELEMENT_EARTH,
        MEIHUA_ELEMENT_FIRE,
        MEIHUA_ELEMENT_METAL,
        MEIHUA_ELEMENT_WATER,
    };

    meihua_element_t a = meihua_trigram_element(ti);
    meihua_element_t b = meihua_trigram_element(yong);
    if (a == b) {
        return MEIHUA_REL_SAME;
    }
    if (generates[a] == b) {
        return MEIHUA_REL_GENERATE; /* 体生用 */
    }
    if (generates[b] == a) {
        return MEIHUA_REL_GENERATED; /* 用生体 */
    }
    if (overcomes[a] == b) {
        return MEIHUA_REL_OVERCOME; /* 体克用 */
    }
    if (overcomes[b] == a) {
        return MEIHUA_REL_OVERCOME_BY; /* 用克体 */
    }
    return MEIHUA_REL_SAME;
}

bool meihua_cast_from_civil(const meihua_civil_time_t *when, meihua_cast_t *out)
{
    if (!when || !out) {
        return false;
    }
    if (when->year < 1 || when->month < 1 || when->month > 12 ||
        when->day < 1 || when->day > 31 || when->hour < 0 || when->hour > 23) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->year_num = meihua_year_zhi_num(when->year);
    out->month_num = (uint8_t)when->month;
    out->day_num = (uint8_t)when->day;
    out->hour_num = meihua_shichen_num(when->hour);

    unsigned ymd = (unsigned)out->year_num + out->month_num + out->day_num;
    unsigned ymdh = ymd + out->hour_num;
    out->upper = (meihua_trigram_t)rem_or_full(ymd, 8);
    out->lower = (meihua_trigram_t)rem_or_full(ymdh, 8);
    out->moving_line = rem_or_full(ymdh, 6);

    pack_lines(out->lower, out->upper, out->ben_lines);

    /* 互卦: lines 2,3,4 → lower; 3,4,5 → upper (1-based). */
    out->hu_lines[0] = out->ben_lines[1];
    out->hu_lines[1] = out->ben_lines[2];
    out->hu_lines[2] = out->ben_lines[3];
    out->hu_lines[3] = out->ben_lines[2];
    out->hu_lines[4] = out->ben_lines[3];
    out->hu_lines[5] = out->ben_lines[4];
    lines_to_trigrams(out->hu_lines, &out->hu_lower, &out->hu_upper);

    memcpy(out->bian_lines, out->ben_lines, sizeof(out->ben_lines));
    out->bian_lines[out->moving_line - 1u] ^= 1u;
    lines_to_trigrams(out->bian_lines, &out->bian_lower, &out->bian_upper);

    /* 动爻在下卦(1–3)则下为用、上为体；在上卦(4–6)则上为用、下为体。 */
    if (out->moving_line <= 3) {
        out->yong = out->lower;
        out->ti = out->upper;
    } else {
        out->yong = out->upper;
        out->ti = out->lower;
    }
    out->relation = meihua_ti_yong_relation(out->ti, out->yong);
    return true;
}

const char *meihua_trigram_name(meihua_trigram_t t)
{
    if (!valid_trigram(t)) {
        return "?";
    }
    return s_trigram_name[t];
}

const char *meihua_trigram_symbol(meihua_trigram_t t)
{
    if (!valid_trigram(t)) {
        return "?";
    }
    return s_trigram_symbol[t];
}

const char *meihua_relation_verdict(meihua_relation_t r)
{
    switch (r) {
    case MEIHUA_REL_SAME:
        return "体用比和，事多平稳。";
    case MEIHUA_REL_GENERATE:
        return "体生用，费力而事可成。";
    case MEIHUA_REL_GENERATED:
        return "用生体，得助而顺。";
    case MEIHUA_REL_OVERCOME:
        return "体克用，可主动进取。";
    case MEIHUA_REL_OVERCOME_BY:
        return "用克体，宜守不宜争。";
    default:
        return "静观其变。";
    }
}
