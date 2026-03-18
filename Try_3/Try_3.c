#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "i2s_mic_rx.pio.h"

// ---------------- CONFIGURATION ----------------
#define SD_PIN              10
#define SCK_PIN             11
#define WS_PIN              12

#define SAMPLE_RATE         16000
#define BITS_PER_SAMPLE     32
#define BUFFER_LENGTH       64
#define BUFFER_SAMPLE       2048

// Calibration offset from your Python code
#define DB_OFFSET           (-46.72f)

// ---------------- HELPER: sign-extend 24-bit packed in upper bits ----------------
// After shifting right by 8, we want a signed 24-bit value in a 32-bit int.
static inline int32_t sample32_to_24(int32_t raw)
{
    // Your Python did: processed_sample = mic_samples[i] >> 8
    // This keeps sign on RP2040 GCC as arithmetic right shift for signed ints.
    return raw >> 8;
}

static void i2s_mic_rx_program_init(PIO pio, uint sm, uint offset, uint sd_pin, uint sck_pin, uint ws_pin, float sample_rate_hz)
{
    pio_sm_config c = i2s_mic_rx_program_get_default_config(offset);

    // Data input pin
    sm_config_set_in_pins(&c, sd_pin);

    // sideset pin = BCLK
    sm_config_set_sideset_pins(&c, sck_pin);

    // set pin base = WS
    sm_config_set_set_pins(&c, ws_pin, 1);

    // Shift left: first sampled bit moves toward MSB, giving a 32-bit word
    sm_config_set_in_shift(&c, false, false, 32);

    // Join FIFO RX for deeper receive buffering
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    // Pin directions
    pio_gpio_init(pio, sd_pin);
    pio_gpio_init(pio, sck_pin);
    pio_gpio_init(pio, ws_pin);

    pio_sm_set_consecutive_pindirs(pio, sm, sck_pin, 1, true); // BCLK output
    pio_sm_set_consecutive_pindirs(pio, sm, ws_pin,  1, true); // WS output
    pio_sm_set_consecutive_pindirs(pio, sm, sd_pin,  1, false); // SD input

    // Estimate state machine frequency.
    //
    // In this simple program, each bit consumes 3 instructions:
    //   nop / in / jmp
    // so each 32-bit slot is ~96 cycles.
    // One pushed slot + one discarded slot => ~192 cycles per audio sample.
    //
    // Therefore:
    //   sm_clk = sample_rate * 192
    //
    // This is a practical approximation for this compact program.
    // If your audio pitch/timing is off, tune this constant slightly.
    const float cycles_per_sample = 192.0f;
    float sm_clk = sample_rate_hz * cycles_per_sample;
    float div = (float)clock_get_hz(clk_sys) / sm_clk;

    sm_config_set_clkdiv(&c, div);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(1500);

    printf("Starting Decibel Meter...\n");

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &i2s_mic_rx_program);

    i2s_mic_rx_program_init(pio, sm, offset, SD_PIN, SCK_PIN, WS_PIN, SAMPLE_RATE);

    int32_t sample_buffer[BUFFER_SAMPLE];

    while (true)
    {
        // Read BUFFER_LENGTH samples from the RX FIFO
        for (int i = 0; i < BUFFER_SAMPLE; i++)
        { 
            // This is likely wrong compared to the python version
            // Python converts the bytes into 32 byte boundaries while this just adds 0's to it
            // Conversion out of this idk lol
            sample_buffer[i] = (int32_t)pio_sm_get_blocking(pio, sm);
        }

        double sum_squares = 0.0;

        for (int i = 0; i < BUFFER_LENGTH; i++)
        {
            int32_t processed_sample = sample32_to_24(sample_buffer[i]);
            sum_squares += (double)processed_sample * (double)processed_sample;
        }

        double rms = sqrt(sum_squares / BUFFER_LENGTH);

        if (rms <= 0.0)
        {
            rms = 1.0;
        }

        double db = 20.0 * log10(rms);
        double final_db = db + DB_OFFSET;

        printf("Raw dB: %.2f | Final dB: %.2f\n", db, final_db);

        sleep_ms(50);
    }

    return 0;
}