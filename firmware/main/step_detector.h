#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float    sample_rate_hz;
    float    lp_cutoff_hz;          // low-pass on acceleration magnitude
    uint32_t min_step_interval_ms;  // refractory period between peaks
    float    k_threshold;           // adaptive threshold = mean + k * std
} step_cfg_t;

typedef struct {
    uint32_t step_count;
    float    cadence_spm;           // steps per minute (rolling)
} step_state_t;

void step_detector_init(const step_cfg_t *cfg);

// Feed a single accelerometer sample (ax,ay,az in g) and return true on detected step.
bool step_detector_update(float ax, float ay, float az, step_state_t *state);

void step_detector_reset(void);
