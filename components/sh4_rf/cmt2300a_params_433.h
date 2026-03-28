#pragma once
/**
 * cmt2300a_params_433.h
 * CMT2300A register configuration for 433.92 MHz OOK direct mode.
 *
 * Sources:
 *  - Captured from Tuya SH4 firmware via SPI logic analyser by @olivluca
 *    https://github.com/olivluca/tuya_rf
 *  - Cross-validated against tuya/tuya-bsp-gpl-public-components
 *  - Cross-validated against NVS parameters extracted from a Tuya IR+RF
 *    blaster flash dump (CBU/BK7231N + SH4 no-name device)
 *
 * Operating mode: DIRECT (PKT1[1:0] = 0x00)
 *   Data is output/input directly on GPIO pins, not through the FIFO.
 *   TX: bit-bang on GPIO1/DIN pin
 *   RX: demodulated signal directly on GPIO2/DOUT pin
 *
 * Preamble value: 0xAA (alternating 10101010b)
 *
 * NOTE: On boards where SCLK/SDIO are not connected to the host MCU
 * (confirmed on certain Tuya no-name IR+RF blasters), the CMT2300A
 * is pre-configured by Tuya in factory. The host MCU only drives:
 *   GPIO1 (DIN)  - TX data input to radio
 *   GPIO2 (DOUT) - RX demodulated output from radio
 *   CSB          - chip select for register access (RSSI readback only)
 *   FCSB         - chip select for FIFO access
 * In that case, the SPI init banks below are NOT sent at runtime.
 * They are kept here for reference and for boards where SCLK/SDIO
 * ARE connected (e.g. S11, Moes UFO-R2-RF).
 */

/* --- Bank sizes --- */
#define CMT2300A_CMT_BANK_SIZE        12
#define CMT2300A_SYSTEM_BANK_SIZE     12
#define CMT2300A_FREQUENCY_BANK_SIZE   8
#define CMT2300A_DATA_RATE_BANK_SIZE  24
#define CMT2300A_BASEBAND_BANK_SIZE   29
#define CMT2300A_TX_BANK_SIZE         11

/* --- Bank base addresses --- */
#define CMT2300A_CMT_BANK_ADDR        0x00
#define CMT2300A_SYSTEM_BANK_ADDR     0x0C
#define CMT2300A_FREQUENCY_BANK_ADDR  0x18
#define CMT2300A_DATA_RATE_BANK_ADDR  0x20
#define CMT2300A_BASEBAND_BANK_ADDR   0x38
#define CMT2300A_TX_BANK_ADDR         0x55

/* --- Control registers (written individually, not in init banks) --- */
#define CMT2300A_REG_MODE_CTL   0x60  /* Mode control (go_sleep, go_stby, go_rx, go_tx ...) */
#define CMT2300A_REG_MODE_STA   0x61  /* Mode status (current state readback) */
#define CMT2300A_REG_IO_SEL     0x65  /* GPIO function selection */
#define CMT2300A_REG_INT1_CTL   0x66  /* INT1 source selection */
#define CMT2300A_REG_INT2_CTL   0x67  /* INT2 source selection + TX_DIN_INV */
#define CMT2300A_REG_INT_EN     0x68  /* Interrupt enable bits */
#define CMT2300A_REG_FIFO_CTL   0x69  /* FIFO control + TX DIN enable */
#define CMT2300A_REG_INT_CLR1   0x6A  /* Interrupt clear 1 */
#define CMT2300A_REG_INT_CLR2   0x6B  /* Interrupt clear 2 */
#define CMT2300A_REG_FIFO_CLR   0x6C  /* FIFO clear */
#define CMT2300A_REG_RSSI_DBM   0x70  /* RSSI in dBm (read only) */
#define CMT2300A_REG_CMT10      0x09  /* xosc_aac_code[2:0] - post-init correction */
#define CMT2300A_REG_SYS2       0x0D  /* Duty cycle / timer enables - set 0 before RX */
#define CMT2300A_REG_PKT29      0x54  /* FIFO threshold */

/* --- Mode control commands (written to REG_MODE_CTL) --- */
#define CMT2300A_GO_SLEEP       0x10
#define CMT2300A_GO_STBY        0x02
#define CMT2300A_GO_RFS         0x04
#define CMT2300A_GO_RX          0x08
#define CMT2300A_GO_TFS         0x20
#define CMT2300A_GO_TX          0x40

/* --- State values (REG_MODE_STA[3:0]) --- */
#define CMT2300A_STA_SLEEP      0x01
#define CMT2300A_STA_STBY       0x02
#define CMT2300A_STA_RFS        0x03
#define CMT2300A_STA_TFS        0x04
#define CMT2300A_STA_RX         0x05
#define CMT2300A_STA_TX         0x06
#define CMT2300A_MASK_STA       0x0F

/* --- IO_SEL GPIO function values --- */
#define CMT2300A_GPIO1_DOUT     0x00  /* GPIO1 = DOUT/DIN (default) */
#define CMT2300A_GPIO1_INT1     0x01
#define CMT2300A_GPIO1_INT2     0x02
#define CMT2300A_GPIO1_DCLK     0x03
#define CMT2300A_GPIO2_INT1     0x00
#define CMT2300A_GPIO2_INT2     0x04
#define CMT2300A_GPIO2_DOUT     0x08  /* GPIO2 = DOUT/DIN */
#define CMT2300A_GPIO2_DCLK     0x0C
#define CMT2300A_GPIO3_CLKO     0x00
#define CMT2300A_GPIO3_DOUT     0x10
#define CMT2300A_GPIO3_INT2     0x20
#define CMT2300A_GPIO3_DCLK     0x30

/* --- Interrupt source values (for INT1_CTL / INT2_CTL [4:0]) --- */
#define CMT2300A_INT_TX_DONE    0x0A
#define CMT2300A_INT_PKT_OK     0x07
#define CMT2300A_INT_SYNC_OK    0x04

/* --- Interrupt enable mask bits (INT_EN register) --- */
#define CMT2300A_EN_TX_DONE     0x20
#define CMT2300A_EN_PKT_DONE    0x01

/* --- FIFO control bits --- */
#define CMT2300A_TX_DIN_EN      0x80  /* Enable TX DIN input */
#define CMT2300A_TX_DIN_GPIO1   0x00  /* TX DIN on GPIO1 */
#define CMT2300A_TX_DIN_GPIO2   0x20
#define CMT2300A_TX_DIN_GPIO3   0x40
#define CMT2300A_TX_DIN_SEL     0x60  /* TX DIN select mask */
#define CMT2300A_FIFO_MERGE_EN  0x02  /* Merge TX and RX FIFOs into 64 bytes */

/* --- INT2_CTL bits --- */
#define CMT2300A_TX_DIN_INV     0x20  /* Invert TX DIN signal */

/* --- FIFO clear bits --- */
#define CMT2300A_CLR_RX_FIFO    0x02
#define CMT2300A_CLR_TX_FIFO    0x01

/* ================================================================
   Register banks - 433.92 MHz OOK, direct mode
   ================================================================ */

static const uint8_t CMT_BANK_433[CMT2300A_CMT_BANK_SIZE] = {
    /* 0x00 */ 0x00,
    /* 0x01 */ 0x66,  /* product_id: 0x66 = CMT2300A */
    /* 0x02 */ 0xEC,
    /* 0x03 */ 0x1C,
    /* 0x04 */ 0xF0,
    /* 0x05 */ 0x80,
    /* 0x06 */ 0x14,
    /* 0x07 */ 0x08,
    /* 0x08 */ 0x91,
    /* 0x09 */ 0x02,  /* CMT10: xosc_aac_code - overwritten after init */
    /* 0x0A */ 0x02,
    /* 0x0B */ 0xD0,  /* RSSI config */
};

static const uint8_t SYSTEM_BANK_433[CMT2300A_SYSTEM_BANK_SIZE] = {
    /* 0x0C */ 0xAE,  /* SYS1: LNA mode */
    /* 0x0D */ 0xE0,  /* SYS2: duty cycle timers - overwritten to 0x00 before RX */
    /* 0x0E */ 0x35,  /* SYS3: sleep bypass, crystal stabilisation time */
    /* 0x0F */ 0x00,
    /* 0x10 */ 0x00,
    /* 0x11 */ 0xF4,  /* SYS6: RX timer T1[7:0] */
    /* 0x12 */ 0x10,  /* SYS7: RX timer T1[10:8] */
    /* 0x13 */ 0xE2,  /* SYS8: RX timer T2[7:0] */
    /* 0x14 */ 0x42,  /* SYS9: RX timer T2[10:8] */
    /* 0x15 */ 0x20,  /* SYS10: RX extend mode */
    /* 0x16 */ 0x00,
    /* 0x17 */ 0x81,  /* SYS12: clock output */
};

static const uint8_t FREQUENCY_BANK_433[CMT2300A_FREQUENCY_BANK_SIZE] = {
    /* 0x18 */ 0x42,  /* RF1: FREQ_RX_N  */
    /* 0x19 */ 0x71,  /* RF2: FREQ_RX_K[7:0]   -> 433.92 MHz RX */
    /* 0x1A */ 0xCE,  /* RF3: FREQ_RX_K[15:8]  */
    /* 0x1B */ 0x1C,  /* RF4: FREQ_RX_K[19:16] | DIVX */
    /* 0x1C */ 0x42,  /* RF5: FREQ_TX_N  */
    /* 0x1D */ 0x5B,  /* RF6: FREQ_TX_K[7:0]   -> 433.92 MHz TX */
    /* 0x1E */ 0x1C,  /* RF7: FREQ_TX_K[15:8]  */
    /* 0x1F */ 0x1C,  /* RF8: FREQ_TX_K[19:16] | VCO_BANK */
};

static const uint8_t DATA_RATE_BANK_433[CMT2300A_DATA_RATE_BANK_SIZE] = {
    /* 0x20 */ 0x32,  /* RF9:  DR_M[7:0]  */
    /* 0x21 */ 0x18,  /* RF10: DR_M[15:8] */
    /* 0x22 */ 0x80,  /* RF11: DR_M[23:16] */
    /* 0x23 */ 0xDD,  /* RF12: DR_E - OOK data rate ~4 kbps */
    /* 0x24 */ 0x00,  /* FSK1 - unused in OOK mode */
    /* 0x25 */ 0x00,
    /* 0x26 */ 0x00,
    /* 0x27 */ 0x00,
    /* 0x28 */ 0x00,
    /* 0x29 */ 0x00,
    /* 0x2A */ 0x00,
    /* 0x2B */ 0x29,  /* CDR1: clock data recovery */
    /* 0x2C */ 0xC0,  /* CDR2 */
    /* 0x2D */ 0x51,  /* CDR3 */
    /* 0x2E */ 0x2A,  /* CDR4 */
    /* 0x2F */ 0x4B,  /* AGC1: automatic gain control */
    /* 0x30 */ 0x05,  /* AGC2 */
    /* 0x31 */ 0x00,  /* AGC3 */
    /* 0x32 */ 0x50,  /* AGC4 */
    /* 0x33 */ 0x2D,  /* OOK1: OOK demodulator settings */
    /* 0x34 */ 0x00,  /* OOK2 */
    /* 0x35 */ 0x01,  /* OOK3 */
    /* 0x36 */ 0x05,  /* OOK4 */
    /* 0x37 */ 0x05,  /* OOK5 */
};

static const uint8_t BASEBAND_BANK_433[CMT2300A_BASEBAND_BANK_SIZE] = {
    /* 0x38 */ 0x10,  /* PKT1: DATA_MODE=DIRECT(0b00), PREAM_UNIT=8-bit, RX_PREAM_SIZE=2 */
    /* 0x39 */ 0x08,  /* PKT2: TX_PREAM_SIZE[7:0] = 8 preamble bytes */
    /* 0x3A */ 0x00,  /* PKT3: TX_PREAM_SIZE[15:8] */
    /* 0x3B */ 0xAA,  /* PKT4: PREAM_VALUE = 0xAA (10101010b alternating) */
    /* 0x3C */ 0x02,  /* PKT5: SYNC_SIZE=1 (2-byte sync word) */
    /* 0x3D */ 0x00,  /* PKT6:  SYNC_VALUE[7:0]  */
    /* 0x3E */ 0x00,  /* PKT7:  SYNC_VALUE[15:8] */
    /* 0x3F */ 0x00,  /* PKT8:  SYNC_VALUE[23:16] */
    /* 0x40 */ 0x00,  /* PKT9:  SYNC_VALUE[31:24] */
    /* 0x41 */ 0x00,  /* PKT10: SYNC_VALUE[39:32] */
    /* 0x42 */ 0x00,  /* PKT11: SYNC_VALUE[47:40] */
    /* 0x43 */ 0xD4,  /* PKT12: SYNC_VALUE[55:48] */
    /* 0x44 */ 0x2D,  /* PKT13: SYNC_VALUE[63:56] */
    /* 0x45 */ 0x00,  /* PKT14: PAYLOAD_LEN[10:8], PKT_TYPE=FIXED */
    /* 0x46 */ 0x1F,  /* PKT15: PAYLOAD_LEN[7:0] = 31 bytes */
    /* 0x47 */ 0x00,  /* PKT16: node detection disabled */
    /* 0x48 */ 0x00,  /* PKT17-20: node value (unused) */
    /* 0x49 */ 0x00,
    /* 0x4A */ 0x00,
    /* 0x4B */ 0x00,
    /* 0x4C */ 0x00,  /* PKT21: FEC=off, CRC=off */
    /* 0x4D */ 0x00,  /* PKT22: CRC seed[7:0] */
    /* 0x4E */ 0x00,  /* PKT23: CRC seed[15:8] */
    /* 0x4F */ 0x60,  /* PKT24: whitening disabled, Manchester disabled */
    /* 0x50 */ 0xFF,  /* PKT25: whitening seed */
    /* 0x51 */ 0x00,  /* PKT26: TX prefix type */
    /* 0x52 */ 0x00,  /* PKT27: TX packet repeat count */
    /* 0x53 */ 0x1F,  /* PKT28: TX inter-packet gap */
    /* 0x54 */ 0x10,  /* PKT29: FIFO threshold = 16 bytes */
};

static const uint8_t TX_BANK_433[CMT2300A_TX_BANK_SIZE] = {
    /* 0x55 */ 0x55,  /* TX1 */
    /* 0x56 */ 0x26,  /* TX2 */
    /* 0x57 */ 0x03,  /* TX3 */
    /* 0x58 */ 0x00,  /* TX4 */
    /* 0x59 */ 0x0F,  /* TX5: PA control */
    /* 0x5A */ 0xB0,  /* TX6 */
    /* 0x5B */ 0x00,  /* TX7 */
    /* 0x5C */ 0x37,  /* TX8 */
    /* 0x5D */ 0x0A,  /* TX9 */
    /* 0x5E */ 0x3F,  /* TX10: PA power (maximum) */
    /* 0x5F */ 0x7F,  /* LBD: low battery detection threshold */
};
