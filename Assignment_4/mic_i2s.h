#ifndef MIC_I2S_H
#define MIC_I2S_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "pico/types.h"

int mic_i2s_init(uint pio_num, uint data_pin, uint sck_pin, uint sample_freq, size_t num_samples);
void mic_i2s_start(void);
void mic_i2s_stop(void);
uint32_t *mic_i2s_get_sample_buffer(bool block);
uint32_t *mic_i2s_record_blocking(void);
void mic_i2s_record_buffer_blocking(uint32_t *buffer, size_t num_samples);

#endif
