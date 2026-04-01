#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "i2s_in_master.pio.h"

#define PIN_SD   2   // INMP441 SD  -> Pico GPIO10 (PIO input)
#define PIN_SCK  3   // INMP441 SCK -> Pico GPIO11 (PIO side-set pin 0)
#define PIN_WS   4   // INMP441 WS  -> Pico GPIO12 (PIO side-set pin 1)
#define PIN_LR   5   // INMP441 L/R select pin

#define SAMPLE_RATE_HZ      48000u
#define BCLKS_PER_HALF_FRAME    32u      // 32 BCLKs per half-frame for INMP441
#define CHANNELS_PER_FRAME  2u       // I2S always clocks stereo frames
#define HALF_BUFFER_SAMPLES 256u     // Mono samples stored per ping/pong buffer
#define STARTUP_SETTLE_MS   120u     // Allow mic to settle after clocks start
#define PRINT_EVERY_N_BLOCKS 8u      // Reduce terminal spam
#define FULL_SCALE_S24      8388607.0
#define FILTER_CENTER_HZ    500.0f
#define FILTER_Q            12.0f
#define DETECT_THRESHOLD_DBFS (-42.0f)

static int32_t ping_buffer[HALF_BUFFER_SAMPLES];
static int32_t pong_buffer[HALF_BUFFER_SAMPLES];
static float filtered_buffer[HALF_BUFFER_SAMPLES];

typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float x1;
    float x2;
    float y1;
    float y2;
} biquad_filter_t;

static biquad_filter_t tone_filter;
static biquad_filter_t tone_filter2;

static void apply_biquad_block_ff(biquad_filter_t *filter, float *buf, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const float x0 = buf[i];
        const float y0 = filter->b0 * x0
                       + filter->b1 * filter->x1
                       + filter->b2 * filter->x2
                       - filter->a1 * filter->y1
                       - filter->a2 * filter->y2;
        filter->x2 = filter->x1;
        filter->x1 = x0;
        filter->y2 = filter->y1;
        filter->y1 = y0;
        buf[i] = y0;
    }
}

static inline int32_t inmp441_raw_to_s24(uint32_t raw_word) {
    // PIO shifts in 32 bits per half-frame; valid 24-bit sample is in bits [31:8].
    return ((int32_t)raw_word) >> 8;
}

static void wait_for_usb_serial(void) {
    absolute_time_t deadline = make_timeout_time_ms(3000);
    while (!stdio_usb_connected() && absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        sleep_ms(10);
    }
}

static void print_pin_summary(void) {
    printf("INMP441 wiring:\n");
    printf("  VDD -> 3V3\n");
    printf("  GND -> GND\n");
    printf("  SD  -> GPIO%d\n", PIN_SD);
    printf("  SCK -> GPIO%d\n", PIN_SCK);
    printf("  WS  -> GPIO%d\n", PIN_WS);
    printf("  L/R -> GPIO%d\n", PIN_LR);
}

static void configure_mic_channel(bool left_channel) {
    gpio_init(PIN_LR);
    gpio_set_dir(PIN_LR, GPIO_OUT);

    // INMP441 datasheet: L/R = 0 -> left channel, L/R = 1 -> right channel.
    gpio_put(PIN_LR, left_channel ? 0 : 1);
}

static void fill_mono_buffer_from_pio(PIO pio, uint sm, int32_t *buffer, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint32_t left = pio_sm_get_blocking(pio, sm);  // left slot — mic data
        (void)         pio_sm_get_blocking(pio, sm);   // right slot — discard
        buffer[i] = inmp441_raw_to_s24(left);
    }
}

static float safe_dbfs_from_amplitude(double amplitude) {
    if (amplitude < 1.0) {
        amplitude = 1.0;
    }
    return 20.0f * (float)log10(amplitude / FULL_SCALE_S24);
}

static biquad_filter_t make_bandpass_filter(float center_hz, float q_factor, float sample_rate_hz) {
    const float omega = 2.0f * (float)M_PI * center_hz / sample_rate_hz;
    const float alpha = sinf(omega) / (2.0f * q_factor);
    const float cos_omega = cosf(omega);
    const float a0 = 1.0f + alpha;

    biquad_filter_t filter = {
        .b0 = alpha / a0,
        .b1 = 0.0f,
        .b2 = -alpha / a0,
        .a1 = -2.0f * cos_omega / a0,
        .a2 = (1.0f - alpha) / a0,
        .x1 = 0.0f,
        .x2 = 0.0f,
        .y1 = 0.0f,
        .y2 = 0.0f,
    };

    return filter;
}

static void apply_biquad_block(biquad_filter_t *filter, const int32_t *input, float *output, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const float x0 = (float)input[i];
        const float y0 = filter->b0 * x0
                       + filter->b1 * filter->x1
                       + filter->b2 * filter->x2
                       - filter->a1 * filter->y1
                       - filter->a2 * filter->y2;

        filter->x2 = filter->x1;
        filter->x1 = x0;
        filter->y2 = filter->y1;
        filter->y1 = y0;
        output[i] = y0;
    }
}

static float rms_from_float_block(const float *buffer, size_t count) {
    double sum_squares = 0.0;

    for (size_t i = 0; i < count; ++i) {
        sum_squares += (double)buffer[i] * (double)buffer[i];
    }

    return (float)sqrt(sum_squares / (double)count);
}

static float peak_abs_from_float_block(const float *buffer, size_t count) {
    float peak = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        float magnitude = fabsf(buffer[i]);
        if (magnitude > peak) {
            peak = magnitude;
        }
    }

    return peak;
}

static void analyse_and_print_block(const int32_t *buffer, const float *filtered, size_t count, uint32_t block_index) {
    int32_t min_value = INT32_MAX;
    int32_t max_value = INT32_MIN;
    int64_t sum_abs = 0;
    double sum_squares = 0.0;

    for (size_t i = 0; i < count; ++i) {
        int32_t s = buffer[i];
        if (s < min_value) min_value = s;
        if (s > max_value) max_value = s;
        sum_abs += (s < 0) ? -(int64_t)s : (int64_t)s;
        sum_squares += (double)s * (double)s;
    }

    int32_t peak = (max_value > -min_value) ? max_value : -min_value;
    int32_t avg_abs = (int32_t)(sum_abs / (int64_t)count);
    double rms = sqrt(sum_squares / (double)count);
    float peak_dbfs = safe_dbfs_from_amplitude((double)peak);
    float rms_dbfs = safe_dbfs_from_amplitude(rms);
    float filtered_rms = rms_from_float_block(filtered, count);
    float filtered_peak = peak_abs_from_float_block(filtered, count);
    float filtered_rms_dbfs = safe_dbfs_from_amplitude((double)filtered_rms);
    float filtered_peak_dbfs = safe_dbfs_from_amplitude((double)filtered_peak);
    float tone_margin_db = filtered_rms_dbfs - rms_dbfs;
    // bool tone_detected = filtered_rms_dbfs > DETECT_THRESHOLD_DBFS && tone_margin_db > -6.0f;
    bool tone_detected = filtered_rms_dbfs > DETECT_THRESHOLD_DBFS && (rms_dbfs - filtered_rms_dbfs) < 6.0f;

    uint bar_len = 0;
    if (filtered_rms_dbfs > -60.0f) {
        bar_len = (uint)(((filtered_rms_dbfs + 60.0f) / 60.0f) * 40.0f);
        if (bar_len > 40u) {
            bar_len = 40u;
        }
    }

    printf("block %lu  raw_rms=%.1f dBFS  500Hz_rms=%.1f dBFS  500Hz_peak=%.1f dBFS  [%s] [",
           (unsigned long)block_index, rms_dbfs, filtered_rms_dbfs, filtered_peak_dbfs, tone_detected ? "TONE" : "----");

    for (uint i = 0; i < bar_len; ++i) {
        putchar('#');
    }
    for (uint i = bar_len; i < 40u; ++i) {
        putchar(' ');
    }
    printf("]\n");

    printf("  min=%ld  max=%ld  avg|x|=%ld  raw_peak=%.1f dBFS  band_delta=%.1f dB\n",
           (long)min_value, (long)max_value, (long)avg_abs, peak_dbfs, tone_margin_db);
}

int main(void) {
    stdio_init_all();
    wait_for_usb_serial();
    sleep_ms(250);

    print_pin_summary();
    configure_mic_channel(true);   // mono: microphone drives the left slot only

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &i2s_in_master_program);
    tone_filter = make_bandpass_filter(FILTER_CENTER_HZ, FILTER_Q, (float)SAMPLE_RATE_HZ);
    tone_filter2 = make_bandpass_filter(FILTER_CENTER_HZ, FILTER_Q, (float)SAMPLE_RATE_HZ);

    // Each stereo frame requires 64 BCLKs, and the PIO uses 2 instructions per BCLK.
    // Therefore the state machine must run at SAMPLE_RATE * 64 * 2 instructions per second.
    const float sm_freq = (float)(SAMPLE_RATE_HZ * BCLKS_PER_HALF_FRAME * CHANNELS_PER_FRAME * 2u);
    const float clkdiv = (float)clock_get_hz(clk_sys) / sm_freq;

    // i2s_in_master_program_init(pio, sm, offset, SAMPLE_RATE_HZ, PIN_SD, PIN_SCK);
    i2s_in_master_program_init(pio, sm, offset, clkdiv, PIN_SD, PIN_SCK);
    pio_sm_set_enabled(pio, sm, true);

    printf("\nPIO I2S input started\n");
    printf("  clk_sys     = %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));
    printf("  sample rate = %u Hz\n", SAMPLE_RATE_HZ);
    printf("  sm clkdiv   = %.6f\n", clkdiv);
    printf("  band-pass   = %.1f Hz (Q=%.1f)\n", FILTER_CENTER_HZ, FILTER_Q);
    printf("  printing    = every %u blocks\n", PRINT_EVERY_N_BLOCKS);
    printf("  Present a steady 500 Hz tone to the microphone.\n\n");

    sleep_ms(STARTUP_SETTLE_MS);

    // Flush a few initial words because the mic outputs zeros right after startup.
    for (uint i = 0; i < 16; ++i) {          // 16 pairs = 32 words, stays in sync
      (void)pio_sm_get_blocking(pio, sm);   // left
      (void)pio_sm_get_blocking(pio, sm);   // right
    }

    uint32_t block_index = 0;
    while (true) {
        int32_t *active = (block_index & 1u) ? pong_buffer : ping_buffer;
        memset(active, 0, HALF_BUFFER_SAMPLES * sizeof(active[0]));

        fill_mono_buffer_from_pio(pio, sm, active, HALF_BUFFER_SAMPLES);
        // apply_biquad_block(&tone_filter, active, filtered_buffer, HALF_BUFFER_SAMPLES);
        apply_biquad_block(&tone_filter, active, filtered_buffer, HALF_BUFFER_SAMPLES);
        apply_biquad_block_ff(&tone_filter2, filtered_buffer, HALF_BUFFER_SAMPLES);

        if ((block_index % PRINT_EVERY_N_BLOCKS) == 0u) {
            analyse_and_print_block(active, filtered_buffer, HALF_BUFFER_SAMPLES, block_index);
        }

        ++block_index;
    }

    return 0;
}
