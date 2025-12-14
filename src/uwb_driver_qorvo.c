#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "deca_device_api.h"
#include "deca_regs.h"

LOG_MODULE_REGISTER(uwb_driver, LOG_LEVEL_INF);

extern void peripherals_init(void);
extern void reset_DWIC(void);
extern void uwb_led_pulse(void);

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

/* TWR Frame Types */
#define FUNC_CODE_POLL   0x61
#define FUNC_CODE_RESP   0x50
#define FUNC_CODE_FINAL  0x69

/* TWR Timestamps (40-bit) */
static uint64_t poll_tx_ts = 0;
static uint64_t resp_rx_ts = 0;
static uint64_t final_tx_ts = 0;

/* Speed of light in air, in metres per microsecond */
#define SPEED_OF_LIGHT 299702547.0

/* UWB microsecond (uus) to device time unit (dtu, around 15.65 ps) conversion factor.
 * 1 uus = 512 / 499.2 µs and 1 µs = 499.2 * 128 dtu. */
#define UUS_TO_DWT_TIME 65536

/* Time-stamps get transmitted and received as 40-bit, so we need to remove any 24-bit overflow. */
static uint64_t get_tx_timestamp_u64(void) {
    uint8_t ts_tab[5];
    uint64_t timestamp = 0;
    
    dwt_readtxtimestamp(ts_tab);
    
    // DW3000: Least Significant Byte is at index 0
    timestamp = ((uint64_t)ts_tab[0]) |
                (((uint64_t)ts_tab[1]) << 8) |
                (((uint64_t)ts_tab[2]) << 16) |
                (((uint64_t)ts_tab[3]) << 24) |
                (((uint64_t)ts_tab[4]) << 32);
    
    return timestamp;
}

static uint64_t get_rx_timestamp_u64(void) {
    uint8_t ts_tab[5];
    uint64_t timestamp = 0;
    
    dwt_readrxtimestamp(ts_tab);
    
    // DW3000: Least Significant Byte is at index 0
    timestamp = ((uint64_t)ts_tab[0]) |
                (((uint64_t)ts_tab[1]) << 8) |
                (((uint64_t)ts_tab[2]) << 16) |
                (((uint64_t)ts_tab[3]) << 24) |
                (((uint64_t)ts_tab[4]) << 32);
    
    return timestamp;
}

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
    
    // Step 4: Check IDLE
    LOG_INF("Step 4: Checking IDLE RC...");
    int idle_retry = 0;
    while (!dwt_checkidlerc() && idle_retry < 5) {
        k_msleep(100);
        idle_retry++;
    }
    if (idle_retry >= 5) {
        LOG_WRN("IDLE check timeout (continuing anyway)");
    } else {
        LOG_INF("IDLE RC OK");
    }
    
    // Step 5: Soft reset
    LOG_INF("Step 5: Performing soft reset...");
    dwt_softreset();
    k_msleep(200);
    
    // Step 6: Initialize DW3000
    LOG_INF("Step 6: Calling dwt_initialise(DWT_DW_INIT)...");
    ret = dwt_initialise(DWT_DW_INIT);
    if (ret == DWT_ERROR) {
        LOG_ERR("ERROR: dwt_initialise() failed!");
        return -1;
    }
    LOG_INF("dwt_initialise() SUCCESS!");
    
    // Step 7: Verify Device ID again
    dev_id = dwt_readdevid();
    LOG_INF("Device ID (post-init): 0x%08X", dev_id);
    
    // Step 8: DISABLE DW3000 LEDs for battery saving!
    LOG_INF("Step 8: Disabling DW3000 LEDs (battery save)...");
    dwt_setleds(DWT_LEDS_DISABLE);  // Disable hardware LEDs to save power!
    
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
    
    // Step 9: Configure UWB parameters
    LOG_INF("Step 9: Configuring UWB (CH5 @ 6.8Mbps)...");
    ret = dwt_configure(&config);
    if (ret != DWT_SUCCESS) {
        LOG_ERR("PLL LOCK FAILED!");
        return -1;
    }
    LOG_INF("PLL LOCK OK!");
    
    // Step 10: Configure TX power (MAXIMUM for better range)
    LOG_INF("Step 10: Setting TX power to MAX...");
    txconfig.power = 0xFEFEFEFE; // Maximum power
    dwt_configuretxrf(&txconfig);
    
    // Step 11: Set antenna delays
    LOG_INF("Step 11: Setting antenna delays...");
    dwt_setrxantennadelay(16385); // RX antenna delay
    dwt_settxantennadelay(16385); // TX antenna delay
    
    // Step 12: Configure RX after TX delay and timeout (CRITICAL for TWR!)
    LOG_INF("Step 12: Configuring RX after TX...");
    dwt_setrxaftertxdelay(0);    // 0us delay - START RX IMMEDIATELY!
    dwt_setrxtimeout(0);         // NO TIMEOUT - wait forever!
    dwt_setpreambledetecttimeout(0); // NO preamble timeout!
    
    // Step 13: Enable LNA/PA
    LOG_INF("Step 13: Enabling LNA/PA...");
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
    
    // Step 14: DISABLE frame filtering - accept ALL frames (CRITICAL for TWR!)
    LOG_INF("Step 14: Disabling frame filtering...");
    dwt_configureframefilter(DWT_FF_DISABLE, 0);
    
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

/* TWR Step 1: Send POLL (IEEE 802.15.4 format) */
int uwb_send_poll(void) {
    uint32_t status;
    
    // *** CRITICAL: Force IMMEDIATE timestamp reset! Prevent old values! ***
    poll_tx_ts = 0;
    resp_rx_ts = 0;
    
    /* IEEE 802.15.4 POLL format */
    /* Frame: FC(2) + Seq(1) + PAN(2) + Dest(2) + Src(2) + MsgType(1) */
    static uint8_t tx_poll_msg[10] = {
        0x41, 0x88,      // Frame Control
        0,               // Sequence Number
        0xCA, 0xDE,      // PAN ID
        0xFF, 0xFF,      // Dest Addr (Broadcast)
        0x01, 0x00,      // Src Addr (Tag ID 1)
        0x61             // Msg Type (POLL)
    };
    
    tx_poll_msg[2] = seq_num++;
    
    // Force IDLE first
    dwt_forcetrxoff();
    k_busy_wait(50);
    
    // Clear ALL status flags
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_TX | SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_ERR);
    
    dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
    dwt_writetxfctrl(sizeof(tx_poll_msg) + 2, 0, 1); // ranging=1
    
    // *** AUTO RX ENABLE - DW3000 starts RX automatically after TX! ***
    dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
    
    uwb_led_pulse();
    
    // Wait TX complete
    int tx_timeout = 0;
    while (!((status = dwt_read32bitreg(SYS_STATUS_ID)) & SYS_STATUS_TXFRS_BIT_MASK)) {
        k_busy_wait(10);
        tx_timeout++;
        if (tx_timeout > 1000) {
            LOG_ERR("TX timeout!");
            return -1;
        }
    }
    
    poll_tx_ts = get_tx_timestamp_u64();
    LOG_INF("✅ POLL sent! TX_TS: 0x%010llX (Seq: %d)", poll_tx_ts, tx_poll_msg[2]);
    
    return 0;
}

/* TWR: Step 2 - Receive RESP frame with ANCHOR timestamps */
static uint64_t poll_rx_ts_anchor = 0;  // ANCHOR's POLL RX timestamp
static uint64_t resp_tx_ts_anchor = 0;  // ANCHOR's RESP TX timestamp
static uint32_t calculated_dist_mm = 0; // Calculated distance to send to Anchor

int uwb_wait_resp(void) {
    uint32_t status;
    uint8_t rx_buffer[128];
    uint16_t frame_len;
    int status_check_count = 0;
    
    LOG_INF("⏳ Waiting for RESPONSE (3sec timeout)...");
    
    // Wait for RX complete or error (max 3000ms)
    for (int i = 0; i < 3000; i++) {
        status = dwt_read32bitreg(SYS_STATUS_ID);
        status_check_count++;
        
        // Good frame received
        if (status & SYS_STATUS_RXFCG_BIT_MASK) {
            frame_len = dwt_read32bitreg(RX_FINFO_ID) & 0x3FF;
            
            if (frame_len > 0 && frame_len < 128) {
                dwt_readrxdata(rx_buffer, frame_len, 0);
                
                // IEEE 802.15.4 RESPONSE format: 
                // FC(2) + Seq(1) + PAN(2) + Dest(2) + Src(2) + MsgType(1) + Payload...
                // MsgType at index 9 should be 0x50 (RESP)
                
                if (frame_len >= 20 && rx_buffer[9] == 0x50) {
                    
                    // Get TAG's RESP RX timestamp
                    resp_rx_ts = get_rx_timestamp_u64();
                    
                    // Extract ANCHOR's POLL_RX timestamp (bytes 10-14)
                    poll_rx_ts_anchor = 0;
                    for (int i = 0; i < 5; i++) {
                        poll_rx_ts_anchor |= ((uint64_t)rx_buffer[10 + i]) << (i * 8);
                    }
                    
                    // Extract ANCHOR's RESP_TX timestamp (bytes 15-19)
                    resp_tx_ts_anchor = 0;
                    for (int i = 0; i < 5; i++) {
                        resp_tx_ts_anchor |= ((uint64_t)rx_buffer[15 + i]) << (i * 8);
                    }
                    
                    LOG_INF("⏱️  TAG: POLL_TX=0x%010llX, RESP_RX=0x%010llX", poll_tx_ts, resp_rx_ts);
                    LOG_INF("⏱️  ANCHOR: POLL_RX=0x%010llX, RESP_TX=0x%010llX", 
                            poll_rx_ts_anchor, resp_tx_ts_anchor);
                    
                    // --- CALCULATE DISTANCE (SS-TWR) ---
                    int64_t Ra, Db;
                    double tof, dist;
                    
                    // Ra = RESP_RX - POLL_TX
                    if (resp_rx_ts >= poll_tx_ts) Ra = resp_rx_ts - poll_tx_ts;
                    else Ra = (0x10000000000ULL - poll_tx_ts) + resp_rx_ts;
                    
                    // Db = RESP_TX - POLL_RX
                    if (resp_tx_ts_anchor >= poll_rx_ts_anchor) Db = resp_tx_ts_anchor - poll_rx_ts_anchor;
                    else Db = (0x10000000000ULL - poll_rx_ts_anchor) + resp_tx_ts_anchor;
                    
                    tof = ((double)Ra - (double)Db) / 2.0;
                    if (tof < 0) tof = 0;
                    
                    // Distance = ToF * Speed of Light
                    // 1 DU = 15.65e-12 s, Speed = 299792458 m/s
                    dist = tof * 15.65e-12 * 299792458.0;
                    calculated_dist_mm = (uint32_t)(dist * 1000.0); // Convert to mm
                    
                    LOG_INF("📏 Calculated Distance: %d mm", calculated_dist_mm);
                    
                    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_ERR);
                    LOG_INF("✅ RESP received!");
                    return 0;
                } else {
                    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_GOOD);
                }
            }
        }
        
        // RX errors - clear and continue
        if (status & SYS_STATUS_ALL_RX_ERR) {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
        }
        
        k_msleep(1);
    }
    
    LOG_ERR("❌ RESP timeout");
    dwt_forcetrxoff();
    return -1;
}

/* TWR: Step 3 - Send FINAL frame with calculated distance */
int uwb_send_final(void) {
    uint32_t status;
    int timeout = 0;
    
    /* FINAL Frame: FC(2) + Seq(1) + PAN(2) + Dest(2) + Src(2) + MsgType(1) + 
     *              DISTANCE_MM(4) = 14 bytes
     */
    uint8_t final_frame[14] = {
        0x41, 0x88,           // [0-1] Frame Control
        0,                    // [2] Sequence
        0xCA, 0xDE,           // [3-4] PAN ID
        0x02, 0x00,           // [5-6] Destination (ANCHOR ID = 0x0002)
        0x01, 0x00,           // [7-8] Source (TAG ID = 0x0001)
        0x23,                 // [9] Msg Type: FINAL (0x23)
        0, 0, 0, 0            // [10-13] Distance in mm (uint32_t)
    };
    
    final_frame[2] = seq_num++;
    
    LOG_INF("🔹 Sending FINAL frame with Distance: %d mm", calculated_dist_mm);
    
    // Embed distance (Little Endian)
    final_frame[10] = calculated_dist_mm & 0xFF;
    final_frame[11] = (calculated_dist_mm >> 8) & 0xFF;
    final_frame[12] = (calculated_dist_mm >> 16) & 0xFF;
    final_frame[13] = (calculated_dist_mm >> 24) & 0xFF;
    
    dwt_forcetrxoff();
    k_busy_wait(10);
    
    dwt_writetxdata(sizeof(final_frame), final_frame, 0);
    dwt_writetxfctrl(sizeof(final_frame) + 2, 0, 1); // +2 FCS, ranging=1
    
    // Immediate TX
    if (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
        LOG_ERR("FINAL TX failed!");
        return -1;
    }
    
    uwb_led_pulse(); // LED pulse when sending FINAL
    
    while (timeout < 10000) {
        status = dwt_read32bitreg(SYS_STATUS_ID);
        if (status & SYS_STATUS_TXFRS_BIT_MASK) {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
            LOG_INF("✅ FINAL sent!");
            return 0;
        }
        k_busy_wait(100);
        timeout += 100;
    }
    
    LOG_ERR("FINAL TX timeout!");
    return -1;
}

/* Calculate distance using TWR timestamps - SS-TWR with explicit Anchor Delay */
static double calculate_distance(void) {
    double tof;
    double distance;
    
    // Already masked to 40-bit in get_timestamp functions
    uint64_t poll_tx_40 = poll_tx_ts;
    uint64_t resp_rx_40 = resp_rx_ts;
    uint64_t poll_rx_anchor_40 = poll_rx_ts_anchor;
    uint64_t resp_tx_anchor_40 = resp_tx_ts_anchor;
    
    // Calculate Ra (Tag Round Trip)
    int64_t Ra;
    if (resp_rx_40 >= poll_tx_40) {
        Ra = (int64_t)(resp_rx_40 - poll_tx_40);
    } else {
        Ra = (int64_t)((0x10000000000ULL - poll_tx_40) + resp_rx_40);
    }
    
    // Calculate Db (Anchor Reply Delay)
    int64_t Db;
    if (resp_tx_anchor_40 >= poll_rx_anchor_40) {
        Db = (int64_t)(resp_tx_anchor_40 - poll_rx_anchor_40);
    } else {
        Db = (int64_t)((0x10000000000ULL - poll_rx_anchor_40) + resp_tx_anchor_40);
    }
    
    LOG_INF("═══ Distance Calculation (SS-TWR) ═══");
    LOG_INF("  TAG POLL_TX:    0x%010llX", poll_tx_40);
    LOG_INF("  TAG RESP_RX:    0x%010llX", resp_rx_40);
    LOG_INF("  ANCHOR POLL_RX: 0x%010llX", poll_rx_anchor_40);
    LOG_INF("  ANCHOR RESP_TX: 0x%010llX", resp_tx_anchor_40);
    LOG_INF("  Ra (Tag Loop):  %lld DU", Ra);
    LOG_INF("  Db (Anchor Dly):%lld DU", Db);
    
    // TWR Formula: ToF = (Ra - Db) / 2
    tof = ((double)Ra - (double)Db) / 2.0;
    
    LOG_INF("  ToF (calculated): %.2f DU (%.3f ns)", tof, tof * 15.65);
    
    // Sanity checks
    if (tof < 0) {
        LOG_WRN("  ⚠️  Negative ToF! Ra < Db. Setting to 0.");
        tof = 0.0;
    }
    
    // Convert DU to seconds: 1 DU = 1/(499.2 MHz × 128) ≈ 15.65 picoseconds
    double tof_sec = tof * (1.0 / (499.2e6 * 128.0));
    
    // Distance = ToF × Speed of Light
    distance = tof_sec * SPEED_OF_LIGHT;
    
    LOG_INF("  ⏱️  ToF: %.6f microseconds", tof_sec * 1e6);
    LOG_INF("  📏 Distance: %.3f meters (%.1f cm)", distance, distance * 100.0);
    
    return distance;
}

/* Complete TWR cycle - DS-TWR METHOD (3 messages with FINAL) */
int uwb_twr_cycle(void) {
    LOG_INF("━━━━━━ Starting SS-TWR Cycle ━━━━━━");
    
    // *** CRITICAL: Reset ALL timestamps at start of EVERY cycle! ***
    poll_tx_ts = 0;
    resp_rx_ts = 0;
    final_tx_ts = 0;
    LOG_INF("🔹 Timestamps RESET: poll_tx=0x%llX, resp_rx=0x%llX", poll_tx_ts, resp_rx_ts);
    
    // Step 1: Send POLL
    if (uwb_send_poll() != 0) {
        LOG_ERR("❌ POLL failed");
        return -1;
    }
    
    k_msleep(5); // Small delay
    
    // Step 2: Wait for RESP
    if (uwb_wait_resp() != 0) {
        LOG_ERR("❌ RESP not received");
        return -1;
    }
    
    k_msleep(5); // Small delay before FINAL
    
    // Step 3: Send FINAL with TAG timestamps to ANCHOR
    if (uwb_send_final() != 0) {
        LOG_ERR("❌ FINAL send failed");
        return -1;
    }
    
    // Step 4: Calculate distance at TAG (we have all timestamps now)
    LOG_INF("━━━━━━ Calculating Distance at TAG ━━━━━━");
    LOG_INF("   POLL_TX:  0x%010llX", poll_tx_ts);
    LOG_INF("   RESP_RX:  0x%010llX", resp_rx_ts);
    LOG_INF("   FINAL_TX: 0x%010llX", final_tx_ts);
    
    double distance = calculate_distance();
    if (distance > 0) {
        LOG_INF("✅ TWR SUCCESS: %.3f m (%.1f cm)", distance, distance * 100.0);
    } else {
        LOG_WRN("⚠️ Distance calculation failed (invalid timestamps)");
    }
    
    return 0;
}

/* ============ TX BEACON TEST MODE ============ */
int uwb_beacon_tx_mode(void) {
    uint32_t status;
    uint32_t beacon_count = 0;
    uint8_t seq = 0;
    
    LOG_INF("╔═══════════════════════════════════════╗");
    LOG_INF("║  TX BEACON: Sending to Anchor...     ║");
    LOG_INF("╚═══════════════════════════════════════╝");
    
    while (1) {
        beacon_count++;
        
        // Simple beacon: FC + SEQ + PAN + "TAG_TX"
        uint8_t beacon[] = {0x41, 0x88, seq++, 0xCA, 0xDE, 'T', 'A', 'G', '_', 'T', 'X'};
        
        dwt_writetxdata(sizeof(beacon), beacon, 0);
        dwt_writetxfctrl(sizeof(beacon) + 2, 0, 0); // +2 FCS, no ranging
        
        if (dwt_starttx(DWT_START_TX_IMMEDIATE) == DWT_SUCCESS) {
            // Wait TX complete
            while (!((status = dwt_read32bitreg(SYS_STATUS_ID)) & SYS_STATUS_TXFRS_BIT_MASK)) {
                k_busy_wait(10);
            }
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
            
            LOG_INF("📡 Beacon #%u sent (Seq: %d)", beacon_count, beacon[2]);
            
            // LED pulse
            extern void uwb_led_pulse(void);
            uwb_led_pulse();
        }
        
        k_msleep(100); // 10 Hz
    }
    
    return 0;
}

/* ============ RX HARDWARE TEST MODE ============ */
int uwb_rx_test_mode(void) {
    uint32_t status;
    uint8_t rx_buffer[128];
    uint16_t frame_len;
    uint32_t rx_count = 0;
    
    LOG_INF("╔═══════════════════════════════════════╗");
    LOG_INF("║  RX TEST: Continuous receive mode    ║");
    LOG_INF("║  Listening for ANY frames...          ║");
    LOG_INF("╚═══════════════════════════════════════╝");
    
    // Enable RX immediately
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    LOG_INF("✅ RX enabled - waiting for frames...");
    
    while (1) {
        status = dwt_read32bitreg(SYS_STATUS_ID);
        
        // Frame received!
        if (status & SYS_STATUS_RXFCG_BIT_MASK) {
            rx_count++;
            frame_len = dwt_read32bitreg(RX_FINFO_ID) & 0x3FF;
            dwt_readrxdata(rx_buffer, (frame_len < 128 ? frame_len : 128), 0);
            
            LOG_INF("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            LOG_INF("🎉 FRAME #%u RECEIVED! (%d bytes)", rx_count, frame_len);
            LOG_HEXDUMP_INF(rx_buffer, (frame_len < 20 ? frame_len : 20), "Data:");
            LOG_INF("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            
            // Clear RX flag and restart
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
        
        // RX errors
        if (status & SYS_STATUS_ALL_RX_ERR) {
            LOG_WRN("⚠️  RX Error: 0x%08X", status);
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
        
        k_msleep(1);
    }
    
    return 0;
}
