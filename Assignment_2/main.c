

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"

// ── Pins ──────────────────────────────────────────────────────────────────────
#define PIN_MOSI 19
#define PIN_SDA  16
#define PIN_CS        21
#define PIN_MISO      20
#define PIN_SCK   18
#define PIN_SCL   17
#define PIN_BUTTON    28

// ── Peripherals ───────────────────────────────────────────────────────────────
#define I2C_PORT  i2c0
#define SPI_PORT  spi0

// ── Register addresses ────────────────────────────────────────────────────────
#define CHIP_I2C_ADDR     0x68
#define REG_WHO_AM_I      0x75
#define REG_PWR_MGMT_1    0x6B
#define REG_ACCEL_XOUT_H  0x3B
#define REG_TEMP_OUT_H    0x41

#define SPI_READ_BIT  0x80
#define GRAVITY       9.80665f
#define ACCEL_SCALE   (1.0f / 16384.0f * GRAVITY)

static bool use_i2c    = false;
static bool spi_inited = false;
static bool i2c_inited = false;

#define BUF_LEN 0x08

// ═════════════════════════════════════════════════════════════════════════════
// SPI helpers
// ═════════════════════════════════════════════════════════════════════════════
static inline void cs_select(void) {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);
    asm volatile("nop \n nop \n nop");
}
static inline void cs_deselect(void) {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}

// One SPI transaction: send [reg|READ_BIT, 0x00 × len], capture rx.
// rx[0] is the placeholder byte; rx[1..len] are the chip's response bytes.
// Returns the response in buf[0..len-1].
static void spi_transfer(uint8_t reg, uint8_t *buf, uint16_t len) {
    uint8_t tx[1 + len];
    uint8_t rx[1 + len];
    tx[0] = reg | SPI_READ_BIT;
    for (uint16_t i = 0; i < len; i++) tx[1 + i] = 0x00;
    cs_select();
    spi_write_read_blocking(SPI_PORT, tx, rx, 1 + len);
    cs_deselect();
    sleep_ms(2);
    for (uint16_t i = 0; i < len; i++) buf[i] = rx[1 + i];
}

// Two-phase read: prime (transaction N) then read (transaction N+1).
// The prime tells the chip which register to prepare; the read gets it.
static void spi_read_registers(uint8_t reg, uint8_t *buf, uint16_t len) {
    uint8_t dummy[len];
    spi_transfer(reg, dummy, len);  // prime: chip pre-loads reg data
    spi_transfer(reg, buf,   len);  // read:  receive pre-loaded data
}

static void spi_write_register(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg & 0x7F, val };
    cs_select();
    spi_write_blocking(SPI_PORT, buf, 2);
    cs_deselect();
    sleep_ms(2);
}

// I2C helpers
static void i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len) {
    i2c_write_blocking(I2C_PORT, addr, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, addr, buf, len, false);
}
static void i2c_reg_write(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t msg[2] = { reg, val };
    i2c_write_blocking(I2C_PORT, addr, msg, 2, false);
}

// Here is a switch to spi, where we re init things because I2C disabled them
static void switch_to_spi(void) {
    // if (i2c_inited) { i2c_deinit(I2C_PORT); i2c_inited = false; }

    gpio_set_function(PIN_MISO,     GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    spi_init(SPI_PORT, 500 * 1000);
    // spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_inited = true;

    // Wake chip
    spi_write_register(REG_PWR_MGMT_1, 0x00);
    sleep_ms(10);

    // WHO_AM_I prime: this also pre-loads spi_buf for the first real read
    uint8_t who;
    spi_read_registers(REG_WHO_AM_I, &who, 1);
    printf("[SPI] WHO_AM_I=0x%02X (expect 0x68)\n", who);

    // Prime the accel and temp registers so the first main-loop read is valid
    // uint8_t dummy[6];
    // spi_transfer(REG_ACCEL_XOUT_H, dummy, 6);
    // spi_transfer(REG_TEMP_OUT_H,   dummy, 2);

    printf("[SPI] Active\n");
}

// Switchint to I2C which is not the inital one so we remove the spi
// But just incase we set gpio
static void switch_to_i2c(void) {
    if (spi_inited) {
        spi_deinit(SPI_PORT);
        spi_inited = false;
        gpio_init(PIN_MISO);
        gpio_init(PIN_MOSI);
        gpio_init(PIN_SCK);
        gpio_init(PIN_CS);
        gpio_put(PIN_CS, 1);
    }

    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);

    i2c_init(I2C_PORT, 400 * 1000);
    i2c_inited = true;
    sleep_ms(10);

    // Don't send PWR_MGMT or WHO_AM_I — those MPU-6050 addresses are
    // out of range for this chip. Just do a quick read to confirm it's alive.
    uint8_t buf[1];
    int ret = i2c_read_blocking(I2C_PORT, CHIP_I2C_ADDR, buf, 1, false);
    if (ret < 0) {
        printf("[I2C] ERROR: chip not responding (ret=%d)\n", ret);
    } else {
        printf("[I2C] Active\n");
    }
}


static void spi_read_sensors(int16_t accel[3], int16_t *temp) {
    uint8_t tx[BUF_LEN + 1] = { REG_ACCEL_XOUT_H | SPI_READ_BIT }; // reg addr in byte 0
    uint8_t rx[BUF_LEN + 1] = { 0 };

    cs_select();
    spi_write_read_blocking(SPI_PORT, tx, rx, BUF_LEN + 1);
    cs_deselect();
    sleep_ms(2);

    *temp    = (int16_t)(((rx[0] << 8) & 0x3F00) | rx[1]);
    // printf("Got temp of: %d\n", *temp);
    accel[0] = (int16_t)((rx[2] << 8) | rx[3]);
    accel[1] = (int16_t)((rx[4] << 8) | rx[5]);
    accel[2] = (int16_t)((rx[6] << 8) | rx[7]);
}


static void i2c_read_sensors(int16_t accel[3], int16_t *temp) {
    uint8_t buf[8];
    // Read all 8 bytes in one burst starting at REG_TEMP (0x00)
    i2c_reg_read(CHIP_I2C_ADDR, 0x00, buf, 8);
    // Doing similar reads to spi here because we are using the same data buffer
    *temp    = (int16_t)(((buf[0] << 8) & 0x3F00) | buf[1]);
    accel[0] = (int16_t)((buf[2] << 8) | buf[3]);
    accel[1] = (int16_t)((buf[4] << 8) | buf[5]);
    accel[2] = (int16_t)((buf[6] << 8) | buf[7]);
}


static bool button_pressed(void) {
    static bool last = false, armed = true;
    bool cur = gpio_get(PIN_BUTTON);
    if (cur && armed && !last) {
        sleep_ms(20);
        if (gpio_get(PIN_BUTTON)) { armed = false; last = true; return true; }
    }
    if (!cur) { armed = true; last = false; }
    return false;
}

int main(void) {
    stdio_init_all();
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_down(PIN_BUTTON);
    sleep_ms(500);
    // const float ADC_full_scale = ((float) (2 << 12));
    const float ADC_full_scale = ((float) 0x3FFF)/5.0;
    const float scale = (953./4.65 + 115./0.563569)/2.0;
    const float BETA = 3950; // should match the Beta Coefficient of the thermistor
    printf("scale: %f, max scale? %f, lower scale?: %f\n", scale, 13839/ADC_full_scale*scale, 6155.0/ADC_full_scale*scale);
    switch_to_spi();
    int16_t accel[3], temp;
    while (1) {
        if (button_pressed()) {
            use_i2c = !use_i2c;
            use_i2c ? switch_to_i2c() : switch_to_spi();
        }

        if (use_i2c) i2c_read_sensors(accel, &temp);
        else         spi_read_sensors(accel, &temp);
        // spi_read_sensors(accel, &temp);
        float ax = accel[0] * ACCEL_SCALE;
        float ay = accel[1] * ACCEL_SCALE;
        float az = accel[2] * ACCEL_SCALE;
        // float tc = (temp / 340.0f) + 36.53f;
        // check to be sure we don't process a startup or spurious value
        if (temp > 0) {
          float adc_f = (double) ((float) (temp) / ADC_full_scale * scale);
          // Convert the scaled adc value to degrees Celsius
          // float celsius = 1 / (log(1 / (1023. / analogValue - 1)) / BETA + 1.0 / 298.15) - 273.15;
          float celsius = 1 / (log(1 / (1023. / adc_f - 1)) / BETA + 1.0 / 298.15) - 273.15;
          printf("[%s] Temp=%.2fC  X:%.3f  Y:%.3f  Z:%.3f  m/s2\n",
               use_i2c ? "I2C" : "SPI", celsius, ax, ay, az);
        }



        sleep_ms(1000);
    }
}
