#include "meihua_cast.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_year_zhi(void)
{
    assert(meihua_year_zhi_num(1984) == 1);  /* 甲子 */
    assert(meihua_year_zhi_num(1985) == 2);  /* 乙丑 */
    assert(meihua_year_zhi_num(2026) == 7);  /* 丙午 → 午=7 */
}

static void test_shichen(void)
{
    assert(meihua_shichen_num(23) == 1);
    assert(meihua_shichen_num(0) == 1);
    assert(meihua_shichen_num(1) == 2);
    assert(meihua_shichen_num(12) == 7);
    assert(meihua_shichen_num(22) == 12);
}

static void test_known_cast(void)
{
    /* year_zhi=7 (2026), month=11, day=15, hour_num for 10:00 → 丑? 
     * 10:00 → (10+1)/2+1 = 6 → 巳时=6
     * ymd = 7+11+15 = 33 → 33%8 = 1 → 乾
     * ymdh = 33+6 = 39 → 39%8 = 7 → 艮; 39%6 = 3 → 动三爻
     */
    meihua_civil_time_t when = { .year = 2026, .month = 11, .day = 15, .hour = 10 };
    meihua_cast_t cast;
    assert(meihua_cast_from_civil(&when, &cast));
    assert(cast.year_num == 7);
    assert(cast.month_num == 11);
    assert(cast.day_num == 15);
    assert(cast.hour_num == 6);
    assert(cast.upper == MEIHUA_TRIGRAM_QIAN);
    assert(cast.lower == MEIHUA_TRIGRAM_GEN);
    assert(cast.moving_line == 3);
    /* 动在下卦 → 下为用、上为体 */
    assert(cast.ti == MEIHUA_TRIGRAM_QIAN);
    assert(cast.yong == MEIHUA_TRIGRAM_GEN);
    assert(cast.ben_lines[2] == 1); /* 震/艮 lower: 艮=100 → bottom yin yin yang? 
                                     * 艮 bits 0x4 = 100 → line1=0,line2=0,line3=1 */
    assert(cast.ben_lines[0] == 0);
    assert(cast.ben_lines[1] == 0);
    assert(cast.ben_lines[2] == 1);
    /* 变卦 flips line 3 */
    assert(cast.bian_lines[2] == 0);
}

static void test_relation(void)
{
    /* 乾金 体, 艮土 用 → 土生金 → 用生体 */
    assert(meihua_ti_yong_relation(MEIHUA_TRIGRAM_QIAN, MEIHUA_TRIGRAM_GEN) ==
           MEIHUA_REL_GENERATED);
    assert(strstr(meihua_relation_verdict(MEIHUA_REL_GENERATED), "用生体") != NULL);
}

static void test_invalid(void)
{
    meihua_cast_t cast;
    meihua_civil_time_t bad = { .year = 2026, .month = 13, .day = 1, .hour = 0 };
    assert(!meihua_cast_from_civil(&bad, &cast));
    assert(!meihua_cast_from_civil(NULL, &cast));
}

int main(void)
{
    test_year_zhi();
    test_shichen();
    test_known_cast();
    test_relation();
    test_invalid();
    puts("test_meihua_cast: PASS");
    return 0;
}
