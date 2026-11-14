// main/meihua_cast.h — Mei Hua Yi Shu time-based casting (pure logic, host-testable).
//
// Classical method documented in docs/apps/meihua-yishu.md:
//   year_num  = 地支序数 for the civil year ((year - 4) % 12) + 1  (子=1 … 亥=12)
//   month_num = civil month 1..12
//   day_num   = civil day-of-month 1..31
//   hour_num  = 时辰 1..12 from local hour (子时 straddles 23:00–00:59)
//   upper     = (year + month + day) % 8;        0 → 8
//   lower     = (year + month + day + hour) % 8; 0 → 8
//   moving    = (year + month + day + hour) % 6; 0 → 6
// Trigram numbers follow 先天八卦数 used by 梅花易数.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEIHUA_TRIGRAM_QIAN = 1, /* 乾 */
    MEIHUA_TRIGRAM_DUI  = 2, /* 兑 */
    MEIHUA_TRIGRAM_LI   = 3, /* 离 */
    MEIHUA_TRIGRAM_ZHEN = 4, /* 震 */
    MEIHUA_TRIGRAM_XUN  = 5, /* 巽 */
    MEIHUA_TRIGRAM_KAN  = 6, /* 坎 */
    MEIHUA_TRIGRAM_GEN  = 7, /* 艮 */
    MEIHUA_TRIGRAM_KUN  = 8, /* 坤 */
} meihua_trigram_t;

typedef enum {
    MEIHUA_ELEMENT_METAL = 0,
    MEIHUA_ELEMENT_WOOD  = 1,
    MEIHUA_ELEMENT_WATER = 2,
    MEIHUA_ELEMENT_FIRE  = 3,
    MEIHUA_ELEMENT_EARTH = 4,
} meihua_element_t;

typedef enum {
    MEIHUA_REL_SAME = 0,      /* 比和 */
    MEIHUA_REL_GENERATE = 1,  /* 体生用 */
    MEIHUA_REL_GENERATED = 2, /* 用生体 */
    MEIHUA_REL_OVERCOME = 3,  /* 体克用 */
    MEIHUA_REL_OVERCOME_BY = 4, /* 用克体 */
} meihua_relation_t;

typedef struct {
    int year;
    int month;
    int day;
    int hour; /* 0..23 local clock hour */
} meihua_civil_time_t;

typedef struct {
    uint8_t year_num;
    uint8_t month_num;
    uint8_t day_num;
    uint8_t hour_num;
    meihua_trigram_t upper;
    meihua_trigram_t lower;
    uint8_t moving_line; /* 1..6, bottom=1 */
    /* Six lines bottom→top as 0=yin, 1=yang for 本卦. */
    uint8_t ben_lines[6];
    uint8_t hu_lines[6];
    uint8_t bian_lines[6];
    meihua_trigram_t hu_upper;
    meihua_trigram_t hu_lower;
    meihua_trigram_t bian_upper;
    meihua_trigram_t bian_lower;
    meihua_trigram_t ti; /* 体 */
    meihua_trigram_t yong; /* 用 */
    meihua_relation_t relation;
} meihua_cast_t;

uint8_t meihua_year_zhi_num(int civil_year);
uint8_t meihua_shichen_num(int hour_0_23);
meihua_element_t meihua_trigram_element(meihua_trigram_t t);
meihua_relation_t meihua_ti_yong_relation(meihua_trigram_t ti, meihua_trigram_t yong);

/* Fill cast from civil wall-clock fields. Returns false on invalid calendar inputs. */
bool meihua_cast_from_civil(const meihua_civil_time_t *when, meihua_cast_t *out);

/* Compact UTF-8 names for UI (Simplified Chinese). */
const char *meihua_trigram_name(meihua_trigram_t t);
const char *meihua_trigram_symbol(meihua_trigram_t t);
const char *meihua_relation_verdict(meihua_relation_t r);

#ifdef __cplusplus
}
#endif
