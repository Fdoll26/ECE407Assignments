// Wokwi Custom Chip - For docs and examples see:
// https://docs.wokwi.com/chips-api/getting-started
//
// SPDX-License-Identifier: MIT
// Copyright 2025 Steven Knudsen

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/*
 * ADC scaling
 *
 * Vref is 5 V, which we make the full-scale 14-bit value of 0x3FFF
 */
const float ADC_full_scale = ((float) 0x3FFF) / 5.0;
#define BUF_LEN 0x08 // byte; a 16-bit ADC result is expected

typedef struct {
  pin_t    a_in_pin;
  pin_t    cs_pin;
  uint32_t spi;
  uint8_t  spi_buffer[BUF_LEN];
  uint32_t curr_state;
} chip_state_t;

static void chip_pin_change(void *user_data, pin_t pin, uint32_t value);
static void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count);

void chip_init() {
  chip_state_t *chip = malloc(sizeof(chip_state_t));

  chip->a_in_pin = pin_init("A_IN", ANALOG);

  chip->cs_pin = pin_init("nCS", INPUT_PULLUP);

  chip->curr_state = 0xDEADBEEF;

  const pin_watch_config_t watch_config = {
    .edge = BOTH,
    .pin_change = chip_pin_change,
    .user_data = chip,
  };
  pin_watch(chip->cs_pin, &watch_config);

  const spi_config_t spi_config = {
    .sck = pin_init("SCK", INPUT),
    .miso = pin_init("MISO", INPUT),
    .mosi = pin_init("MOSI", INPUT),
    .done = chip_spi_done,
    .user_data = chip,
  };
  chip->spi = spi_init(&spi_config);
  
  printf("SPI initialized!\n");
}

void chip_pin_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t*)user_data;
  // Handle nCS pin logic
  if (pin == chip->cs_pin) {
    if (value == LOW) {
      spi_start(chip->spi, chip->spi_buffer, sizeof(chip->spi_buffer));
    } else {
      spi_stop(chip->spi);
    }
  }
}
// Part of a random number generator function
static uint32_t lcg_next(uint32_t *s) {
  *s = (*s) * 1664525u + 1013904223u;
  return *s;
}
// Part of a random number generator function, this takes some range and plugs in some numbers that are really large to get some "random" values
static int16_t rand_range(uint32_t *s, int16_t range) {
  return (int16_t)((int32_t)(lcg_next(s) % (uint32_t)(2*range+1)) - range);
}


void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  chip_state_t *chip = (chip_state_t*)user_data;
  if (!count) return;

  // Temperature — big-endian (high byte first, matches MPU-6050 convention)
  float normalized = ADC_full_scale*pin_adc_read(chip->a_in_pin);  // 0.0 to 1.0
  // uint16_t itemp = (uint16_t)(normalized * 0x3FFF);
  uint16_t itemp = (uint16_t)(normalized);
  chip->spi_buffer[0] = (itemp >> 8) & 0x00FF; // high byte first
  chip->spi_buffer[1] =  itemp       & 0x00FF;

  // Accel X — big-endian
  int16_t x = rand_range(&chip->curr_state, 16384);
  chip->spi_buffer[2] = (x >> 8) & 0xFF;
  chip->spi_buffer[3] =  x       & 0xFF;

  // Accel Y — big-endian
  int16_t y = rand_range(&chip->curr_state, 16384);
  chip->spi_buffer[4] = (y >> 8) & 0xFF;
  chip->spi_buffer[5] =  y       & 0xFF;

  // Accel Z — ~1 g + noise, big-endian
  int16_t z = 16384 + rand_range(&chip->curr_state, 1000);
  chip->spi_buffer[6] = (z >> 8) & 0xFF;
  chip->spi_buffer[7] =  z       & 0xFF;

  printf("[chip] temp_raw=%u  x=%d  y=%d  z=%d\n", itemp, x, y, z);
}