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
#define BUF_LEN       8
#define CHIP_ADDR     0x68   // Change to match your diagram.json

// Registers (first byte written sets the read pointer)
#define REG_TEMP      0x00   // 2 bytes, 14-bit ADC result
#define REG_ACCEL_X   0x02   // 2 bytes, signed 16-bit
#define REG_ACCEL_Y   0x04
#define REG_ACCEL_Z   0x06

typedef struct {
  pin_t    a_in_pin;
  pin_t    cs_pin;
  uint32_t spi;

  uint32_t curr_state;
  uint32_t i2c;

  uint8_t  buf[BUF_LEN];   // flat register file
  uint8_t  reg_ptr;        // current register pointer
  bool     reg_ptr_set;    // has the register pointer been written this transaction?
} chip_state_t;

static void chip_pin_change(void *user_data, pin_t pin, uint32_t value);
static void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count);

static bool on_i2c_connect(void *user_data, uint32_t address, bool read);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t data);
static void on_i2c_disconnect(void *user_data);

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

  const i2c_config_t i2c_cfg = {
    .address    = CHIP_ADDR,
    .scl        = pin_init("SCL", INPUT),
    .sda        = pin_init("SDA", INPUT),
    .connect    = on_i2c_connect,
    .read       = on_i2c_read,
    .write      = on_i2c_write,
    .disconnect = on_i2c_disconnect,
    .user_data  = chip,
  };
  chip->i2c = i2c_init(&i2c_cfg);
  
  printf("SPI I2C Chip initialized!\n");
}

void chip_pin_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t*)user_data;
  // Handle nCS pin logic
  if (pin == chip->cs_pin) {
    if (value == LOW) {
      spi_start(chip->spi, chip->buf, sizeof(chip->buf));
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

static void refresh_sensors(chip_state_t *chip) {
  // Temperature: read analog pin, scale to 14-bit unsigned
  float normalized = ADC_full_scale*pin_adc_read(chip->a_in_pin);  // 0.0 to 1.0
  // uint16_t itemp = (uint16_t)(normalized * 0x3FFF);
  uint16_t itemp = (uint16_t)(normalized);
  chip->buf[REG_TEMP]     = (itemp >> 8) & 0xFF;
  chip->buf[REG_TEMP + 1] = itemp & 0xFF;

  // Accelerometer X, Y: random noise across full signed range
  int16_t x = rand_range(&chip->curr_state, 16384);
  chip->buf[REG_ACCEL_X]     = (x >> 8) & 0xFF;
  chip->buf[REG_ACCEL_X + 1] = x & 0xFF;

  int16_t y = rand_range(&chip->curr_state, 16384);
  chip->buf[REG_ACCEL_Y]     = (y >> 8) & 0xFF;
  chip->buf[REG_ACCEL_Y + 1] = y & 0xFF;

  // Accelerometer Z: ~1 g (16384 LSB) + small noise
  int16_t z = 16384 + rand_range(&chip->curr_state, 1000);
  chip->buf[REG_ACCEL_Z]     = (z >> 8) & 0xFF;
  chip->buf[REG_ACCEL_Z + 1] = z & 0xFF;

  printf("[chip] temp=%u  x=%d  y=%d  z=%d\n", itemp, x, y, z);
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = user_data;

  if (address != CHIP_ADDR) return false; // NAK

  chip->reg_ptr_set = false;

  if (read) {
    // Refresh data just before a read transaction
    refresh_sensors(chip);
  }

  return true; // ACK
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = user_data;

  if (chip->reg_ptr >= BUF_LEN) {
    printf("[chip] read past end of register file\n");
    return 0xFF;
  }

  uint8_t val = chip->buf[chip->reg_ptr];
  // printf("[chip] read reg[%02X] = %02X\n", chip->reg_ptr, val);
  chip->reg_ptr++;   // auto-increment for burst reads
  return val;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = user_data;

  if (!chip->reg_ptr_set) {
    // First byte of a write = register pointer
    if (data >= BUF_LEN) {
      printf("[chip] invalid register address 0x%02X\n", data);
      return false; // NAK
    }
    chip->reg_ptr     = data;
    chip->reg_ptr_set = true;
    printf("[chip] register pointer set to 0x%02X\n", data);
  } else {
    // Subsequent bytes = register writes (registers are read-only in this chip,
    // but we ACK silently to avoid bus errors from the host)
    printf("[chip] write to reg[%02X] = %02X (ignored, read-only)\n",
           chip->reg_ptr, data);
    chip->reg_ptr++;
  }
  return true; // ACK
}

static void on_i2c_disconnect(void *user_data) {
  chip_state_t *chip = user_data;
  chip->reg_ptr_set  = false;
  printf("[chip] disconnected\n");
}


void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  chip_state_t *chip = (chip_state_t*)user_data;
  if (!count) return;

  // Temperature — big-endian (high byte first, matches MPU-6050 convention)
  // float normalized = ADC_full_scale*pin_adc_read(chip->a_in_pin);  // 0.0 to 1.0
  // // uint16_t itemp = (uint16_t)(normalized * 0x3FFF);
  // uint16_t itemp = (uint16_t)(normalized);
  // chip->buf[0] = (itemp >> 8) & 0x00FF; // high byte first
  // chip->buf[1] =  itemp       & 0x00FF;

  // // Accel X — big-endian
  // int16_t x = rand_range(&chip->curr_state, 16384);
  // chip->buf[2] = (x >> 8) & 0xFF;
  // chip->buf[3] =  x       & 0xFF;

  // // Accel Y — big-endian
  // int16_t y = rand_range(&chip->curr_state, 16384);
  // chip->buf[4] = (y >> 8) & 0xFF;
  // chip->buf[5] =  y       & 0xFF;

  // // Accel Z — ~1 g + noise, big-endian
  // int16_t z = 16384 + rand_range(&chip->curr_state, 1000);
  // chip->buf[6] = (z >> 8) & 0xFF;
  // chip->buf[7] =  z       & 0xFF;
  refresh_sensors(chip);
}