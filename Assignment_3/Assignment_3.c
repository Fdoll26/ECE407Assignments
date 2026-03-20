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

#define PIN_SD   10   // INMP441 SD  -> Pico GPIO10 (PIO input)
#define PIN_SCK  11   // INMP441 SCK -> Pico GPIO11 (PIO side-set pin 0)
#define PIN_WS   12   // INMP441 WS  -> Pico GPIO12 (PIO side-set pin 1)
#define PIN_LR   13   // INMP441 L/R select pin

#define SAMPLE_RATE_HZ      48000u
#define BITS_PER_CHANNEL    32u      // 32 BCLKs per half-frame for INMP441
#define CHANNELS_PER_FRAME  2u       // I2S always clocks stereo frames
#define HALF_BUFFER_SAMPLES 256u     // Mono samples stored per ping/pong buffer
#define STARTUP_SETTLE_MS   120u     // Allow mic to settle after clocks start
#define PRINT_EVERY_N_BLOCKS 8u      // Reduce terminal spam
#define FULL_SCALE_S24      8388607.0
#define DB_OFFSET           97.0f

static int32_t ping_buffer[HALF_BUFFER_SAMPLES];
static int32_t pong_buffer[HALF_BUFFER_SAMPLES];

static inline int32_t inmp441_raw_to_s24(uint32_t raw_word) {
    // PIO shifts in 32 bits per half-frame; valid 24-bit sample is in bits [31:8].
    return ((int32_t)raw_word) >> 8;
}

static void wait_for_usb_serial(void) {
    absolute_time_t deadline = make_timeout_time_ms(3000);
    while (!stdio_usb_connected() && absolute_time_diff_us(get_absolute_time(), deadline) < 0) {
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

static void fill_mono_buffer_from_pio(PIO pio, uint sm, int32_t *buffer, size_t count, bool capture_left_channel) {
    const unsigned wanted_ws = capture_left_channel ? 0u : 1u;
    size_t collected = 0;

    while (collected < count) {
        uint32_t raw_word = pio_sm_get_blocking(pio, sm);
        unsigned ws_level = gpio_get(PIN_WS);

        // When the FIFO receives a word, the state machine has just finished one half-frame.
        // GPIO12 still reflects the WS level for that half-frame, so we can use it to select
        // the requested channel and ignore the other slot.
        if (ws_level == wanted_ws) {
            buffer[collected++] = inmp441_raw_to_s24(raw_word);
        }
    }
}

static float safe_dbfs_from_amplitude(double amplitude) {
    if (amplitude < 1.0) {
        amplitude = 1.0;
    }
    return 20.0f * (float)log10(amplitude / FULL_SCALE_S24);
}

static void analyse_and_print_block(const int32_t *buffer, size_t count, uint32_t block_index) {
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

    uint bar_len = 0;
    if (rms_dbfs > -60.0f) {
        bar_len = (uint)(((rms_dbfs + 60.0f) / 60.0f) * 40.0f);
        if (bar_len > 40u) {
            bar_len = 40u;
        }
    }

    printf("block %lu  peak=%ld  rms=%.0f  peak=%.1f dBFS  rms=%.1f dBFS  [",
           (unsigned long)block_index,
           (long)peak,
           rms,
           peak_dbfs,
           rms_dbfs);

    for (uint i = 0; i < bar_len; ++i) {
        putchar('#');
    }
    for (uint i = bar_len; i < 40u; ++i) {
        putchar(' ');
    }
    printf("]\n");

    printf("  min=%ld  max=%ld  avg|x|=%ld\n",
           (long)min_value,
           (long)max_value,
           (long)avg_abs);
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

    // Each stereo frame requires 64 BCLKs, and the PIO uses 2 instructions per BCLK.
    // Therefore the state machine must run at SAMPLE_RATE * 64 * 2 instructions per second.
    const float sm_freq = (float)(SAMPLE_RATE_HZ * BITS_PER_CHANNEL * CHANNELS_PER_FRAME * 2u);
    const float clkdiv = (float)clock_get_hz(clk_sys) / sm_freq;

    i2s_in_master_program_init(pio, sm, offset, PIN_SD, PIN_SCK, clkdiv);

    printf("\nPIO I2S input started\n");
    printf("  clk_sys     = %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));
    printf("  sample rate = %u Hz\n", SAMPLE_RATE_HZ);
    printf("  sm clkdiv   = %.6f\n", clkdiv);
    printf("  printing    = every %u blocks\n", PRINT_EVERY_N_BLOCKS);
    printf("  Speak or whistle into the microphone.\n\n");

    sleep_ms(STARTUP_SETTLE_MS);

    // Flush a few initial words because the mic outputs zeros right after startup.
    for (uint i = 0; i < 32; ++i) {
        (void)pio_sm_get_blocking(pio, sm);
    }

    uint32_t block_index = 0;
    while (true) {
        int32_t *active = (block_index & 1u) ? pong_buffer : ping_buffer;
        memset(active, 0, HALF_BUFFER_SAMPLES * sizeof(active[0]));

        fill_mono_buffer_from_pio(pio, sm, active, HALF_BUFFER_SAMPLES, true);

        if ((block_index % PRINT_EVERY_N_BLOCKS) == 0u) {
            analyse_and_print_block(active, HALF_BUFFER_SAMPLES, block_index);
        }

        ++block_index;
    }

    return 0;
}