#include "step_detector.h"

#include <math.h>
#include <string.h>
#include "esp_timer.h"

#define HIST_LEN 200  // 2 s at 100 Hz

static step_cfg_t s_cfg;
static float s_lp_state;
static float s_lp_alpha;
static float s_hist[HIST_LEN];
static int   s_hist_idx;
static int   s_hist_count;
static int64_t s_last_step_us;
static int64_t s_step_times_us[16];
static int   s_step_times_head;
static uint32_t s_total_steps;

static void update_history(float v)
{
    s_hist[s_hist_idx] = v;
    s_hist_idx = (s_hist_idx + 1) % HIST_LEN;
    if (s_hist_count < HIST_LEN) s_hist_count++;
}

static void hist_stats(float *mean, float *stddev)
{
    if (s_hist_count == 0) { *mean = 0; *stddev = 0; return; }
    float sum = 0.0f;
    for (int i = 0; i < s_hist_count; i++) sum += s_hist[i];
    float m = sum / s_hist_count;
    float ss = 0.0f;
    for (int i = 0; i < s_hist_count; i++) {
        float d = s_hist[i] - m;
        ss += d * d;
    }
    *mean = m;
    *stddev = sqrtf(ss / s_hist_count);
}

void step_detector_init(const step_cfg_t *cfg)
{
    s_cfg = *cfg;
    float dt = 1.0f / cfg->sample_rate_hz;
    float rc = 1.0f / (2.0f * 3.14159265f * cfg->lp_cutoff_hz);
    s_lp_alpha = dt / (rc + dt);
    s_lp_state = 0.0f;
    s_hist_idx = 0;
    s_hist_count = 0;
    s_last_step_us = 0;
    s_step_times_head = 0;
    memset(s_step_times_us, 0, sizeof(s_step_times_us));
    s_total_steps = 0;
}

void step_detector_reset(void)
{
    s_total_steps = 0;
    s_last_step_us = 0;
    s_hist_idx = 0;
    s_hist_count = 0;
}

bool step_detector_update(float ax, float ay, float az, step_state_t *state)
{
    float mag = sqrtf(ax * ax + ay * ay + az * az) - 1.0f;
    s_lp_state += s_lp_alpha * (mag - s_lp_state);

    float mean, std;
    hist_stats(&mean, &std);
    update_history(s_lp_state);

    float threshold = mean + s_cfg.k_threshold * std;
    int64_t now = esp_timer_get_time();
    int64_t dt_us = now - s_last_step_us;

    bool step = false;
    if (s_lp_state > threshold &&
        dt_us > (int64_t)s_cfg.min_step_interval_ms * 1000 &&
        s_hist_count > HIST_LEN / 2) {
        step = true;
        s_total_steps++;
        s_last_step_us = now;
        s_step_times_us[s_step_times_head] = now;
        s_step_times_head = (s_step_times_head + 1) % 16;
    }

    int64_t oldest = s_step_times_us[s_step_times_head];
    float cadence_spm = 0.0f;
    if (oldest > 0 && s_total_steps >= 16) {
        cadence_spm = 16.0f * 60.0f / ((now - oldest) / 1.0e6f);
    }
    state->step_count = s_total_steps;
    state->cadence_spm = cadence_spm;
    return step;
}
