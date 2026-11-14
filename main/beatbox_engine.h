// main/beatbox_engine.h — offline beatbox pattern + procedural drum synth (host-testable).
//
// Milestone 1: style patterns, BPM clamp, streaming PCM synthesis, and a
// documented BPM-analysis stub (real mic analysis lands in milestone 2).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BEATBOX_STEPS 16
#define BEATBOX_SAMPLE_RATE 16000
#define BEATBOX_BPM_MIN 60
#define BEATBOX_BPM_MAX 160
#define BEATBOX_BPM_STEP 5
#define BEATBOX_BPM_DEFAULT 90

typedef enum {
    BEATBOX_STYLE_TRAP = 0,
    BEATBOX_STYLE_HOUSE,
    BEATBOX_STYLE_LOFI,
    BEATBOX_STYLE_BOOM_BAP,
    BEATBOX_STYLE_COUNT,
} beatbox_style_t;

/* Bit flags per 16th-note step. */
enum {
    BEATBOX_HIT_KICK = 1u << 0,
    BEATBOX_HIT_SNARE = 1u << 1,
    BEATBOX_HIT_HAT = 1u << 2,
};

typedef struct {
    beatbox_style_t style;
    int bpm;
    uint8_t steps[BEATBOX_STEPS];
} beatbox_pattern_t;

typedef struct {
    beatbox_style_t style;
    int bpm;
    uint8_t steps[BEATBOX_STEPS];
    /* Playback cursor in samples from loop start. */
    uint32_t sample_pos;
    uint32_t loop_samples;
    /* Active voice envelopes (samples remaining / phase). */
    int kick_remain;
    int snare_remain;
    int hat_remain;
    int kick_phase;
    int snare_phase;
    int hat_phase;
    int last_step;
} beatbox_voice_t;

const char *beatbox_style_name(beatbox_style_t style);
beatbox_style_t beatbox_style_next(beatbox_style_t style);
int beatbox_clamp_bpm(int bpm);
int beatbox_bump_bpm(int bpm);

/* Fill pattern steps for the given style (deterministic built-in bank). */
void beatbox_pattern_for_style(beatbox_style_t style, beatbox_pattern_t *out);

/* Samples in one 16-step bar at the given BPM. */
uint32_t beatbox_loop_samples(int bpm);

void beatbox_voice_init(beatbox_voice_t *v, const beatbox_pattern_t *pattern);
void beatbox_voice_set_pattern(beatbox_voice_t *v, const beatbox_pattern_t *pattern);

/* Render mono PCM16 LE into out[n]. Advances the voice. */
void beatbox_voice_render(beatbox_voice_t *v, int16_t *out, size_t n);

/*
 * Milestone-1 BPM analysis stub.
 * Does not inspect pcm yet; returns BEATBOX_BPM_DEFAULT and sets *stubbed=true
 * when stubbed is non-NULL. Milestone 2 will replace this with offline onset /
 * autocorrelation analysis. Callers must treat stubbed results as Unverified.
 */
int beatbox_analyze_bpm(const int16_t *pcm, size_t samples, uint32_t sample_rate,
                        bool *stubbed);

#ifdef __cplusplus
}
#endif
