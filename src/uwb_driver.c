#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>

LOG_MODULE_REGISTER(uwb_driver, CONFIG_LOG_DEFAULT_LEVEL);

/* TAG Configuration */
#define TAG_ID              0x0001      // Unique tag ID
#define PAN_ID              0xDECA      // PAN ID (Decawave default)
#define ANCHOR_ADDR         0xFFFF      // Broadcast or specific anchor address

/* IEEE 802.15.4 Frame Control Field */
#define FRAME_TYPE_DATA     0x01        // Data frame
#define SEC_ENABLED         0x00        // Security disabled
#define FRAME_PENDING       0x00        // No pending frame
#define ACK_REQUEST         0x00        // No ACK requested
#define PAN_ID_COMPRESS     0x01        // PAN ID compression enabled
#define DEST_ADDR_MODE_SHORT 0x02       // 16-bit short address
#define SRC_ADDR_MODE_SHORT  0x02       // 16-bit short address

/* Frame Control: 0x4188
 * Bits 0-2:   Frame Type = 001 (Data)
 * Bit 3:      Security Enabled = 0
 * Bit 4:      Frame Pending = 0
 * Bit 5:      ACK Request = 0
 * Bit 6:      PAN ID Compression = 1
 * Bits 10-11: Dest Addr Mode = 10 (16-bit)
 * Bits 14-15: Src Addr Mode = 10 (16-bit)
 */
#define FRAME_CTRL_LSB      0x41        // Frame control byte 0
#define FRAME_CTRL_MSB      0x88        // Frame control byte 1

/* TWR Message Types */
#define MSG_TYPE_POLL       0x61        // TWR Poll message
#define MSG_TYPE_RESP       0x50        // TWR Response message  
#define MSG_TYPE_FINAL      0x23        // TWR Final message
#define MSG_TYPE_BEACON     0x70        // Beacon/discovery message

/* IEEE 802.15.4 Data Frame Structure
 * +----------------+----------------+----------------+
 * | Frame Control  | Sequence Num   | PAN ID         |
 * | (2 bytes)      | (1 byte)       | (2 bytes)      |
 * +----------------+----------------+----------------+
 * | Dest Address   | Src Address    | Payload        |
 * | (2 bytes)      | (2 bytes)      | (n bytes)      |
 * +----------------+----------------+----------------+
 */
typedef struct {
    uint8_t frame_ctrl[2];      // Frame control field
    uint8_t seq_num;            // Sequence number
    uint8_t pan_id[2];          // PAN ID
    uint8_t dest_addr[2];       // Destination address
    uint8_t src_addr[2];        // Source address (TAG_ID)
    uint8_t msg_type;           // Message type (POLL/RESP/FINAL/BEACON)
    uint8_t payload[32];        // Payload data
} __packed uwb_data_frame_t;

static uint8_t frame_seq_num = 0;

/**
 * Build IEEE 802.15.4 DATA frame for TWR
 * @param frame     Pointer to frame structure
 * @param msg_type  Message type (POLL/BEACON/etc)
 * @param payload   Optional payload data
 * @param payload_len Length of payload
 * @return Total frame length
 */
static int build_ieee802154_frame(uwb_data_frame_t *frame, uint8_t msg_type,
                                   const uint8_t *payload, uint8_t payload_len)
{
    if (!frame) {
        return -1;
    }

    memset(frame, 0, sizeof(uwb_data_frame_t));

    // Frame Control: 0x4188 (Data frame, short addressing)
    frame->frame_ctrl[0] = FRAME_CTRL_LSB;  // 0x41
    frame->frame_ctrl[1] = FRAME_CTRL_MSB;  // 0x88

    // Sequence number (increments with each transmission)
    frame->seq_num = frame_seq_num++;

    // PAN ID: 0xDECA (little-endian)
    frame->pan_id[0] = (PAN_ID & 0xFF);         // 0xCA
    frame->pan_id[1] = (PAN_ID >> 8) & 0xFF;    // 0xDE

    // Destination address (broadcast or specific anchor)
    frame->dest_addr[0] = (ANCHOR_ADDR & 0xFF);        // 0xFF
    frame->dest_addr[1] = (ANCHOR_ADDR >> 8) & 0xFF;   // 0xFF

    // Source address (TAG_ID)
    frame->src_addr[0] = (TAG_ID & 0xFF);       // 0x01
    frame->src_addr[1] = (TAG_ID >> 8) & 0xFF;  // 0x00

    // Message type
    frame->msg_type = msg_type;

    // Copy payload if provided
    if (payload && payload_len > 0) {
        uint8_t copy_len = (payload_len > 32) ? 32 : payload_len;
        memcpy(frame->payload, payload, copy_len);
        return 9 + 1 + copy_len;  // Header (9) + msg_type (1) + payload
    }

    return 10;  // Header (9) + msg_type (1), no payload
}

/**
 * Initialize UWB driver and DW3210 transceiver
 * @return 0 on success, negative error code on failure
 */
int uwb_driver_init(void)
{
    printk("===========================================\n");
    printk("UWB Driver - IEEE 802.15.4 Mode\n");
    printk("===========================================\n");
    printk("TAG ID: 0x%04X\n", TAG_ID);
    printk("PAN ID: 0x%04X\n", PAN_ID);
    printk("Frame Type: IEEE 802.15.4 DATA (0x4188)\n");
    printk("===========================================\n");
    
    printk("UWB Driver initialized - IEEE 802.15.4 mode\n");
    printk("TAG ID: 0x%04X, PAN ID: 0x%04X\n", TAG_ID, PAN_ID);
    
    /* TEST: SPI3 Device Check */
    printk("\n*** SPI3 TEST ***\n");
    const struct device *spi3 = DEVICE_DT_GET(DT_NODELABEL(spi3));
    if (!device_is_ready(spi3)) {
        printk("ERROR: SPI3 device not ready!\n");
        return -1;
    }
    printk("SUCCESS: SPI3 device ready!\n");
    
    /* TEST: Read DW3110 Device ID via SPI */
    printk("\n*** DW3110 DEVICE ID TEST ***\n");
    
    // Configure SPI with CS pin from Device Tree
    struct spi_config spi_cfg = {
        .frequency = 2000000,  // 2 MHz
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = {
            .gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(spi3), cs_gpios),
            .delay = 0,
        },
    };
    
    // DW3110 Register read: Address 0x00 (Device ID), length 4 bytes
    // Header: 0x00 (Read) | 0x00 (Sub-address 0)
    uint8_t tx_buf[5] = {0x00, 0x00, 0x00, 0x00, 0x00};  
    uint8_t rx_buf[5] = {0};
    
    struct spi_buf tx = {.buf = tx_buf, .len = 5};
    struct spi_buf rx = {.buf = rx_buf, .len = 5};
    struct spi_buf_set tx_bufs = {.buffers = &tx, .count = 1};
    struct spi_buf_set rx_bufs = {.buffers = &rx, .count = 1};
    
    int ret = spi_transceive(spi3, &spi_cfg, &tx_bufs, &rx_bufs);
    if (ret != 0) {
        printk("ERROR: SPI transceive failed: %d\n", ret);
        return -1;
    }
    
    // Print raw bytes for debugging
    printk("RX Bytes: %02X %02X %02X %02X %02X\n", 
           rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4]);

    uint32_t dev_id = (rx_buf[4] << 24) | (rx_buf[3] << 16) | (rx_buf[2] << 8) | rx_buf[1];
    printk("Device ID: 0x%08X\n", dev_id);
    
    if (dev_id == 0xDECA0302) {
        printk("SUCCESS: DW3110 detected!\n");
    } else {
        printk("WARNING: Unexpected Device ID (expected 0xDECA0302)\n");
    }
    
    return 0;
}

/**
 * Send IEEE 802.15.4 DATA frame (replaces BLINK)
 * This sends a BEACON/POLL frame that anchors can see
 * @return 0 on success, negative error code on failure
 */
int uwb_send_blink(void)
{
    uwb_data_frame_t frame;
    uint8_t tag_info[8];
    int frame_len;

    // Prepare tag info payload
    tag_info[0] = (TAG_ID & 0xFF);
    tag_info[1] = (TAG_ID >> 8) & 0xFF;
    tag_info[2] = 0x01;  // Tag version
    tag_info[3] = 0x00;  // Status flags
    tag_info[4] = frame_seq_num;  // Sequence number
    tag_info[5] = 0x00;  // Reserved
    tag_info[6] = 0x00;  // Reserved
    tag_info[7] = 0x00;  // Reserved

    // Build IEEE 802.15.4 frame with BEACON message type
    frame_len = build_ieee802154_frame(&frame, MSG_TYPE_BEACON, tag_info, 8);

    if (frame_len <= 0) {
        LOG_ERR("Failed to build frame");
        return -1;
    }

    // Log frame details
    printk("*** IEEE 802.15.4 DATA FRAME ***\n");
    printk("  Frame Ctrl: 0x%02X%02X (Data frame)\n", 
            frame.frame_ctrl[1], frame.frame_ctrl[0]);
    printk("  Seq Num:    0x%02X\n", frame.seq_num);
    printk("  PAN ID:     0x%02X%02X\n", frame.pan_id[1], frame.pan_id[0]);
    printk("  Dest Addr:  0x%02X%02X\n", frame.dest_addr[1], frame.dest_addr[0]);
    printk("  Src Addr:   0x%04X (TAG)\n", TAG_ID);
    printk("  Msg Type:   0x%02X (BEACON)\n", frame.msg_type);
    printk("  Frame Len:  %d bytes\n", frame_len);
    
    printk("TX: IEEE 802.15.4 frame [Seq:%d, Len:%d]\n", frame.seq_num, frame_len);

    // TODO: Actual SPI transmission to DW3210 will go here
    // For now, simulate transmission delay
    k_msleep(10);

    return 0;
}

/**
 * Send TWR POLL message (initiates ranging)
 * @return 0 on success, negative error code on failure
 */
int uwb_send_twr_poll(void)
{
    uwb_data_frame_t frame;
    int frame_len;

    // Build TWR POLL frame (no payload needed)
    frame_len = build_ieee802154_frame(&frame, MSG_TYPE_POLL, NULL, 0);

    if (frame_len <= 0) {
        return -1;
    }

    LOG_INF("*** TWR POLL FRAME ***");
    LOG_INF("  Seq Num:   0x%02X", frame.seq_num);
    LOG_INF("  TAG -> ANCHOR: Poll request");
    
    printk("TX: TWR POLL [Seq:%d]\n", frame.seq_num);

    // TODO: Send via SPI to DW3210
    k_msleep(10);

    return 0;
}
