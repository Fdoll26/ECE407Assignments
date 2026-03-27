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

// tone reporting range:
#define REPORT_MIN_FREQ_HZ  100
#define REPORT_MAX_FREQ_HZ  2000

// 500 Hz detection settings:
#define TARGET_FREQ_HZ      500
#define TARGET_TOLERANCE_HZ 40
#define TONE_RATIO_MIN      12
#define MIN_PEAK_AMPLITUDE  6000

typedef struct {
  char channel_name;
  uint32_t frequency_hz;
  uint64_t strongest_power;
  uint64_t total_power;
  int32_t peak_amplitude;
  bool tone_detected;
} tone_result_t;

static int32_t extract_sample(uint32_t raw_word)
{
  return ((int32_t) raw_word) >> 8;
}

static uint64_t goertzel_power_channel(const uint32_t *samples, size_t num_samples, size_t start_index, size_t bin_index)
{
  size_t channel_samples = num_samples / 2;
  float normalized = (2.0f * (float) M_PI * (float) bin_index) / (float) channel_samples;
  float coeff = 2.0f * cosf(normalized);
  float s_prev = 0.0f;
  float s_prev2 = 0.0f;

  for (size_t i = start_index; i < num_samples; i += 2) {
    float sample = (float) extract_sample(samples[i]);
    float s = sample + (coeff * s_prev) - s_prev2;
    s_prev2 = s_prev;
    s_prev = s;
  }

  float power = (s_prev * s_prev) + (s_prev2 * s_prev2) - (coeff * s_prev * s_prev2);
  return (power > 0.0f) ? (uint64_t) power : 0;
}

static tone_result_t analyze_channel(const uint32_t *samples, size_t num_samples, size_t start_index, char channel_name)
{
  size_t channel_samples = num_samples / 2;
  size_t report_first_bin = (REPORT_MIN_FREQ_HZ * channel_samples) / MIC_SAMPLE_FREQ;
  size_t report_last_bin = (REPORT_MAX_FREQ_HZ * channel_samples) / MIC_SAMPLE_FREQ;
  size_t target_first_bin = ((TARGET_FREQ_HZ - TARGET_TOLERANCE_HZ) * channel_samples) / MIC_SAMPLE_FREQ;
  size_t target_last_bin = ((TARGET_FREQ_HZ + TARGET_TOLERANCE_HZ) * channel_samples) / MIC_SAMPLE_FREQ;

  tone_result_t result = {
    .channel_name = channel_name
  };
  uint64_t best_target_power = 0;
  size_t best_bin = 0;

  for (size_t i = start_index; i < num_samples; i += 2) {
    int32_t sample = extract_sample(samples[i]);
    int32_t magnitude = (sample < 0) ? -sample : sample;
    result.total_power += (uint64_t) magnitude * (uint64_t) magnitude;
    if (magnitude > result.peak_amplitude) {
      result.peak_amplitude = magnitude;
    }
  }

  if (channel_samples == 0 || result.total_power == 0) {
    return result;
  }

  for (size_t bin = report_first_bin; bin <= report_last_bin; ++bin) {
    uint64_t power = goertzel_power_channel(samples, num_samples, start_index, bin);
    if (power > result.strongest_power) {
      result.strongest_power = power;
      best_bin = bin;
    }
    if (bin >= target_first_bin && bin <= target_last_bin && power > best_target_power) {
      best_target_power = power;
    }
  }

  result.frequency_hz = (uint32_t) ((best_bin * MIC_SAMPLE_FREQ) / channel_samples);
  result.tone_detected =
      (result.peak_amplitude >= MIN_PEAK_AMPLITUDE) &&
      (best_target_power > (uint64_t) TONE_RATIO_MIN * result.total_power);

  return result;
}

static const tone_result_t *pick_active_channel(const tone_result_t *left, const tone_result_t *right)
{
  if (left->peak_amplitude > right->peak_amplitude) {
    return left;
  }
  if (right->peak_amplitude > left->peak_amplitude) {
    return right;
  }
  if (left->total_power >= right->total_power) {
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
  sleep_ms(250);

  while (1) {
    uint32_t *samples = mic_i2s_get_sample_buffer(true);
    tone_result_t left = analyze_channel(samples, MIC_NUM_SAMPLES, 0, 'L');
    tone_result_t right = analyze_channel(samples, MIC_NUM_SAMPLES, 1, 'R');
    const tone_result_t *active = pick_active_channel(&left, &right);

    gpio_put(LED_PIN, left.tone_detected || right.tone_detected);
    printf("Active channel: %c | strongest tone: %lu Hz | peak: %ld | 500 Hz match: %s\n",
           active->channel_name,
           (unsigned long) active->frequency_hz,
           (long) active->peak_amplitude,
           (left.tone_detected || right.tone_detected) ? "YES" : "NO");
  }
}
