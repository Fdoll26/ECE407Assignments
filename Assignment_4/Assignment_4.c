#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "i2s_in_master.pio.h"

#define PIN_SD   2
#define PIN_SCK  3 
#define PIN_WS   4 
#define PIN_LR   5

#define SAMPLE_RATE_HZ      48000u
#define BCLKS_PER_HALF_FRAME    32u 
#define CHANNELS_PER_FRAME  2u
#define HALF_BUFFER_SAMPLES 256u
#define STARTUP_SETTLE_MS   120u
#define PRINT_EVERY_N_BLOCKS 8u 
#define FULL_SCALE_S24      8388607.0
#define FILTER_CENTER_HZ    500.0f
#define FILTER_Q            12.0f
#define FILTER_HALF_BANDWIDTH_HZ 30.0f
// #define DETECT_THRESHOLD_DBFS (-42.0f)
#define DETECT_THRESHOLD_DBFS (-55.0f)
// #define FIR_LENGTH 65u
#define FIR_LENGTH 129u

static int32_t ping_buffer[HALF_BUFFER_SAMPLES];
static int32_t pong_buffer[HALF_BUFFER_SAMPLES];
// static float filtered_buffer[HALF_BUFFER_SAMPLES];
static double filtered_buffer[HALF_BUFFER_SAMPLES];

typedef struct {
    int samplecount;
    int length;
    double *fircoeff;
    int32_t *history;
} fir_filter_t;

static double sinc(const double x)
{
    if (x == 0)
        return 1;
    return sin(M_PI * x) / (M_PI * x);
}

static fir_filter_t make_fir_filter(int length, double middle, double offset, bool win_on){
    double *rep_fircoeff = (double*) malloc(length * sizeof(double));
    int32_t *rep_history = (int32_t*)calloc((size_t)(length - 1), sizeof(int32_t));
    double f1 = (middle - offset) / (double)SAMPLE_RATE_HZ;
    double f2 = (middle + offset) / (double)SAMPLE_RATE_HZ;


    for(int i = 0; i < length; i++) {
        // int temp = i - int(length/2);
        int temp = i - length/2;
        // rep_fircoeff[i] = 2.0*f1*sinc(2.0*f1*temp) - 2.0*f2*sinc(2.0*f2*temp);
        rep_fircoeff[i] = 2.0*f2*sinc(2.0*f2*temp) - 2.0*f1*sinc(2.0*f1*temp);
        if(win_on){
            double alpha = 0.54;
            double beta = 0.46;
            double hamming = alpha - beta * cos(2.0 * M_PI * i / (length - 1));
            double hanning = sin(((double) M_PI * i) / (length - 1)) * sin(((double) M_PI * i) / (length - 1));
            // double triangle = 1 - fabs((i - (((double)(length-1)) / 2.0)) / (((double)length) / 2.0));
            double alpha0 = 0.42;
            double alpha1 = 0.5;
            double alpha2 = 0.08;
            double blackman = alpha0 - alpha1 * cos(2.0 * M_PI * i / (length - 1)) - alpha2 * cos(4.0 * M_PI * i / (length - 1));
            // rep_fircoeff[i] = rep_fircoeff[i] * hanning;
            rep_fircoeff[i] = rep_fircoeff[i] * blackman;
        }
    }

    fir_filter_t new_filter = {
        .samplecount = HALF_BUFFER_SAMPLES,
        .length = length,
        .fircoeff = rep_fircoeff,
        .history = rep_history,
    };

    return new_filter;
}

static void apply_fir_filter(fir_filter_t *filter, const int32_t *input, double *output) {
    for (int idx = 0; idx < filter->samplecount; ++idx) {
        double acc = 0.0;
        for (int i = 0; i < filter->length; ++i) {
            int sample_idx = idx - i;
            if (sample_idx >= 0) {
                acc += (double)input[sample_idx] * filter->fircoeff[i];
            } else {
                int history_idx = filter->length - 2 + sample_idx;
                acc += (double)filter->history[history_idx] * filter->fircoeff[i];
            }
        }
        output[idx] = acc;
    }

    for (int i = 0; i < filter->length - 1; ++i) {
        filter->history[i] = input[filter->samplecount - (filter->length - 1) + i];
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
        (void) pio_sm_get_blocking(pio, sm);   // right slot — discard
        buffer[i] = inmp441_raw_to_s24(left);
    }
}

static float safe_dbfs_from_amplitude(double amplitude) {
    if (amplitude < 1.0) {
        amplitude = 1.0;
    }
    return 20.0f * (float)log10(amplitude / FULL_SCALE_S24);
}

static double rms_from_float_block(const double *buffer, size_t count) {
    double sum_squares = 0.0;

    for (size_t i = 0; i < count; ++i) {
        sum_squares += buffer[i] * buffer[i];
    }

    return sqrt(sum_squares / (double)count);
}

static double peak_abs_from_float_block(const double *buffer, size_t count) {
    double peak = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        // double magnitude = fabsf(buffer[i]);
        double magnitude = fabs(buffer[i]);
        if (magnitude > peak) {
            peak = magnitude;
        }
    }

    return peak;
}

static void analyse_and_print_block(const int32_t *buffer, const double *filtered, size_t count, uint32_t block_index) {
    int32_t min_value = INT32_MAX;
    int32_t max_value = INT32_MIN;
    int64_t sum_abs = 0;
    int64_t sum_samples = 0;
    double sum_squares = 0.0;

    for (size_t i = 0; i < count; ++i) {
        int32_t s = buffer[i];
        if (s < min_value) min_value = s;
        if (s > max_value) max_value = s;
        sum_abs += (s < 0) ? -(int64_t)s : (int64_t)s;
        sum_samples += s;
    }

    const double mean = (double)sum_samples / (double)count;
    double dc_removed_peak = 0.0;
    sum_abs = 0;

    for (size_t i = 0; i < count; ++i) {
        double centered = (double)buffer[i] - mean;
        double magnitude = fabs(centered);
        sum_abs += (int64_t)magnitude;
        sum_squares += centered * centered;
        if (magnitude > dc_removed_peak) {
            dc_removed_peak = magnitude;
        }
    }

    int32_t avg_abs = (int32_t)(sum_abs / (int64_t)count);
    double rms = sqrt(sum_squares / (double)count);
    double peak_dbfs = safe_dbfs_from_amplitude(dc_removed_peak);
    double rms_dbfs = safe_dbfs_from_amplitude(rms);
    double filtered_rms = rms_from_float_block(filtered, count);
    double filtered_peak = peak_abs_from_float_block(filtered, count);
    double filtered_rms_dbfs = safe_dbfs_from_amplitude(filtered_rms);
    double filtered_peak_dbfs = safe_dbfs_from_amplitude(filtered_peak);
    double tone_margin_db = filtered_rms_dbfs - rms_dbfs;
    // bool tone_detected = filtered_rms_dbfs > DETECT_THRESHOLD_DBFS && tone_margin_db > -6.0f;
    bool tone_detected = filtered_rms_dbfs > DETECT_THRESHOLD_DBFS && (rms_dbfs - filtered_rms_dbfs) < 10.0f;

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
    configure_mic_channel(true);

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &i2s_in_master_program);
    fir_filter_t firfilter = make_fir_filter(FIR_LENGTH, FILTER_CENTER_HZ, FILTER_HALF_BANDWIDTH_HZ, true);
    const float sm_freq = (float)(SAMPLE_RATE_HZ * BCLKS_PER_HALF_FRAME * CHANNELS_PER_FRAME * 2u);
    const float clkdiv = (float)clock_get_hz(clk_sys) / sm_freq;

    // i2s_in_master_program_init(pio, sm, offset, SAMPLE_RATE_HZ, PIN_SD, PIN_SCK);
    i2s_in_master_program_init(pio, sm, offset, clkdiv, PIN_SD, PIN_SCK);
    pio_sm_set_enabled(pio, sm, true);

    printf("\nPIO I2S input started\n");
    printf("  clk_sys     = %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));
    printf("  sample rate = %u Hz\n", SAMPLE_RATE_HZ);
    printf("  sm clkdiv   = %.6f\n", clkdiv);
    // printf("  band-pass   = %.1f Hz (Q=%.1f)\n", FILTER_CENTER_HZ, FILTER_Q);
    printf("  band-pass   = %.1f +/- %.1f Hz  FIR taps=%d\n", FILTER_CENTER_HZ, FILTER_HALF_BANDWIDTH_HZ, FIR_LENGTH);
    printf("  printing    = every %u blocks\n", PRINT_EVERY_N_BLOCKS);
    printf("  Present a steady 500 Hz tone to the microphone.\n\n");

    sleep_ms(STARTUP_SETTLE_MS);

    // Flush a few initial words because the mic outputs zeros right after startup.
    for (uint i = 0; i < 16; ++i) { 
      (void)pio_sm_get_blocking(pio, sm); 
      (void)pio_sm_get_blocking(pio, sm);
    }

    uint32_t block_index = 0;
    while (true) {
        int32_t *active = (block_index & 1u) ? pong_buffer : ping_buffer;
        memset(active, 0, HALF_BUFFER_SAMPLES * sizeof(active[0]));

        fill_mono_buffer_from_pio(pio, sm, active, HALF_BUFFER_SAMPLES);
        apply_fir_filter(&firfilter, active, filtered_buffer);

        if ((block_index % PRINT_EVERY_N_BLOCKS) == 0u) {
            analyse_and_print_block(active, filtered_buffer, HALF_BUFFER_SAMPLES, block_index);
        }

        ++block_index;
    }
    
    return 0;
}
