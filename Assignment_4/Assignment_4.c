#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "mic_i2s.h"

// used pins:
#define MIC_SCK_PIN   3
#define MIC_WS_PIN    (MIC_SCK_PIN + 1)
#define MIC_DATA_PIN  2
#define LED_PIN       16

// recording config:
#define MIC_PIO_NUM         0
#define MIC_NUM_SAMPLES     2048
#define MIC_SAMPLE_FREQ     24000

// FIR bandpass settings for a 500 Hz tone detector.
// A 120 Hz passband is narrow enough to reject nearby content while
// still keeping the filter short enough for real-time use.
#define TARGET_FREQ_HZ                 500
#define BANDPASS_LOW_HZ                440
#define BANDPASS_HIGH_HZ               560
#define FIR_TAP_COUNT                  81
#define MIN_RAW_PEAK_AMPLITUDE         4000
#define MIN_FILTERED_PEAK_AMPLITUDE    1000
#define MIN_BAND_ENERGY_RATIO_PERMILLE 180

typedef struct {
  char channel_name;
  uint64_t raw_energy;
  uint64_t band_energy;
  uint32_t band_energy_ratio_permille;
  int32_t raw_peak_amplitude;
  int32_t filtered_peak_amplitude;
  bool tone_detected;
} tone_result_t;

static float bandpass_taps[FIR_TAP_COUNT];

static int32_t extract_sample(uint32_t raw_word)
{
  return ((int32_t) raw_word) >> 8;
}

static float normalized_sinc(float x)
{
  if (fabsf(x) < 1.0e-6f) {
    return 1.0f;
  }
  return sinf((float) M_PI * x) / ((float) M_PI * x);
}

static void init_bandpass_filter(void)
{
  const int mid = FIR_TAP_COUNT / 2;
  const float low_cutoff = (float) BANDPASS_LOW_HZ / (float) MIC_SAMPLE_FREQ;
  const float high_cutoff = (float) BANDPASS_HIGH_HZ / (float) MIC_SAMPLE_FREQ;
  const float target_omega = 2.0f * (float) M_PI * ((float) TARGET_FREQ_HZ / (float) MIC_SAMPLE_FREQ);
  float center_gain = 0.0f;

  for (int tap = 0; tap < FIR_TAP_COUNT; ++tap) {
    float offset = (float) (tap - mid);
    float ideal = (2.0f * high_cutoff * normalized_sinc(2.0f * high_cutoff * offset)) -
                  (2.0f * low_cutoff * normalized_sinc(2.0f * low_cutoff * offset));
    float window = 0.54f - 0.46f * cosf((2.0f * (float) M_PI * (float) tap) / (float) (FIR_TAP_COUNT - 1));
    bandpass_taps[tap] = ideal * window;
    center_gain += bandpass_taps[tap] * cosf(target_omega * offset);
  }

  if (fabsf(center_gain) < 1.0e-6f) {
    return;
  }

  for (int tap = 0; tap < FIR_TAP_COUNT; ++tap) {
    bandpass_taps[tap] /= center_gain;
  }
}

static tone_result_t analyze_channel(const uint32_t *samples, size_t num_samples, size_t start_index, char channel_name)
{
  size_t channel_samples = num_samples / 2;
  int32_t history[FIR_TAP_COUNT] = {0};
  size_t history_index = 0;
  tone_result_t result = {.channel_name = channel_name};

  for (size_t i = start_index; i < num_samples; i += 2) {
    int32_t sample = extract_sample(samples[i]);
    uint32_t magnitude = (sample < 0) ? (uint32_t) (-sample) : (uint32_t) sample;
    float filtered_sample = 0.0f;
    size_t tap_index;

    history[history_index] = sample;
    tap_index = history_index;
    for (size_t tap = 0; tap < FIR_TAP_COUNT; ++tap) {
      filtered_sample += bandpass_taps[tap] * (float) history[tap_index];
      tap_index = (tap_index == 0) ? (FIR_TAP_COUNT - 1) : (tap_index - 1);
    }
    history_index = (history_index + 1) % FIR_TAP_COUNT;

    int32_t filtered = (int32_t) lroundf(filtered_sample);
    uint32_t filtered_magnitude = (filtered < 0) ? (uint32_t) (-filtered) : (uint32_t) filtered;

    result.raw_energy += (uint64_t) magnitude * (uint64_t) magnitude;
    result.band_energy += (uint64_t) filtered_magnitude * (uint64_t) filtered_magnitude;

    if ((int32_t) magnitude > result.raw_peak_amplitude) {
      result.raw_peak_amplitude = (int32_t) magnitude;
    }
    if ((int32_t) filtered_magnitude > result.filtered_peak_amplitude) {
      result.filtered_peak_amplitude = (int32_t) filtered_magnitude;
    }
  }

  if (channel_samples == 0 || result.raw_energy == 0) {
    return result;
  }

  result.band_energy_ratio_permille = (uint32_t) ((result.band_energy * 1000u) / result.raw_energy);
  result.tone_detected =
      (result.raw_peak_amplitude >= MIN_RAW_PEAK_AMPLITUDE) &&
      (result.filtered_peak_amplitude >= MIN_FILTERED_PEAK_AMPLITUDE) &&
      (result.band_energy_ratio_permille >= MIN_BAND_ENERGY_RATIO_PERMILLE);

  return result;
}

static const tone_result_t *pick_active_channel(const tone_result_t *left, const tone_result_t *right)
{
  if (left->filtered_peak_amplitude > right->filtered_peak_amplitude) {
    return left;
  }
  if (right->filtered_peak_amplitude > left->filtered_peak_amplitude) {
    return right;
  }
  if (left->band_energy >= right->band_energy) {
    return left;
  }
  return right;
}

int main(void)
{
  stdio_init_all();

  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
  gpio_put(LED_PIN, 0);

  if (mic_i2s_init(MIC_PIO_NUM, MIC_DATA_PIN, MIC_SCK_PIN, MIC_SAMPLE_FREQ, MIC_NUM_SAMPLES) != 0) {
    while (1) {
      gpio_put(LED_PIN, 1);
      sleep_ms(100);
      gpio_put(LED_PIN, 0);
      sleep_ms(100);
    }
  }

  mic_i2s_start();
  init_bandpass_filter();
  sleep_ms(250);

  while (1) {
    uint32_t *samples = mic_i2s_get_sample_buffer(true);
    tone_result_t left = analyze_channel(samples, MIC_NUM_SAMPLES, 0, 'L');
    tone_result_t right = analyze_channel(samples, MIC_NUM_SAMPLES, 1, 'R');
    const tone_result_t *active = pick_active_channel(&left, &right);

    gpio_put(LED_PIN, left.tone_detected || right.tone_detected);
    printf("Active channel: %c | %d Hz band energy: %lu.%03lu%% | raw peak: %ld | filtered peak: %ld | match: %s\n",
           active->channel_name,
           TARGET_FREQ_HZ,
           (unsigned long) (active->band_energy_ratio_permille / 10),
           (unsigned long) (active->band_energy_ratio_permille % 10) * 100ul,
           (long) active->raw_peak_amplitude,
           (long) active->filtered_peak_amplitude,
           (left.tone_detected || right.tone_detected) ? "YES" : "NO");
  }
}
