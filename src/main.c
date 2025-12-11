#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <hal/nrf_gpio.h>

LOG_MODULE_REGISTER(uwb_tag_firmware, CONFIG_LOG_DEFAULT_LEVEL);

/* LED0 for nRF52833 Dongle */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* SPI Device for Raw Test */
#define DW3000_NODE DT_NODELABEL(dw3000)
static const struct device *spi_dev = DEVICE_DT_GET(DT_BUS(DW3000_NODE));
static const struct gpio_dt_spec cs_gpio = GPIO_DT_SPEC_GET(DW3000_NODE, cs_gpios);

/* Forward declaration of UWB driver functions */
extern int uwb_driver_init(void);
extern int uwb_send_blink(void);
extern void reset_DWIC(void);

void gpio_scan_disco(void) {
    printk("\n--- STARTING GPIO DISCO SCAN ---\n");
    printk("Watch the board! Each pin will blink for 200ms.\n");
    
    // Scan Port 0
    for (int i = 0; i < 32; i++) {
        // Skip critical pins (Reset=29, UART=6/8, CS=2, SCK=31, MOSI=30, MISO=28)
        if (i == 29 || i == 6 || i == 8 || i == 2 || i == 31 || i == 30 || i == 28) continue;
        
        nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(0, i));
        nrf_gpio_pin_write(NRF_GPIO_PIN_MAP(0, i), 0); // Low
        k_msleep(10);
        nrf_gpio_pin_write(NRF_GPIO_PIN_MAP(0, i), 1); // High
        printk("P0.%02d ", i);
        k_msleep(100);
        nrf_gpio_pin_write(NRF_GPIO_PIN_MAP(0, i), 0); // Low
        nrf_gpio_cfg_input(NRF_GPIO_PIN_MAP(0, i), NRF_GPIO_PIN_NOPULL);
    }
    printk("\n");

    // Scan Port 1 (nRF52833 only has P1.00 - P1.09)
    for (int i = 0; i <= 9; i++) {
        nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(1, i));
        nrf_gpio_pin_write(NRF_GPIO_PIN_MAP(1, i), 0);
        k_msleep(10);
        nrf_gpio_pin_write(NRF_GPIO_PIN_MAP(1, i), 1);
        printk("P1.%02d ", i);
        k_msleep(100);
        nrf_gpio_pin_write(NRF_GPIO_PIN_MAP(1, i), 0);
        nrf_gpio_cfg_input(NRF_GPIO_PIN_MAP(1, i), NRF_GPIO_PIN_NOPULL);
    }
    printk("\n--- DISCO SCAN COMPLETE ---\n");
}

void raw_spi_test(void) {
    struct spi_config spi_cfg = {
        .frequency = 1000000, // Lower to 1MHz
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = NULL,
    };

    uint8_t tx_buf[5] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF}; // Read ID command + dummy
    uint8_t rx_buf[5] = {0};

    struct spi_buf tx = { .buf = tx_buf, .len = 5 };
    struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
    struct spi_buf rx = { .buf = rx_buf, .len = 5 };
    struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };

    printk("RAW SPI TEST: Reading Device ID (Reg 0x00)...\n");

    if (!device_is_ready(spi_dev)) {
        printk("SPI Device NOT Ready!\n");
        return;
    }

    // Manual CS (ACTIVE_LOW: 1=Active/Low, 0=Inactive/High)
    gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE);
    k_msleep(1);
    gpio_pin_set_dt(&cs_gpio, 1); // ACTIVE_LOW: Activate CS (Physical Low)
    k_busy_wait(10);
    
    int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
    
    k_busy_wait(10);
    gpio_pin_set_dt(&cs_gpio, 0); // ACTIVE_LOW: Deactivate CS (Physical High)

    if (ret == 0) {
        printk("SPI Read Success! Data: %02X %02X %02X %02X %02X\n", 
               rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4]);
        uint32_t dev_id = (rx_buf[4] << 24) | (rx_buf[3] << 16) | (rx_buf[2] << 8) | rx_buf[1];
        printk("Device ID: 0x%08X\n", dev_id);
    } else {
        printk("SPI Read Failed: %d\n", ret);
    }
}

/**
 * Main application entry point
 * UWB TAG FIRMWARE - TX Mode (Transmitter/BLINK)
 * 
 * This firmware runs the NRF52833 in TX-only mode:
 * - Sends UWB BLINK frames every 500ms
 * - Enters sleep mode for 490ms between transmissions
 * - NO RX (receive) functionality
 */
int main(void)
{
    int ret = 0;
    uint32_t frame_count = 0;
    bool led_available = false;

    /* Wait for RTT to connect */
    k_msleep(2000);

    printk("\n\n");
    printk("===========================================\n");
    printk("UWB TAG FIRMWARE - DIAGNOSTIC MODE\n");
    printk("===========================================\n");

    /* Check Voltage */
    printk("UICR REGOUT0: 0x%08X (Expected: 0xFFFFFFFF for 3.0V or 0x00000005 for 3.3V)\n", NRF_UICR->REGOUT0);

    /* Initialize LED */
    printk("Checking LED device...\n");
    if (!gpio_is_ready_dt(&led)) {
        printk("ERROR: LED device not ready!\n");
    } else {
        printk("LED device ready, configuring...\n");
        ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        if (ret != 0) {
            printk("ERROR: Failed to configure LED: %d\n", ret);
        } else {
            printk("LED configured successfully! Blinking...\n");
            led_available = true;
            
            /* Blink LED 3 times to show we're alive */
            for (int i = 0; i < 3; i++) {
                gpio_pin_set_dt(&led, 1);
                k_msleep(100);
                gpio_pin_set_dt(&led, 0);
                k_msleep(100);
            }
        }
    }

    /* Run quick SPI diagnostic test first */
    printk("\n===========================================\n");
    printk("DIAGNOSTIC: Testing SPI communication...\n");
    raw_spi_test();
    k_msleep(500);
    
    /* Initialize UWB driver and DW3000 transceiver */
    printk("\n===========================================\n");
    printk("Initializing UWB driver...\n");
    LOG_INF("Initializing UWB driver...");
    
    ret = uwb_driver_init();
    if (ret) {
        printk("ERROR: Failed to initialize UWB driver: %d\n", ret);
        LOG_ERR("Failed to initialize UWB driver: %d", ret);
        printk("System halted. Check SPI connections.\n");
        printk("Running continuous diagnostic...\n");
        while (1) {
            if (led_available) {
                gpio_pin_set_dt(&led, 1);
                k_msleep(100);
                gpio_pin_set_dt(&led, 0);
                k_msleep(900);
            } else {
                k_msleep(1000);
            }
            raw_spi_test();
        }
    }

    printk("UWB Driver initialized successfully!\n");
    printk("Entering TX transmission loop...\n");
    printk("Transmission interval: 500ms\n");
    printk("===========================================\n\n");
    
    LOG_INF("UWB Driver initialized successfully");
    LOG_INF("Entering TX transmission loop - 500ms interval");
    k_msleep(500);

    /* Main TX loop - Send BLINK frame every 500ms */
    while (1) {
        frame_count++;
        
        /* Send UWB BLINK frame (TX operation) */
        if (frame_count % 10 == 1) {
            printk(">>> FRAME #%u: Sending BLINK...\n", frame_count);
        }
        LOG_INF(">>> FRAME #%u: Sending BLINK...", frame_count);
        
        ret = uwb_send_blink();
        if (ret) {
            printk("ERROR: Failed to send BLINK frame: %d\n", ret);
            LOG_ERR("Failed to send BLINK frame: %d", ret);
        }
        
        /* Blink LED every 2 frames (once per second at 500ms interval) */
        if (led_available && (frame_count % 2 == 0)) {
            gpio_pin_set_dt(&led, 1);  // LED ON
            k_msleep(100);
            gpio_pin_set_dt(&led, 0);  // LED OFF
        }

        /* Sleep for 500ms before next transmission */
        k_msleep(500);
    }

    return 0;
}
