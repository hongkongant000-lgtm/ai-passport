#include "beatbox_engine.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_style_cycle(void)
{
    assert(strcmp(beatbox_style_name(BEATBOX_STYLE_TRAP), "Trap") == 0);
    assert(strcmp(beatbox_style_name(BEATBOX_STYLE_HOUSE), "House") == 0);
    assert(strcmp(beatbox_style_name(BEATBOX_STYLE_LOFI), "Lo-fi") == 0);
    assert(strcmp(beatbox_style_name(BEATBOX_STYLE_BOOM_BAP), "Boom Bap") == 0);
    assert(beatbox_style_next(BEATBOX_STYLE_TRAP) == BEATBOX_STYLE_HOUSE);
    assert(beatbox_style_next(BEATBOX_STYLE_BOOM_BAP) == BEATBOX_STYLE_TRAP);
}

static void test_bpm(void)
{
    assert(beatbox_clamp_bpm(0) == BEATBOX_BPM_MIN);
    assert(beatbox_clamp_bpm(999) == BEATBOX_BPM_MAX);
    assert(beatbox_clamp_bpm(92) == 90);
    assert(beatbox_bump_bpm(160) == BEATBOX_BPM_MIN);
    assert(beatbox_bump_bpm(90) == 95);
}

static void test_patterns(void)
{
    beatbox_pattern_t p;
    beatbox_pattern_for_style(BEATBOX_STYLE_HOUSE, &p);
    assert(p.style == BEATBOX_STYLE_HOUSE);
    assert(p.bpm == 120);
    assert((p.steps[0] & BEATBOX_HIT_KICK) != 0);
    assert((p.steps[4] & BEATBOX_HIT_SNARE) != 0);

    beatbox_pattern_for_style(BEATBOX_STYLE_TRAP, &p);
    assert(p.bpm == 140);
    assert(beatbox_loop_samples(120) == (uint32_t)(BEATBOX_SAMPLE_RATE * 60 * 4 / 120));
}

static void test_render_energy(void)
{
    beatbox_pattern_t p;
    beatbox_pattern_for_style(BEATBOX_STYLE_BOOM_BAP, &p);
    p.bpm = 90;
    beatbox_voice_t v;
    beatbox_voice_init(&v, &p);

    int16_t buf[512];
    beatbox_voice_render(&v, buf, 512);
    int64_t energy = 0;
    for (size_t i = 0; i < 512; i++) {
        energy += (int32_t)buf[i] * (int32_t)buf[i];
    }
    assert(energy > 0);

    /* Silence path with null voice should zero. */
    int16_t z[8];
    memset(z, 0x5A, sizeof(z));
    beatbox_voice_render(NULL, z, 8);
    for (int i = 0; i < 8; i++) {
        assert(z[i] == 0);
    }
}

static void test_bpm_stub(void)
{
    bool stubbed = false;
    int bpm = beatbox_analyze_bpm(NULL, 0, BEATBOX_SAMPLE_RATE, &stubbed);
    assert(stubbed);
    assert(bpm == BEATBOX_BPM_DEFAULT);
}

int main(void)
{
    test_style_cycle();
    test_bpm();
    test_patterns();
    test_render_energy();
    test_bpm_stub();
    puts("test_beatbox_engine: PASS");
    return 0;
}
