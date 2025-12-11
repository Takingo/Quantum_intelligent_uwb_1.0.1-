#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "deca_device_api.h"
#include "deca_regs.h"

LOG_MODULE_REGISTER(uwb_driver, LOG_LEVEL_INF);

extern void peripherals_init(void);
extern void reset_DWIC(void);

static dwt_config_t config = {
    5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1, DWT_BR_6M8, 
    DWT_PHRMODE_STD, DWT_PHRRATE_STD, (129 + 8 - 8), 
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};
static dwt_txconfig_t txconfig = {
    0x34,           /* PG delay. */
    0xfdfdfdfd,      /* TX power. */
    0x0             /*PG count*/
};
static uint8_t seq_num = 0;

int uwb_driver_init(void) {
    int ret;
    uint32_t dev_id = 0;
    
    LOG_INF("=== UWB Driver Initialization Start ===");
    
    // Step 1: Initialize SPI and GPIOs
    LOG_INF("Step 1: Initializing peripherals (SPI3, CS, RST)...");
    peripherals_init(); 
    k_msleep(10);
    
    // Step 2: Hardware reset DW3000
    LOG_INF("Step 2: Performing hardware reset...");
    reset_DWIC(); 
    k_msleep(50); // Wait for chip startup
    
    // Step 3: Read Device ID before initialization
    LOG_INF("Step 3: Reading Device ID (Register 0x00)...");
    dev_id = dwt_readdevid();
    LOG_INF("Device ID (raw): 0x%08X", dev_id);
    
    if (dev_id == 0x00000000 || dev_id == 0xFFFFFFFF) {
        LOG_ERR("ERROR: Invalid Device ID! SPI communication failure.");
        LOG_ERR("Check: CS=P0.02, SCK=P0.31, MOSI=P0.30, MISO=P0.28, RST=P0.29");
        return -1;
    }
    
    // Step 4: Initialize DW3000
    LOG_INF("Step 4: Calling dwt_initialise(DWT_DW_INIT)...");
    ret = dwt_initialise(DWT_DW_INIT);
    if (ret == DWT_ERROR) {
        LOG_ERR("ERROR: dwt_initialise() failed!");
        return -1;
    }
    LOG_INF("dwt_initialise() SUCCESS!");
    
    // Step 5: Verify Device ID again
    dev_id = dwt_readdevid();
    LOG_INF("Device ID (post-init): 0x%08X", dev_id);
    
    // Check for known DW3000 variants
    if (dev_id == 0xDECA0302 || dev_id == 0xDECA0312) {
        LOG_INF("SUCCESS: DW3000 C0 detected! (PDOA=%s)", 
                dev_id == 0xDECA0312 ? "YES" : "NO");
    } else if (dev_id == 0xDECA0301 || dev_id == 0xDECA0311) {
        LOG_INF("SUCCESS: DW3000 B0 detected! (PDOA=%s)", 
                dev_id == 0xDECA0311 ? "YES" : "NO");
    } else {
        LOG_WRN("WARNING: Unknown Device ID: 0x%08X (expected 0xDECA030x)", dev_id);
    }
    
    // Step 6: Configure UWB parameters
    LOG_INF("Step 5: Configuring UWB parameters...");
    dwt_configure(&config);
    dwt_configuretxrf(&txconfig);
    
    LOG_INF("=== UWB Driver Initialization Complete ===");
    return 0;
}

int uwb_send_blink(void) {
    uint32_t status;
    int timeout = 0;
    
    // Standard IEEE 802.15.4 BLINK Frame
    // Frame Control (0xC5) + Seq# + Source Address (8 bytes) + FCS (2 bytes)
    uint8_t frame[12] = { 
        0xC5,           // Frame Control: BLINK frame type
        seq_num++,      // Sequence number (auto-increment)
        0x01,           // Source Address byte 0 (Tag ID)
        0x23,           // Source Address byte 1
        0x45,           // Source Address byte 2
        0x67,           // Source Address byte 3
        0x89,           // Source Address byte 4
        0xAB,           // Source Address byte 5
        0xCD,           // Source Address byte 6
        0xEF,           // Source Address byte 7
        0x00, 0x00      // FCS (will be auto-calculated by DW3000)
    };
    
    // Force IDLE state
    dwt_forcetrxoff();
    k_busy_wait(10);
    
    // Write frame to TX buffer
    dwt_writetxdata(sizeof(frame), frame, 0);
    dwt_writetxfctrl(sizeof(frame), 0, 0);
    
    // Start immediate transmission
    if (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
        LOG_ERR("TX Start failed!");
        return -1;
    }
    
    // Polling Mode: Wait for TX complete (max 10ms)
    while (timeout < 10000) {
        status = dwt_read32bitreg(SYS_STATUS_ID);
        if (status & SYS_STATUS_TXFRS_BIT_MASK) {
            // Clear TX complete flag
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
            LOG_DBG("TX BLINK OK - Seq: %d, Timeout: %d us", frame[1], timeout);
            return 0;
        }
        k_busy_wait(100);
        timeout += 100;
    }
    
    LOG_ERR("TX Timeout! Status: 0x%08X", status);
    return -1;
}
