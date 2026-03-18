#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "mic.pio.h"



#define PIN_LED 16

#define PIN_LR 13
#define PIN_WS 12
#define PIN_SCK 11
#define PIN_SD 10

#define BUFFER_LENGTH 64
// void blink_pin_forever(PIO pio, uint sm, uint offset, uint pin, uint freq) {
//     blink_program_init(pio, sm, offset, pin);
//     pio_sm_set_enabled(pio, sm, true);

//     printf("Blinking pin %d at %d Hz\n", pin, freq);

//     // PIO counter program takes 3 more cycles in total than we pass as
//     // input (wait for n + 1; mov; jmp)
//     pio->txf[sm] = (125000000 / (2 * freq)) - 3;
// }



// int main()
// {
//     stdio_init_all();

//     // PIO Blinking example
//     PIO pio = pio0;
//     _i2s_input_audio_format = i2s_input_audio_format;
//     _i2s_output_audio_format = i2s_output_audio_format;
//     uint func = GPIO_FUNC_PIOx;
//     gpio_set_function(PIN_WS, GPIO_FUNC_PIO, PICO_AUDIO_PIO);
//     gpio_set_function(PIN_SCK, GPIO_FUNC_PIO, PICO_AUDIO_PIO);
//     gpio_set_function(PIN_SD, GPIO_FUNC_PIO, PICO_AUDIO_PIO);

//     // uint8_t sm = shared_state.pio_sm = config->pio_sm;
//     // pio_sm_claim(pio0, sm);

//     loaded_offset = pio_add_program(pio0, &audio_i2s_program);
//     printf("Loaded program at %d\n", loaded_offset);
//     // uint offset = pio_add_program(pio, &blink_program);
//     // printf("Loaded program at %d\n", offset);
    
//     // #ifdef PICO_DEFAULT_LED_PIN
//     // blink_pin_forever(pio, 0, offset, PICO_DEFAULT_LED_PIN, 3);
//     // #else
//     // blink_pin_forever(pio, 0, offset, 6, 3);
//     // #endif
//     // For more pio examples see https://github.com/raspberrypi/pico-examples/tree/master/pio

//     while (true) {
//         printf("Hello, world!\n");
//         sleep_ms(1000);
//     }
// }

// #define SAMPLE_RATE 16000

// static inline int32_t mic_unpack_24(uint32_t raw) {
//     return ((int32_t)raw) >> 8;
// }
// #include "hardware/uart.h"
#include "hardware/dma.h"
#include <math.h>

#define printu(var) printf("%s: %lu\n", (#var), (size_t) (var))

#define bswap(x) \
  ((((x) & 0xff000000u) >> 24) | (((x) & 0x00ff0000u) >> 8)	\
   | (((x) & 0x0000ff00u) << 8) | (((x) & 0x000000ffu) << 24))

size_t clk;
PIO pio = pio0;
uint sm;
uint dma_chan;

// #define BLOCK_SIZE (48000)
#define BLOCK_SIZE 256

void init() {
//   stdio_uart_init_full(uart0, 921600, 0, 1);
  /* stdio_init_all(); */
  clk = clock_get_hz(clk_sys);
  dma_chan = dma_claim_unused_channel(true);
}

static void start_dma(int32_t* buf, size_t len) {
  dma_channel_config c = dma_channel_get_default_config(dma_chan);
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, true);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
  dma_channel_configure(dma_chan, &c, buf, &pio->rxf[sm], len, true);
}

static void finalize_dma() {
  dma_channel_wait_for_finish_blocking(dma_chan);
}

static void print_samples(int32_t* samples, size_t len) {
    for (size_t i = 0; i < len; i++) {
    //   int32_t val = samples[i];
    //   printf("%d\t%X\n", val, val);
        int32_t min_v = INT32_MAX;
        int32_t max_v = INT32_MIN;
        int64_t sum_abs = 0;
        int32_t peak = 0;

        for (size_t i = 0; i < BLOCK_SIZE; i++) {
            int32_t s = samples[i];
            if (s < min_v) min_v = s;
            if (s > max_v) max_v = s;

            int32_t a = (s < 0) ? -s : s;
            if (a > peak) peak = a;
            sum_abs += a;
        }

        printf("min=%ld max=%ld avg_abs=%ld peak=%ld\n",
            (long)min_v, (long)max_v,
            (long)(sum_abs / BLOCK_SIZE), (long)peak);
    }
    /* printf("("); */
    /* for (size_t i = 0; i < len; i++) { */
    /*   printf("%d, ", samples[i]); */
    /*   /1* printf("%08X, ", samples[i]); *1/ */
    /* } */
    /* printf(")\n"); */
}

static void normalize(int32_t* samples, size_t len) {
  for (int i = 0; i < 10; i++) {
    start_dma(samples, len);
    finalize_dma();
  }
}

static inline int32_t sign_extend_24(uint32_t x) {
    x &= 0x00FFFFFFu;
    if (x & 0x00800000u) {
        x |= 0xFF000000u;
    }
    return (int32_t)x;
}

static inline int32_t raw_i2s32_to_signed24(uint32_t raw32) {
    // With shift-right mode, the first received bit becomes bit31.
    // For a 24-bit mic in a 32-bit slot, the valid audio is normally
    // left-justified in the top 24 bits, so shift down by 8.
    uint32_t s24 = raw32 >> 8;
    return sign_extend_24(s24);
}

uint32_t floor(uint32_t a, uint32_t b){
    uint32_t q = a / b;
    uint32_t r = a % b;
    if(r != 0 && ((a < 0) ^ (b < 0))){
        q -= 1;
    }
    return q
}

float DB_OFFSET = -46.72;

int main() {
    stdio_init_all();
    sleep_ms(2000);
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    printf("INMP441 start\n");

    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);

    // Optional only if L/R is driven by GPIO instead of tied to GND/3V3 externally
    // For left channel:
    gpio_init(PIN_LR);
    gpio_set_dir(PIN_LR, GPIO_OUT);
    gpio_put(PIN_LR, 0);   // 0 = left slot, 1 = right slot

    // PIO pio = pio0;
    int sm_clk = 0;
    int sm_rx  = 1;

    // uint offset_clk = pio_add_program(pio, &mic_clk_program);
    // uint offset_rx  = pio_add_program(pio, &mic_rx_program);
    uint offset = pio_add_program(pio, &i2s_program);
    sm = pio_claim_unused_sm(pio, true);
    
    i2s_program_init(pio, sm, offset, PIN_SD, PIN_SCK);

    // Clock generator SM
    // pio_gpio_init(pio, PIN_SCK);
    // pio_gpio_init(pio, PIN_WS);
    // pio_sm_set_consecutive_pindirs(pio, sm_clk, PIN_SCK, 2, true);

    // pio_sm_config c_clk = mic_clk_program_get_default_config(offset_clk);
    // sm_config_set_sideset_pins(&c_clk, PIN_SCK);

    // float div = (float)clock_get_hz(clk_sys) / (130.0f * SAMPLE_RATE);
    // sm_config_set_clkdiv(&c_clk, div);

    // pio_sm_init(pio, sm_clk, offset_clk, &c_clk);

    // // Receiver SM
    // pio_gpio_init(pio, PIN_SD);
    // gpio_pull_down(PIN_SD);

    // pio_sm_config c_rx = mic_rx_program_get_default_config(offset_rx);
    // sm_config_set_in_pins(&c_rx, PIN_SD);
    // sm_config_set_in_shift(&c_rx, false, false, 32);
    // sm_config_set_fifo_join(&c_rx, PIO_FIFO_JOIN_RX);
    // sm_config_set_clkdiv(&c_rx, 1.0f);

    // pio_sm_init(pio, sm_rx, offset_rx, &c_rx);

    // pio_sm_set_enabled(pio, sm_rx, true);
    // pio_sm_set_enabled(pio, sm_clk, true);


    while (true) {
        // gpio_put(PIN_LED, 1);

        // uint32_t raw = pio_sm_get_blocking(pio, sm_rx);
        // int32_t sample = mic_unpack_24(raw);
        // printf("%ld\n", (long)sample);
        // printf("%d\n", raw);
        // printf("alive\n");
        // sleep_ms(1000);
        
        // gpio_put(PIN_LED, 0);
        int32_t samples[BLOCK_SIZE] = {0};

        uint32_t bytes_read = 0;
        uint32_t sum_squares = 0;
        for (size_t i = 0; i < BLOCK_SIZE; i++) {
            // uint32_t val = pio_sm_get_blocking(pio, sm);
            // samples[i] = *((int32_t *) &val);
            uint32_t raw = pio_sm_get_blocking(pio, sm);
            int32_t sample = raw_i2s32_to_signed24(raw);
            samples[i] = sample;
            pio_sm_get_blocking += 1;
            sum_squares += sample * sample;
        }

        if(bytes_read > 0){
            float rms = sqrt(sum_squares / samples_read);
            // Avoid log(0) error
            if rms <= 0.0:
                rms = 1.0;
            // Calculate dB
            float db = 20.0 * log10(rms);
            // Apply calibration
            float final_db = db + DB_OFFSET
            // Print to Serial
            printf("Raw dB: %.2f | Final dB: %.2f", db, final_db);
        }

        // print_samples(samples, BLOCK_SIZE);
        sleep_ms(500);
    }
}
// #include <stdint.h>
// #include <inttypes.h>
// #include <limits.h>
// #include "i2s_in_master.pio.h"


// #define SAMPLE_RATE_HZ 48000
// #define BLOCK_SAMPLES  256


// int main() {
//     stdio_init_all();
//     sleep_ms(2000);

//     printf("INMP441 I2S RX test starting...\n");
//     printf("Pins: LR=%d WS=%d SCK=%d SD=%d\n", PIN_LR, PIN_WS, PIN_SCK, PIN_SD);

//     // L/R select on the mic:
//     // 0 = left channel, 1 = right channel
//     gpio_init(PIN_LR);
//     gpio_set_dir(PIN_LR, GPIO_OUT);
//     gpio_put(PIN_LR, 0);   // select LEFT slot from microphone

//     // Sanity check for this PIO program
//     if (PIN_WS != (PIN_SCK + 1)) {
//         printf("ERROR: This PIO program requires PIN_WS = PIN_SCK + 1\n");
//         while (1) {
//             sleep_ms(1000);
//         }
//     }

//     PIO pio = pio0;
//     uint sm = 0;
//     uint offset = pio_add_program(pio, &i2s_in_master_program);

//     i2s_in_master_program_init(
//         pio,
//         sm,
//         offset,
//         PIN_SD,
//         PIN_SCK,
//         SAMPLE_RATE_HZ
//     );

//     printf("PIO running at %d Hz sample rate\n", SAMPLE_RATE_HZ);
//     printf("Speak into the mic. You should see avg_abs / peak change.\n\n");

//     while (true) {
//         int32_t min_v = INT32_MAX;
//         int32_t max_v = INT32_MIN;
//         int32_t peak  = 0;
//         int64_t sum_abs = 0;

//         for (int i = 0; i < BLOCK_SAMPLES; i++) {
//             // Read one stereo frame: left then right
//             uint32_t raw_left  = pio_sm_get_blocking(pio, sm);
//             uint32_t raw_right = pio_sm_get_blocking(pio, sm);
//             (void)raw_right; // INMP441 is strapped to LEFT slot in this example

//             int32_t sample = raw_i2s32_to_signed24(raw_left);

//             if (sample < min_v) min_v = sample;
//             if (sample > max_v) max_v = sample;

//             int32_t a = (sample < 0) ? -sample : sample;
//             if (a > peak) peak = a;

//             sum_abs += a;
//         }

//         int32_t avg_abs = (int32_t)(sum_abs / BLOCK_SAMPLES);

//         // crude text bar
//         int bars = peak >> 18;
//         if (bars > 40) bars = 40;

//         printf("min=%" PRId32 " max=%" PRId32 " avg_abs=%" PRId32 " peak=%" PRId32 " [",
//                min_v, max_v, avg_abs, peak);

//         for (int i = 0; i < bars; i++) putchar('#');
//         for (int i = bars; i < 40; i++) putchar(' ');
//         printf("]\n");
//     }
// }