// main/beatbox_engine.c — procedural drum bank + pattern logic (no ESP-IDF).
#include "beatbox_engine.h"

#include <string.h>

static const char *const s_style_names[BEATBOX_STYLE_COUNT] = {
    "Trap",
    "House",
    "Lo-fi",
    "Boom Bap",
};

/* Built-in 16-step banks: kick / snare / hat bitmasks. */
static const uint8_t s_trap[BEATBOX_STEPS] = {
    BEATBOX_HIT_KICK | BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, 0, BEATBOX_HIT_HAT,
    BEATBOX_HIT_SNARE | BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_KICK, BEATBOX_HIT_HAT,
    BEATBOX_HIT_KICK | BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, 0, BEATBOX_HIT_HAT,
    BEATBOX_HIT_SNARE | BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_KICK | BEATBOX_HIT_HAT, BEATBOX_HIT_HAT,
};

static const uint8_t s_house[BEATBOX_STEPS] = {
    BEATBOX_HIT_KICK | BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_HAT,
    BEATBOX_HIT_KICK | BEATBOX_HIT_SNARE | BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_HAT,
    BEATBOX_HIT_KICK | BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_HAT,
    BEATBOX_HIT_KICK | BEATBOX_HIT_SNARE | BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_HAT, BEATBOX_HIT_HAT,
};

static const uint8_t s_lofi[BEATBOX_STEPS] = {
    BEATBOX_HIT_KICK | BEATBOX_HIT_HAT, 0, BEATBOX_HIT_HAT, 0,
    BEATBOX_HIT_SNARE, BEATBOX_HIT_HAT, BEATBOX_HIT_KICK, 0,
    BEATBOX_HIT_HAT, 0, BEATBOX_HIT_KICK | BEATBOX_HIT_HAT, 0,
    BEATBOX_HIT_SNARE, 0, BEATBOX_HIT_HAT, BEATBOX_HIT_HAT,
};

static const uint8_t s_boom[BEATBOX_STEPS] = {
    BEATBOX_HIT_KICK, BEATBOX_HIT_HAT, 0, BEATBOX_HIT_HAT,
    BEATBOX_HIT_SNARE, 0, BEATBOX_HIT_HAT, BEATBOX_HIT_KICK,
    0, BEATBOX_HIT_HAT, BEATBOX_HIT_KICK, 0,
    BEATBOX_HIT_SNARE, BEATBOX_HIT_HAT, 0, BEATBOX_HIT_HAT,
};

const char *beatbox_style_name(beatbox_style_t style)
{
    if (style < 0 || style >= BEATBOX_STYLE_COUNT) {
        return "?";
    }
    return s_style_names[style];
}

beatbox_style_t beatbox_style_next(beatbox_style_t style)
{
    int next = ((int)style + 1) % (int)BEATBOX_STYLE_COUNT;
    return (beatbox_style_t)next;
}

int beatbox_clamp_bpm(int bpm)
{
    if (bpm < BEATBOX_BPM_MIN) {
        return BEATBOX_BPM_MIN;
    }
    if (bpm > BEATBOX_BPM_MAX) {
        return BEATBOX_BPM_MAX;
    }
    /* Snap to step grid. */
    int offset = bpm - BEATBOX_BPM_MIN;
    int steps = (offset + BEATBOX_BPM_STEP / 2) / BEATBOX_BPM_STEP;
    return BEATBOX_BPM_MIN + steps * BEATBOX_BPM_STEP;
}

int beatbox_bump_bpm(int bpm)
{
    int next = beatbox_clamp_bpm(bpm) + BEATBOX_BPM_STEP;
    if (next > BEATBOX_BPM_MAX) {
        return BEATBOX_BPM_MIN;
    }
    return next;
}

void beatbox_pattern_for_style(beatbox_style_t style, beatbox_pattern_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->style = style;
    out->bpm = BEATBOX_BPM_DEFAULT;
    const uint8_t *src = s_trap;
    switch (style) {
    case BEATBOX_STYLE_HOUSE:
        src = s_house;
        out->bpm = 120;
        break;
    case BEATBOX_STYLE_LOFI:
        src = s_lofi;
        out->bpm = 80;
        break;
    case BEATBOX_STYLE_BOOM_BAP:
        src = s_boom;
        out->bpm = 90;
        break;
    case BEATBOX_STYLE_TRAP:
    default:
        src = s_trap;
        out->bpm = 140;
        out->style = BEATBOX_STYLE_TRAP;
        break;
    }
    memcpy(out->steps, src, BEATBOX_STEPS);
}

uint32_t beatbox_loop_samples(int bpm)
{
    bpm = beatbox_clamp_bpm(bpm);
    /* One bar of 16 sixteenth-notes: samples = 60/bpm * 4 beats * sample_rate. */
    return (uint32_t)((uint64_t)BEATBOX_SAMPLE_RATE * 60u * 4u / (uint32_t)bpm);
}

static void trigger_hits(beatbox_voice_t *v, uint8_t hits)
{
    if (hits & BEATBOX_HIT_KICK) {
        v->kick_remain = BEATBOX_SAMPLE_RATE / 8; /* ~125 ms */
        v->kick_phase = 0;
    }
    if (hits & BEATBOX_HIT_SNARE) {
        v->snare_remain = BEATBOX_SAMPLE_RATE / 10;
        v->snare_phase = 0;
    }
    if (hits & BEATBOX_HIT_HAT) {
        v->hat_remain = BEATBOX_SAMPLE_RATE / 40; /* ~25 ms */
        v->hat_phase = 0;
    }
}

void beatbox_voice_init(beatbox_voice_t *v, const beatbox_pattern_t *pattern)
{
    if (!v) {
        return;
    }
    memset(v, 0, sizeof(*v));
    v->last_step = -1;
    if (pattern) {
        beatbox_voice_set_pattern(v, pattern);
    }
}

void beatbox_voice_set_pattern(beatbox_voice_t *v, const beatbox_pattern_t *pattern)
{
    if (!v || !pattern) {
        return;
    }
    v->style = pattern->style;
    v->bpm = beatbox_clamp_bpm(pattern->bpm);
    memcpy(v->steps, pattern->steps, BEATBOX_STEPS);
    v->loop_samples = beatbox_loop_samples(v->bpm);
    if (v->sample_pos >= v->loop_samples) {
        v->sample_pos = 0;
        v->last_step = -1;
    }
}

static int16_t mix_sat(int32_t acc)
{
    if (acc > 32767) {
        return 32767;
    }
    if (acc < -32768) {
        return -32768;
    }
    return (int16_t)acc;
}

/* Tiny LCG noise for snare/hat without libc rand (host + device). */
static uint32_t s_noise = 0xA5A5u;

static int16_t next_noise(void)
{
    s_noise = s_noise * 1664525u + 1013904223u;
    return (int16_t)((int32_t)(s_noise >> 16) - 32768);
}

void beatbox_voice_render(beatbox_voice_t *v, int16_t *out, size_t n)
{
    if (!v || !out || n == 0 || v->loop_samples == 0) {
        if (out && n) {
            memset(out, 0, n * sizeof(int16_t));
        }
        return;
    }

    for (size_t i = 0; i < n; i++) {
        uint32_t step_samples = v->loop_samples / BEATBOX_STEPS;
        if (step_samples == 0) {
            step_samples = 1;
        }
        int step = (int)(v->sample_pos / step_samples);
        if (step >= BEATBOX_STEPS) {
            step = BEATBOX_STEPS - 1;
        }
        if (step != v->last_step) {
            trigger_hits(v, v->steps[step]);
            v->last_step = step;
        }

        int32_t acc = 0;

        if (v->kick_remain > 0) {
            /* Soft sine-ish kick via triangle + decay. */
            int period = BEATBOX_SAMPLE_RATE / 55;
            int half = period / 2;
            int16_t tri = (v->kick_phase < half) ? 9000 : -9000;
            int env = v->kick_remain;
            acc += (tri * env) / (BEATBOX_SAMPLE_RATE / 8);
            if (++v->kick_phase >= period) {
                v->kick_phase = 0;
                /* Pitch drop: stretch period slightly. */
                period += 1;
                (void)period;
            }
            v->kick_remain--;
        }

        if (v->snare_remain > 0) {
            int16_t nse = next_noise() / 4;
            int env = v->snare_remain;
            acc += (nse * env) / (BEATBOX_SAMPLE_RATE / 10);
            /* Brief body tone. */
            int period = BEATBOX_SAMPLE_RATE / 180;
            int16_t tone = (v->snare_phase < period / 2) ? 4000 : -4000;
            acc += (tone * env) / (BEATBOX_SAMPLE_RATE / 10);
            if (++v->snare_phase >= period) {
                v->snare_phase = 0;
            }
            v->snare_remain--;
        }

        if (v->hat_remain > 0) {
            int16_t nse = next_noise() / 8;
            int env = v->hat_remain;
            acc += (nse * env) / (BEATBOX_SAMPLE_RATE / 40);
            v->hat_remain--;
            v->hat_phase++;
        }

        out[i] = mix_sat(acc);

        if (++v->sample_pos >= v->loop_samples) {
            v->sample_pos = 0;
            v->last_step = -1;
        }
    }
}

int beatbox_analyze_bpm(const int16_t *pcm, size_t samples, uint32_t sample_rate,
                        bool *stubbed)
{
    (void)pcm;
    (void)samples;
    (void)sample_rate;
    if (stubbed) {
        *stubbed = true;
    }
    return BEATBOX_BPM_DEFAULT;
}
