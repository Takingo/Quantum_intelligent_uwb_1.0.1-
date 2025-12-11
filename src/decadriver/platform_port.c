#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdint.h>
#include "deca_device_api.h"

LOG_MODULE_REGISTER(platform_port, LOG_LEVEL_INF);

const struct device *spi_dev;
struct spi_config spi_cfg;
static const struct gpio_dt_spec rst_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(dw3000), reset_gpios);
static const struct gpio_dt_spec cs_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(dw3000), cs_gpios);

void openspi(void) {
    /* dw3000'in bağlı olduğu BUS'ı (SPI3) otomatik bul */
    spi_dev = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(dw3000)));
    if (!device_is_ready(spi_dev)) { 
        LOG_ERR("SPI Device Not Ready!"); 
        return; 
    }

    /* Configure CS GPIO manually */
    if (!gpio_is_ready_dt(&cs_gpio)) { 
        LOG_ERR("CS GPIO Not Ready!"); 
        return; 
    }
    gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE); // Start High (Inactive)

    /* SPI Configuration - 2MHz for DW3000, Mode 0 (CPOL=0, CPHA=0) */
    spi_cfg.frequency = 2000000;
    spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
    spi_cfg.slave = 0;
    // Manual CS control via GPIO (no cs struct needed)
    
    LOG_INF("SPI3 Initialized: 2MHz, Mode 0, Manual CS");
}

void peripherals_init(void) {
    openspi();
}

void closespi(void) {}

int readfromspi(uint16_t headerLength, uint8_t *headerBuffer, uint16_t readlength, uint8_t *readBuffer) {
    int ret;
    struct spi_buf tx_buf = { .buf = headerBuffer, .len = headerLength };
    struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf rx_bufs[2] = { { .buf = NULL, .len = headerLength }, { .buf = readBuffer, .len = readlength } };
    struct spi_buf_set rx = { .buffers = rx_bufs, .count = 2 };

    gpio_pin_set_dt(&cs_gpio, 1); // ACTIVE_LOW: 1 = Active (Low)
    k_busy_wait(1); // Short delay for CS setup time
    ret = spi_transceive(spi_dev, &spi_cfg, &tx, &rx);
    k_busy_wait(1); // Short delay before CS release
    gpio_pin_set_dt(&cs_gpio, 0); // ACTIVE_LOW: 0 = Inactive (High)
    
    return ret;
}

int writetospi(uint16_t headerLength, uint8_t *headerBuffer, uint16_t bodylength, uint8_t *bodyBuffer) {
    int ret;
    struct spi_buf tx_bufs[2] = { { .buf = headerBuffer, .len = headerLength }, { .buf = bodyBuffer, .len = bodylength } };
    struct spi_buf_set tx = { .buffers = tx_bufs, .count = 2 };

    gpio_pin_set_dt(&cs_gpio, 1); // ACTIVE_LOW: 1 = Active (Low)
    k_busy_wait(1); // Short delay for CS setup time
    ret = spi_write(spi_dev, &spi_cfg, &tx);
    k_busy_wait(1); // Short delay before CS release
    gpio_pin_set_dt(&cs_gpio, 0); // ACTIVE_LOW: 0 = Inactive (High)

    return ret;
}

void deca_sleep(uint8_t time_ms) { k_msleep(time_ms); }
void deca_usleep(uint8_t time_us) { k_busy_wait(time_us); }
decaIrqStatus_t decamutexon(void) { return 0; }
void decamutexoff(decaIrqStatus_t s) { }

void reset_DWIC(void) {
    if (!gpio_is_ready_dt(&rst_gpio)) {
        LOG_ERR("Reset GPIO not ready");
        return;
    }
    LOG_INF("Resetting DW3000...");
    gpio_pin_configure_dt(&rst_gpio, GPIO_OUTPUT_ACTIVE); // Drive Low (Reset)
    k_sleep(K_MSEC(10));
    gpio_pin_configure_dt(&rst_gpio, GPIO_OUTPUT_INACTIVE); // Drive High (Run)
    k_sleep(K_MSEC(50)); // Increased wait time for startup
    LOG_INF("DW3000 Reset Complete");
}
