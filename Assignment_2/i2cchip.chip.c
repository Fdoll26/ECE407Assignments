// // Wokwi Custom Chip - For docs and examples see:
// // https://docs.wokwi.com/chips-api/getting-started
// //
// // SPDX-License-Identifier: MIT
// // Copyright 2023 Author name

// #include "wokwi-api.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <math.h>
// #include <string.h>


// #define BUF_LEN 0x08 // byte; a 16-bit ADC result is expected
// #define ADDR_MASK 0x3F 
// #define ADDR_GEN_CALL 0

// const float ADC_full_scale = ((float) 0x3FFF) / 5.0;

// typedef struct {
//   pin_t a_in_pin;

//   uint8_t i2c_buf[BUF_LEN];
//   uint32_t i2c;
//   uint32_t curr_state;

//   bool i2c_type;     // read = true, write = false
//   bool i2c_gen_call; // was the last command a general call ?
//   uint32_t addr;
//   uint8_t i2c_ndx;
// } chip_state_t;

// static bool    on_i2c_connect  (void *user_data, uint32_t address, bool connect);
// static uint8_t on_i2c_read     (void *user_data);
// static bool    on_i2c_write    (void *user_data, uint8_t data);
// static void    on_i2c_disconnect(void *user_data);

// void chip_init() {
//   chip_state_t *chip = malloc(sizeof(chip_state_t));

//   // TODO: Initialize the chip, set up IO pins, create timers, etc.
//   chip->a_in_pin = pin_init("A_IN", ANALOG);

//   chip->curr_state = 0xDEADBEEF;

//   const i2c_config_t i2c_config = {
//     .address = 0,
//     .scl = pin_init("SCL", INPUT),
//     .sda = pin_init("SDA", INPUT),
//     .connect = on_i2c_connect,
//     .read = on_i2c_read,
//     .write = on_i2c_write,
//     .disconnect = on_i2c_disconnect,
//     .user_data = chip,
//   };
//   chip->i2c = i2c_init(&i2c_config);
//   // Need to do device ID here, currently it is not set up in the register
//   // chip->regs[REG_WHO_AM_I] = 0xE5;

//   printf("Hello from custom chip!\n");
// }

// static uint32_t lcg_next(uint32_t *s) {
//   *s = (*s) * 1664525u + 1013904223u;
//   return *s;
// }
// static int16_t rand_range(uint32_t *s, int16_t range) {
//   return (int16_t)((int32_t)(lcg_next(s) % (uint32_t)(2*range+1)) - range);
// }

// static void refresh_sensors(chip_state_t *chip) {
//   float temp = ADC_full_scale*pin_adc_read(chip->a_in_pin); 
//   uint16_t itemp = (uint16_t) (temp);
//   chip->i2c_buf[0] = itemp & 0x00FF;
//   chip->i2c_buf[1] = (itemp >> 8) & 0x00FF;

//   int16_t x = rand_range(&chip->curr_state, 16384);
//   chip->i2c_buf[2] = x & 0x00FF;
//   chip->i2c_buf[3] = (x >> 8) & 0x00FF;
//   int16_t y = rand_range(&chip->curr_state, 16384);
//   chip->i2c_buf[4] = y & 0x00FF;
//   chip->i2c_buf[5] = (y >> 8) & 0x00FF;
//   int16_t z = 16384 + rand_range(&chip->curr_state, 1000);
//   chip->i2c_buf[6] = z & 0x00FF;
//   chip->i2c_buf[7] = (z >> 8) & 0x00FF;
// }

// // bool on_i2c_connect(void *user_data, uint32_t address, bool connect) {
// //   chip_state_t *chip = (chip_state_t*)user_data;
// //   if (connect) {
// //     refresh_sensors(chip);
// //     chip->i2c_reg_set = false;
// //   }
// //   return true; // ACK
// // }

// bool on_i2c_connect(void *data, uint32_t address, bool read)
// {
//   chip_state_t *chip = data;

//   chip->i2c_ndx = 0;
//   memset(chip->i2c_buf, 0, BUF_LEN);

//   // check for reset first - proceeded by General Call Address (0x00)
//   if (address == ADDR_GEN_CALL || ((address & ADDR_MASK) == chip->addr))
//   {
//     chip->i2c_gen_call = address == ADDR_GEN_CALL;
//     chip->i2c_type = read;
//     return true; /* Ack */
//   }

//   return false; /* NAck */
// }

// uint8_t on_i2c_read(void *user_data) {
//   chip_state_t *chip = (chip_state_t*)user_data;
//   // uint8_t val = (chip->i2c_reg < REG_FILE_SIZE) ? chip->regs[chip->i2c_reg] : 0;
//   // chip->i2c_reg++;
//   // return val;

//   switch (chip->ctrl_reg)
//   {
//   case MODE1:
//     printf("on_i2c_read mode1: %X\n", chip->mode1);
//     return chip->mode1;
//   case MODE2:
//     printf("on_i2c_read mode2: %X\n", chip->mode2);
//     return chip->mode2;
//   case PRE_SCALE:
//     printf("on_i2c_read prescale: %X\n", chip->prescale);
//     return chip->prescale;
//   }
//   printf("on_i2c_read: unhandled read of reg %X\n", chip->ctrl_reg);
//   return 0;
// }


// static bool on_i2c_write(void *user_data, uint8_t data) {
//   chip_state_t *chip = (chip_state_t*)user_data;
//   // if (!chip->i2c_reg_set) {
//   //   chip->i2c_reg = data;
//   //   chip->i2c_reg_set = true;
//   // } else {
//   //   reg_write(chip, chip->i2c_reg, data);
//   //   chip->i2c_reg++;
//   // }
//   // return true; // ACK
//   if (chip->i2c_ndx >= BUF_LEN)
//   {
//     printf("err: too many bytes written to i2c buffer, resetting\n");
//     // chip_reset(chip);
//     return false;
//   }

//   chip->i2c_buf[chip->i2c_ndx++] = data;
//   return true; // Ack
// }

// static void on_i2c_disconnect(void *user_data) {
//   chip_state_t *chip = (chip_state_t*)user_data;
//   chip->i2c_reg_set = false;
// }

// Wokwi Custom Chip - IMU + Temp Sensor (I2C)
// 8 bytes: [0-1] = temp (ADC), [2-3] = accel X, [4-5] = accel Y, [6-7] = accel Z
//
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_LEN       8
#define CHIP_ADDR     0x68   // Change to match your diagram.json

// Registers (first byte written sets the read pointer)
#define REG_TEMP      0x00   // 2 bytes, 14-bit ADC result
#define REG_ACCEL_X   0x02   // 2 bytes, signed 16-bit
#define REG_ACCEL_Y   0x04
#define REG_ACCEL_Z   0x06

// Full-scale: 14-bit result mapped to 0–5 V
#define ADC_FULL_SCALE  ((float)0x3FFF / 5.0f)

typedef struct {
  pin_t    a_in_pin;
  uint32_t i2c;

  uint8_t  buf[BUF_LEN];   // flat register file
  uint8_t  reg_ptr;        // current register pointer
  bool     reg_ptr_set;    // has the register pointer been written this transaction?

  uint32_t lcg_state;      // LCG PRNG state
} chip_state_t;

// ── PRNG ──────────────────────────────────────────────────────────────────────

static uint32_t lcg_next(uint32_t *s) {
  *s = (*s) * 1664525u + 1013904223u;
  return *s;
}

static int16_t rand_range(uint32_t *s, int16_t range) {
  return (int16_t)((int32_t)(lcg_next(s) % (uint32_t)(2 * range + 1)) - range);
}

// ── Sensor refresh ────────────────────────────────────────────────────────────

static void refresh_sensors(chip_state_t *chip) {
  // Temperature: read analog pin, scale to 14-bit unsigned
  float   voltage = pin_adc_read(chip->a_in_pin);
  uint16_t raw_t  = (uint16_t)(ADC_FULL_SCALE * voltage);
  chip->buf[REG_TEMP]     =  raw_t       & 0xFF;
  chip->buf[REG_TEMP + 1] = (raw_t >> 8) & 0xFF;

  // Accelerometer X, Y: random noise across full signed range
  int16_t x = rand_range(&chip->lcg_state, 16384);
  chip->buf[REG_ACCEL_X]     =  x       & 0xFF;
  chip->buf[REG_ACCEL_X + 1] = (x >> 8) & 0xFF;

  int16_t y = rand_range(&chip->lcg_state, 16384);
  chip->buf[REG_ACCEL_Y]     =  y       & 0xFF;
  chip->buf[REG_ACCEL_Y + 1] = (y >> 8) & 0xFF;

  // Accelerometer Z: ~1 g (16384 LSB) + small noise
  int16_t z = 16384 + rand_range(&chip->lcg_state, 1000);
  chip->buf[REG_ACCEL_Z]     =  z       & 0xFF;
  chip->buf[REG_ACCEL_Z + 1] = (z >> 8) & 0xFF;

  printf("[chip] temp_raw=%u  x=%d  y=%d  z=%d\n", raw_t, x, y, z);
}

// ── I2C callbacks ─────────────────────────────────────────────────────────────

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

// ── chip_init ─────────────────────────────────────────────────────────────────

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  memset(chip, 0, sizeof(chip_state_t));

  chip->a_in_pin  = pin_init("A_IN", ANALOG);
  chip->lcg_state = 0xDEADBEEF; // deterministic seed; change as desired

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

  printf("[chip] IMU+Temp sensor ready at I2C addr 0x%02X\n", CHIP_ADDR);
}