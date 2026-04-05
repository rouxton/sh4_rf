/**
 * sh4_rf.cpp
 * ESPHome component for Tuya SH4 RF module (CMT2300A transceiver).
 *
 * Implements:
 *   - CMT2300A SPI bit-bang (3-wire, bidirectional SDIO, ~500 kHz)
 *   - 433.92 MHz OOK, direct GPIO mode and FIFO packet mode
 *   - TX: bit-bang DIN pin with busy-wait microsecond timing
 *   - RX direct: GPIO edge ISR → microsecond ring buffer → remote_base
 *   - RX FIFO: CMT2300A packet handler → SPI FIFO read → remote_base
 *   - Runtime switch between direct and FIFO receive modes
 *
 * Hardware variants:
 *   Variant A (SCLK/SDIO connected): full SPI init at boot, spi_enabled=true
 *   Variant B (SCLK/SDIO not connected): skip SPI init, spi_enabled=false
 */

#include "sh4_rf.h"
#include "cmt2300a_params_433.h"

/* -----------------------------------------------------------------------
   BK7231N direct GPIO register access
   From firmware reverse engineering of gpio_write at 0x5ff58:
     reg_addr = (pin + 0x200a00) << 2
     *reg = (*reg & ~0x02) | ((value & 1) << 1)
   Bit1 = output value, Bit0 = direction (0=output, 1=input)
   GPIO peripheral base: 0x802800 = (0x200a00 << 2)
   ----------------------------------------------------------------------- */
/* BK7231N GPIO via ROM gpio_init (0x15894) and gpio_write app fn (0x5ff58)
 * gpio_init(pin, mode): mode 1=OUTPUT_LOW, 2=OUTPUT_HIGH, 0=INPUT_FLOAT
 * gpio_write(pin, val): writes bit1 of (pin+0x200a00)<<2
 * Both called via inline asm to avoid function pointer issues */
static inline void bk_gpio_init_asm(uint8_t pin, uint8_t mode) {
  __asm__ volatile(
    "mov r0, %0
"
    "mov r1, %1
"
    "bl 0x15894
"
    : : "r"((uint32_t)pin), "r"((uint32_t)mode) : "r0","r1","r2","r3","lr"
  );
}
#define BK_GPIO_REG(p)   (*((volatile uint32_t *)(((uint32_t)(p) + 0x200a00u) << 2)))
#define BK_GPIO_HIGH(p)  do { bk_gpio_init_asm((p), 2); } while(0)  /* OUTPUT_HIGH */
#define BK_GPIO_LOW(p)   do { bk_gpio_init_asm((p), 1); } while(0)  /* OUTPUT_LOW  */
#define BK_GPIO_OUT(p)   do { bk_gpio_init_asm((p), 1); } while(0)  /* OUTPUT_LOW  */
#define BK_GPIO_IN(p)    do { bk_gpio_init_asm((p), 0); } while(0)  /* INPUT       */

#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace sh4_rf {

static const char *const TAG = "sh4_rf";

/* =======================================================================
   SPI bit-bang implementation
   The CMT2300A uses a non-standard 3-wire SPI with a bidirectional SDIO:
     Write: CSB↓ | addr[6:0]+W=0 (8 bits) | data (8 bits) | CSB↑
     Read:  CSB↓ | addr[6:0]+R=1 (8 bits) | SDIO→input | data (8 bits) | CSB↑
   Clock polarity: CPOL=0, CPHA=0. Data sampled on rising SCLK edge.
   Max clock: 5 MHz; we run at ~500 kHz (bit-bang on BK7231N GPIO).
   ======================================================================= */

static inline void pin_hi(InternalGPIOPin *p)  { p->digital_write(true);  }
static inline void pin_lo(InternalGPIOPin *p)  { p->digital_write(false); }

/** Send one byte MSB-first on SDIO (SDIO must be configured as output). */
static void spi_send_byte(InternalGPIOPin *sclk, InternalGPIOPin *sdio, uint8_t byte) {
  /* CPOL=0 CPHA=0: set data on falling SCLK, clock high, repeat */
  for (int i = 7; i >= 0; i--) {
    pin_lo(sclk);
    if (byte & (1u << i)) pin_hi(sdio); else pin_lo(sdio);
    delayMicroseconds(1);
    pin_hi(sclk);
    delayMicroseconds(1);
  }
  pin_lo(sclk);
}

/** Receive one byte MSB-first from SDIO (SDIO must be configured as input).
 *  Tuya firmware: SCLK_LOW, SCLK_HIGH, then read bit (sample on rising edge). */
static uint8_t spi_recv_byte(InternalGPIOPin *sclk, InternalGPIOPin *sdio) {
  uint8_t val = 0;
  for (int i = 7; i >= 0; i--) {
    pin_lo(sclk);
    delayMicroseconds(1);
    pin_hi(sclk);
    delayMicroseconds(1);
    /* Sample AFTER rising edge - matches Tuya firmware recv_byte */
    if (sdio->digital_read()) val |= (1u << i);
  }
  pin_lo(sclk);
  return val;
}

void SH4RfComponent::spi_write_reg(uint8_t addr, uint8_t data) {
  /* Tuya firmware sequence:
     SDIO=HIGH, SCLK=OUTPUT, SCLK=LOW, FCSB=HIGH, CSB=LOW,
     send(addr & 0x7F), send(data), SCLK=LOW, CSB=HIGH,
     SDIO=HIGH, SDIO=INPUT, FCSB=HIGH */
  pin_hi(sdio_);
  sdio_->pin_mode(gpio::FLAG_OUTPUT);
  pin_lo(sclk_);
  pin_hi(fcsb_);
  pin_lo(csb_);
  spi_send_byte(sclk_, sdio_, addr & 0x7F);  /* W: bit7=0 */
  spi_send_byte(sclk_, sdio_, data);
  pin_lo(sclk_);
  pin_hi(csb_);
  pin_hi(sdio_);
  sdio_->pin_mode(gpio::FLAG_INPUT);
  pin_hi(fcsb_);
}

uint8_t SH4RfComponent::spi_read_reg(uint8_t addr) {
  /* Tuya firmware sequence:
     SDIO=HIGH, SCLK=OUTPUT, SCLK=LOW, FCSB=HIGH, CSB=LOW,
     send(addr | 0x80), SDIO=INPUT, recv(), SCLK=LOW, CSB=HIGH,
     SDIO=INPUT, FCSB=HIGH */
  pin_hi(sdio_);
  sdio_->pin_mode(gpio::FLAG_OUTPUT);
  pin_lo(sclk_);
  pin_hi(fcsb_);
  pin_lo(csb_);
  spi_send_byte(sclk_, sdio_, addr | 0x80);  /* R: bit7=1 */
  /* Switch to input before clock cycles for data */
  sdio_->pin_mode(gpio::FLAG_INPUT);
  uint8_t val = spi_recv_byte(sclk_, sdio_);
  pin_lo(sclk_);
  pin_hi(csb_);
  sdio_->pin_mode(gpio::FLAG_INPUT);
  pin_hi(fcsb_);
  return val;
}

void SH4RfComponent::spi_write_bank(uint8_t base_addr, const uint8_t *bank, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    spi_write_reg(base_addr + i, bank[i]);
  }
}

/* =======================================================================
   CMT2300A state machine helpers
   ======================================================================= */

static constexpr uint32_t STATE_TIMEOUT_MS = 10;

bool SH4RfComponent::wait_state_(uint8_t expected) {
  uint32_t t0 = millis();
  while (millis() - t0 < STATE_TIMEOUT_MS) {
    if ((spi_read_reg(CMT2300A_REG_MODE_STA) & CMT2300A_MASK_STA) == expected)
      return true;
    App.feed_wdt();
  }
  return false;
}

bool SH4RfComponent::go_state_(uint8_t cmd, uint8_t expected) {
  spi_write_reg(CMT2300A_REG_MODE_CTL, cmd);
  return wait_state_(expected);
}

/* =======================================================================
   CMT2300A initialisation (Variant A: SCLK/SDIO connected)
   ======================================================================= */

bool SH4RfComponent::cmt_init() {
  ESP_LOGE(TAG, "cmt_init() called spi_enabled=%d sclk=%p sdio=%p", spi_enabled_, sclk_, sdio_);
  /* Ensure CSB is high before starting */
  pin_hi(csb_);
  pin_lo(sclk_);
  delay(5);

  /* Soft reset */
  spi_write_reg(0x7F, 0xFF);
  delay(20);  /* give CMT2300A time to reset */

  /* Verify presence: product_id register (0x01) must read 0x66 for CMT2300A */
  uint8_t pid = spi_read_reg(0x01);
  cmt_product_id_ = pid;
  ESP_LOGE(TAG, "CMT2300A product_id=0x%02X (expect 0x66)", pid);

  if (pid != 0x66) {
    /* Try once more after longer delay - CMT2300A may need more time after POR */
    delay(50);
    pid = spi_read_reg(0x01);
    ESP_LOGE(TAG, "CMT2300A product_id retry=0x%02X", pid);
  }

  if (pid != 0x66) {
    ESP_LOGE(TAG, "CMT2300A not found! product_id=0x%02X (expected 0x66)", pid);
    ESP_LOGE(TAG, "  Check: SCLK=P14, SDIO=P16, CSB=P6");
    return false;
  }

  ESP_LOGE(TAG, "CMT2300A found OK");

  /* Load all 6 configuration banks */
  spi_write_bank(CMT2300A_CMT_BANK_ADDR,       CMT_BANK_433,       CMT2300A_CMT_BANK_SIZE);
  spi_write_bank(CMT2300A_SYSTEM_BANK_ADDR,    SYSTEM_BANK_433,    CMT2300A_SYSTEM_BANK_SIZE);
  spi_write_bank(CMT2300A_FREQUENCY_BANK_ADDR, FREQUENCY_BANK_433, CMT2300A_FREQUENCY_BANK_SIZE);
  spi_write_bank(CMT2300A_DATA_RATE_BANK_ADDR, DATA_RATE_BANK_433, CMT2300A_DATA_RATE_BANK_SIZE);
  spi_write_bank(CMT2300A_BASEBAND_BANK_ADDR,  BASEBAND_BANK_433,  CMT2300A_BASEBAND_BANK_SIZE);
  spi_write_bank(CMT2300A_TX_BANK_ADDR,        TX_BANK_433,        CMT2300A_TX_BANK_SIZE);

  /*
   * Mandatory post-init correction from Tuya BSP (radio.c):
   * Set xosc_aac_code[2:0] = 2 while preserving other bits.
   */
  uint8_t tmp = spi_read_reg(CMT2300A_REG_CMT10) & ~0x07u;
  spi_write_reg(CMT2300A_REG_CMT10, tmp | 0x02u);

  return true;
}

/* =======================================================================
   TX mode: configure CMT2300A to transmit, drive DIN from host GPIO
   GPIO1 = DIN (modulation input from CBU)
   GPIO2 = INT2 (not used during TX)
   GPIO3 = DOUT (not relevant during TX)
   ======================================================================= */

bool SH4RfComponent::start_tx() {
  if (spi_enabled_) {
    if (!initialized_) {
      if (!cmt_init()) return false;
      initialized_ = true;
    }

    /*
     * Exact StartTx() sequence from Tuya firmware disassembly:
     *
     * 1. ConfigGpio(IO_SEL=0x0A)   GPIO1=DCLK, GPIO2=INT1
     * 2. WriteReg(INT_EN=0x68, 0x3D)
     * 3. GoSleep
     * 4. EnableTxDin(true)         WriteReg(0x62, val|0x20)  enable DIN input
     * 5. EnableTxDinInvert(true)   WriteReg(0x69, val|0x02)  invert DIN
     * 6. GoSleep (again)
     * Note: GoTx is NOT called here - the CBU drives DIN directly
     *       and the CMT2300A enters TX mode automatically when DIN is asserted
     */

    /* Step 1: IO_SEL = 0x0A */
    uint8_t io = spi_read_reg(CMT2300A_REG_IO_SEL);
    io = (io & ~0x1Fu) | 0x0Au;
    spi_write_reg(CMT2300A_REG_IO_SEL, io);

    /* Step 2: INT_EN = 0x3D */
    spi_write_reg(CMT2300A_REG_INT_EN, 0x3D);

    /* Step 3: GoSleep */
    spi_write_reg(CMT2300A_REG_MODE_CTL, CMT2300A_GO_SLEEP);
    delay(2);

    /* Step 4: EnableTxDin(true) - set bit5 of reg 0x62 */
    uint8_t r62 = spi_read_reg(0x62);
    spi_write_reg(0x62, r62 | 0x20u);

    /* Step 5: EnableTxDinInvert(true) - set bit1 of FIFO_CTL(0x69) */
    uint8_t fifo = spi_read_reg(CMT2300A_REG_FIFO_CTL);
    spi_write_reg(CMT2300A_REG_FIFO_CTL, fifo | 0x02u);

    /* Step 6: GoSleep again */
    spi_write_reg(CMT2300A_REG_MODE_CTL, CMT2300A_GO_SLEEP);
    delay(2);

    /* Step 7: GoStby + GoTx (no wait - Tuya firmware doesn't poll state) */
    spi_write_reg(CMT2300A_REG_MODE_CTL, CMT2300A_GO_STBY);
    delay(2);
    spi_write_reg(CMT2300A_REG_MODE_CTL, CMT2300A_GO_TX);
    delay(2);

    ESP_LOGD(TAG, "CMT2300A TX mode ready");
  }
  /* Detach ISR before switching TX pin to OUTPUT */
  this->RemoteReceiverBase::pin_->detach_interrupt();
  high_freq_.stop();
  /* Diagnostic: lecture/écriture directe registre GPIO P22 */
  volatile uint32_t *reg22 = (volatile uint32_t *)(((uint32_t)(tx_pin_num_) + 0x200a00u) << 2);
  uint32_t before = *reg22;
  ESP_LOGE(TAG, "P22 before=0x%08x addr=0x%08x", before, (uint32_t)reg22);
  /* Tenter écriture bit3=1 (OUT HIGH) et bit2=1 (OE) */
  *reg22 = before | 0x0Cu;
  uint32_t after = *reg22;
  ESP_LOGE(TAG, "P22 after write 0x%08x = 0x%08x", before | 0x0Cu, after);
  /* Test: écriture à 0xFF */
  *reg22 = 0xFFu;
  ESP_LOGE(TAG, "P22 after write 0xFF = 0x%08x", *reg22);
  *reg22 = before;  /* restaurer */
  delay(10);
  return true;
}

/* =======================================================================
   RX mode setup
   ======================================================================= */

bool SH4RfComponent::start_rx() {
  if (spi_enabled_) {
    if (!initialized_) {
      if (!cmt_init()) return false;
      initialized_ = true;
    }

    if (rx_mode_ == RxMode::DIRECT) {
      /*
       * Direct mode RX — exact sequence from Tuya firmware StartRx() disassembly:
       *
       * 1. ConfigInterrupt(INT1=SYNC_OK|SL_TMO)  WriteReg(0x65, 0x15)
       * 2. ConfigGpio(IO_SEL=0x0A)               GPIO1=DCLK, GPIO2=DOUT
       * 3. WriteReg(PKT29=0x21)                  FIFO threshold
       * 4. GoSleep
       * 5. EnableTxDinInvert(true)               WriteReg(0x67, val|0x20)
       * 6. WriteReg(FIFO_CTL, val & 0xFA)        disable FIFO merge
       * 7. GoSleep + GoStby
       * 8. ConfigInterrupt(0x2B)                 WriteReg(0x65, 0x2B)
       * 9. ConfigGpio(IO_SEL=0x0C)               GPIO1=DCLK, GPIO2=DOUT, GPIO3=INT2
       * 10. WriteReg(FIFO_CTL, val & 0xFA)       clear FIFO merge bit
       * 11. ClearInterruptFlags                  WriteReg(0x6A, 0x3F) + WriteReg(0x6B, 0x3F)
       * 12. ClearRxFifo                          WriteReg(0x6C, 0x02)
       * 13. GoRx
       */

      /* Step 1: INT1_CTL = 0x15 (SYNC_OK | SL_TMO) */
      spi_write_reg(CMT2300A_REG_INT1_CTL, 0x15);

      /* Step 2: IO_SEL = 0x0A: GPIO1=DCLK(0x03), GPIO2=DOUT(0x08) */
      uint8_t io = spi_read_reg(CMT2300A_REG_IO_SEL);
      io = (io & ~0x1Fu) | 0x0Au;
      spi_write_reg(CMT2300A_REG_IO_SEL, io);

      /* Step 3: PKT29 = 0x21 */
      spi_write_reg(CMT2300A_REG_PKT29, 0x21);

      /* Step 4: GoSleep */
      spi_write_reg(CMT2300A_REG_MODE_CTL, CMT2300A_GO_SLEEP);
      delay(2);

      /* Step 5: EnableTxDinInvert(true) — set bit5 of INT2_CTL(0x67) */
      uint8_t int2 = spi_read_reg(CMT2300A_REG_INT2_CTL);
      spi_write_reg(CMT2300A_REG_INT2_CTL, int2 | 0x20u);

      /* Step 6: FIFO_CTL(0x69) & 0xFA — clear bit1 (FIFO_MERGE_EN=0) */
      uint8_t fifo = spi_read_reg(CMT2300A_REG_FIFO_CTL);
      spi_write_reg(CMT2300A_REG_FIFO_CTL, fifo & 0xFAu);

      /* Step 7: GoSleep + GoStby */
      spi_write_reg(CMT2300A_REG_MODE_CTL, CMT2300A_GO_SLEEP);
      delay(2);
      spi_write_reg(CMT2300A_REG_MODE_CTL, CMT2300A_GO_STBY);
      delay(2);

      /* Step 8: INT1_CTL = 0x2B */
      spi_write_reg(CMT2300A_REG_INT1_CTL, 0x2B);

      /* Step 9: IO_SEL = 0x0C */
      io = spi_read_reg(CMT2300A_REG_IO_SEL);
      io = (io & ~0x1Fu) | 0x0Cu;
      spi_write_reg(CMT2300A_REG_IO_SEL, io);

      /* Step 10: FIFO_CTL & 0xFA again */
      fifo = spi_read_reg(CMT2300A_REG_FIFO_CTL);
      spi_write_reg(CMT2300A_REG_FIFO_CTL, fifo & 0xFAu);

      /* Step 11: ClearInterruptFlags */
      spi_write_reg(CMT2300A_REG_INT_CLR1, 0x3F);
      spi_write_reg(CMT2300A_REG_INT_CLR2, 0x3F);

      /* Step 12: ClearRxFifo */
      spi_write_reg(CMT2300A_REG_FIFO_CLR, 0x02u);

      /* Step 13: GoRx */
      spi_write_reg(CMT2300A_REG_MODE_CTL, CMT2300A_GO_RX);
      delay(2);

      uint8_t rssi = spi_read_reg(CMT2300A_REG_RSSI_DBM);
      ESP_LOGD(TAG, "CMT2300A RX mode (direct), RSSI=%d dBm", (int8_t)rssi);

    } else {
      /* FIFO mode - keep existing implementation */
      spi_write_reg(CMT2300A_REG_IO_SEL,
                    CMT2300A_GPIO1_INT1 | CMT2300A_GPIO2_INT2 | CMT2300A_GPIO3_DOUT);
      spi_write_reg(CMT2300A_REG_INT1_CTL, 0x0C);
      spi_write_reg(CMT2300A_REG_INT2_CTL, CMT2300A_INT_PKT_OK);
      spi_write_reg(CMT2300A_REG_INT_EN,   CMT2300A_EN_TX_DONE | CMT2300A_EN_PKT_DONE);
      spi_write_reg(CMT2300A_REG_SYS2, 0x00);
      spi_write_reg(CMT2300A_REG_FIFO_CTL,
                    spi_read_reg(CMT2300A_REG_FIFO_CTL) | CMT2300A_FIFO_MERGE_EN);
      spi_write_reg(CMT2300A_REG_PKT29, 0x20);
      if (!go_state_(CMT2300A_GO_SLEEP, CMT2300A_STA_SLEEP)) return false;
      if (!go_state_(CMT2300A_GO_STBY,  CMT2300A_STA_STBY))  return false;
      spi_write_reg(CMT2300A_REG_FIFO_CLR,  CMT2300A_CLR_RX_FIFO | CMT2300A_CLR_TX_FIFO);
      spi_write_reg(CMT2300A_REG_INT_CLR1,  0x3F);
      spi_write_reg(CMT2300A_REG_INT_CLR2,  0x3F);
      if (!go_state_(CMT2300A_GO_RX, CMT2300A_STA_RX)) {
        ESP_LOGE(TAG, "RX FIFO: cannot reach RX state");
        return false;
      }
    }
  }
  /* Switch P20 back to INPUT, then reattach ISR */
  this->RemoteReceiverBase::pin_->pin_mode(gpio::FLAG_INPUT);
  store_.buffer_write_at = store_.buffer_read_at =
      this->RemoteReceiverBase::pin_->digital_read() ? 0 : 1;
  store_.overflow = false;
  this->RemoteReceiverBase::pin_->attach_interrupt(
      SH4RfReceiverStore::gpio_intr, &store_, gpio::INTERRUPT_ANY_EDGE);
  high_freq_.start();
  return true;
}

void SH4RfComponent::go_standby() {
  if (spi_enabled_) {
    spi_write_reg(CMT2300A_REG_MODE_CTL, CMT2300A_GO_STBY);
    wait_state_(CMT2300A_STA_STBY);
  }
}

/* =======================================================================
   Component setup
   ======================================================================= */

void SH4RfComponent::setup() {
  /* Configure SPI pins (only if SPI is enabled) */
  if (spi_enabled_ && sclk_ != nullptr && sdio_ != nullptr) {
    sclk_->setup(); sclk_->digital_write(false);
    sdio_->setup(); sdio_->digital_write(false);
  }
  if (csb_  != nullptr) { csb_->setup();  csb_->digital_write(true); }
  if (fcsb_ != nullptr) { fcsb_->setup(); fcsb_->digital_write(true); }

  /* Data pin P20 - setup as INPUT initially (RX), switched to OUTPUT in start_tx() */
  this->RemoteReceiverBase::pin_->setup();
  tx_pin_num_ = this->RemoteTransmitterBase::pin_->get_pin();

  /* ISR store */
  auto &s = store_;
  s.pin = this->RemoteReceiverBase::pin_->to_isr();
  if (s.buffer_size % 2 != 0) s.buffer_size++;

  /* Initialise radio if SPI is available */
  if (spi_enabled_) {
    if (!cmt_init()) {
      ESP_LOGE(TAG, "CMT2300A init failed - check SCLK/SDIO/CSB wiring");
      mark_failed();
      return;
    }
    initialized_ = true;
  }

  if (led_pin_ != nullptr) {
    led_pin_->setup();
    led_pin_->digital_write(false);
  }

  set_receiver(!receiver_disabled_);
}

void SH4RfComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SH4 RF (CMT2300A 433.92 MHz OOK):");
  if (spi_enabled_) {
    if (cmt_product_id_ == 0x66) {
      ESP_LOGCONFIG(TAG, "  CMT2300A: FOUND (product_id=0x66)");
    } else {
      ESP_LOGE(TAG, "  CMT2300A: NOT FOUND (product_id=0x%02X, expected 0x66)", cmt_product_id_);
    }
  }
  if (is_failed()) {
    ESP_LOGE(TAG, "  Component FAILED during setup()!");
    return;
  }
  if (spi_enabled_) {
    LOG_PIN("  SCLK:  ", sclk_);
    LOG_PIN("  SDIO:  ", sdio_);
  } else {
    ESP_LOGCONFIG(TAG, "  SPI init: DISABLED (factory pre-configured SH4)");
  }
  LOG_PIN("  CSB:   ", csb_);
  LOG_PIN("  FCSB:  ", fcsb_);
  LOG_PIN("  TX (DIN):  ", this->RemoteTransmitterBase::pin_);
  LOG_PIN("  RX (DOUT): ", this->RemoteReceiverBase::pin_);
  ESP_LOGCONFIG(TAG, "  RX mode:     %s", rx_mode_ == RxMode::DIRECT ? "direct" : "FIFO");
  ESP_LOGCONFIG(TAG, "  Buffer size: %u",   store_.buffer_size);
  ESP_LOGCONFIG(TAG, "  Filter:      %u µs", store_.filter_us);
  ESP_LOGCONFIG(TAG, "  Start pulse: %u – %u µs", start_pulse_min_us_, start_pulse_max_us_);
  ESP_LOGCONFIG(TAG, "  End pulse:   %u µs", end_pulse_us_);
  ESP_LOGCONFIG(TAG, "  Receiver:    %s", receiver_disabled_ ? "disabled" : "enabled");
  /* Report actual runtime state (set during setup(), visible after WiFi connects) */
  ESP_LOGCONFIG(TAG, "  ISR buffer:  %s (ptr=%p)",
                store_.buffer != nullptr ? "ALLOCATED - receiver running" : "NULL - receiver NOT started",
                (void*)store_.buffer);
}

/* =======================================================================
   Receiver management
   ======================================================================= */

void IRAM_ATTR HOT SH4RfReceiverStore::gpio_intr(SH4RfReceiverStore *arg) {
  const uint32_t now  = micros();
  const uint32_t next = (arg->buffer_write_at + 1) % arg->buffer_size;
  const bool     lvl  = arg->pin.digital_read();

  /* CMT2300A DOUT is active-high: HIGH=mark(carrier), LOW=space
     Even slot = mark start (rising edge = HIGH), odd slot = space start (falling edge = LOW) */
  if (lvl != (next % 2 == 0)) return;
  if (next == arg->buffer_read_at) { arg->overflow = true; return; }
  if (now - arg->buffer[arg->buffer_write_at] <= arg->filter_us) return;

  arg->buffer[arg->buffer_write_at = next] = now;
}

void SH4RfComponent::led_blink_(int times, uint32_t on_ms, uint32_t off_ms) {
  if (led_pin_ == nullptr) return;
  for (int i = 0; i < times; i++) {
    led_pin_->digital_write(true);
    delay(on_ms);
    led_pin_->digital_write(false);
    if (i + 1 < times) delay(off_ms);
  }
}

void SH4RfComponent::set_receiver(bool on) {
  if (on) {
    ESP_LOGD(TAG, "Starting receiver");
    auto &s = store_;
    if (s.buffer == nullptr) {
      s.buffer = new uint32_t[s.buffer_size];
      memset((void *)s.buffer, 0, s.buffer_size * sizeof(uint32_t));
    }
    s.buffer_write_at = s.buffer_read_at =
        this->RemoteReceiverBase::pin_->digital_read() ? 0 : 1;  /* HIGH=mark=even slot */
    s.overflow = false;
    this->RemoteReceiverBase::pin_->attach_interrupt(
        SH4RfReceiverStore::gpio_intr, &store_, gpio::INTERRUPT_ANY_EDGE);
    high_freq_.start();
    if (!transmitting_) {
      if (!start_rx()) {
        ESP_LOGE(TAG, "Failed to start RX mode");
      }
    }
  } else {
    ESP_LOGD(TAG, "Stopping receiver");
    this->RemoteReceiverBase::pin_->detach_interrupt();
    high_freq_.stop();
    if (!transmitting_) go_standby();
  }
}

void SH4RfComponent::turn_on_receiver() {
  if (receiver_disabled_) {
    receiver_disabled_ = false;
    set_receiver(true);
  } else {
    ESP_LOGD(TAG, "Receiver already active");
  }
}

void SH4RfComponent::turn_off_receiver() {
  if (!receiver_disabled_) {
    receiver_disabled_ = true;
    set_receiver(false);
  } else {
    ESP_LOGD(TAG, "Receiver already disabled");
  }
}

void SH4RfComponent::set_rx_mode_runtime(RxMode m) {
  if (m == rx_mode_) return;
  ESP_LOGD(TAG, "Switching RX mode to %s", m == RxMode::DIRECT ? "direct" : "FIFO");
  rx_mode_ = m;
  if (!receiver_disabled_ && !transmitting_) {
    go_standby();
    start_rx();
  }
}

/* =======================================================================
   TX: send_internal()
   Called by RemoteTransmitterBase when a transmit_raw / transmit action fires.
   The CMT2300A is put into TX mode; the CBU then bit-bangs the DIN pin,
   which the CMT2300A forwards directly to the PA without any encoding.
   ======================================================================= */

void SH4RfComponent::await_target_time_() {
  if (target_time_ == 0) { target_time_ = micros(); return; }
  while ((int32_t)(target_time_ - micros()) > 0) App.feed_wdt();
}

void SH4RfComponent::mark_(uint32_t usec) {
  await_target_time_();
  BK_GPIO_HIGH(tx_pin_num_);
  target_time_ += usec;
}

void SH4RfComponent::space_(uint32_t usec) {
  await_target_time_();
  BK_GPIO_LOW(tx_pin_num_);
  target_time_ += usec;
}

void IRAM_ATTR SH4RfComponent::send_internal(uint32_t send_times, uint32_t send_wait) {
  ESP_LOGI(TAG, "Transmitting RF code (%u repetition(s))", send_times);

  led_blink_(3, 20, 20);

  if (!start_tx()) {
    ESP_LOGE(TAG, "TX init failed");
    return;
  }

  ESP_LOGI(TAG, "TX bit-bang on pin %d", tx_pin_num_);

  {
    InterruptLock lock;
    transmitting_ = true;
    BK_GPIO_LOW(tx_pin_num_);

    target_time_ = 0;

    for (uint32_t rep = 0; rep < send_times; rep++) {
      for (int32_t item : this->RemoteTransmitterBase::temp_.get_data()) {
        if (item > 0) mark_(static_cast<uint32_t>(item));
        else          space_(static_cast<uint32_t>(-item));
      }
      if (rep + 1 < send_times && send_wait > 0) space_(send_wait);
    }

    await_target_time_();
    BK_GPIO_LOW(tx_pin_num_);
    transmitting_ = false;
  }

  if (!receiver_disabled_) {
    if (!start_rx()) ESP_LOGE(TAG, "Failed to return to RX after TX");
  } else {
    go_standby();
  }
}

/* =======================================================================
   loop(): process ISR ring buffer → remote_base pipeline
   ======================================================================= */

void SH4RfComponent::loop() {
  if (receiver_disabled_) return;
  if (rx_mode_ == RxMode::DIRECT) process_direct_rx_();
  else                             process_fifo_rx_();
}

/**
 * Direct mode: decode microsecond timestamps from the ISR ring buffer.
 * RF-specific filtering:
 *   - Start pulse must be between start_pulse_min_us_ and start_pulse_max_us_
 *   - Pauses during reception must be < start_pulse_min_us_
 *   - End of frame detected by a pulse >= end_pulse_us_ (CMT2300A generates
 *     ~90 ms tail in direct mode when the signal stops)
 */
void SH4RfComponent::process_direct_rx_() {
  auto &s = store_;

  if (s.overflow) {
    ESP_LOGW(TAG, "ISR buffer overflow, discarding");
    s.overflow = false;
    s.buffer_read_at = s.buffer_write_at;
    old_write_at_ = s.buffer_write_at;
    receive_started_ = false;
    return;
  }

  const uint32_t write_at = s.buffer_write_at;
  const uint32_t dist = (s.buffer_size + write_at - s.buffer_read_at) % s.buffer_size;
  if (dist <= 1) return;

  bool receive_end = false;
  uint32_t new_write_at = old_write_at_;

  while (new_write_at != write_at) {
    uint32_t prev = (new_write_at == 0) ? s.buffer_size - 1 : new_write_at - 1;
    uint32_t diff = s.buffer[new_write_at] - s.buffer[prev];

    if (new_write_at % 2 == 0) {
      /* Falling edge: check for start or end pulse */
      if (diff >= start_pulse_min_us_) {
        if (diff >= end_pulse_us_) {
          if (receive_started_) {
            receive_end   = true;
            new_write_at  = prev;
            break;
          }
        } else if (diff < start_pulse_max_us_) {
          ESP_LOGVV(TAG, "Start pulse %u µs", diff);
          s.buffer_read_at = prev;
          receive_started_ = true;
        }
      }
    } else if (receive_started_ && diff >= start_pulse_min_us_) {
      ESP_LOGVV(TAG, "Long pause %u µs, restarting", diff);
      receive_started_ = false;
    }

    if (!receive_started_) s.buffer_read_at = prev;
    new_write_at = (new_write_at + 1) % s.buffer_size;
  }
  old_write_at_ = new_write_at;

  if (!receive_end) return;

  receive_started_ = false;
  ESP_LOGD(TAG, "RF frame received (direct mode)");
  led_blink_(2, 30, 30);

  /* Build RemoteReceiver data vector from ring buffer timestamps */
  uint32_t prev = s.buffer_read_at;
  s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
  const uint32_t reserve_size =
      1 + (s.buffer_size + new_write_at - s.buffer_read_at) % s.buffer_size;

  this->RemoteReceiverBase::temp_.clear();
  this->RemoteReceiverBase::temp_.reserve(reserve_size);

  int32_t multiplier = (s.buffer_read_at % 2 == 0) ? 1 : -1;
  while (prev != new_write_at) {
    int32_t delta = (int32_t)(s.buffer[s.buffer_read_at] - s.buffer[prev]);
    this->RemoteReceiverBase::temp_.push_back(multiplier * delta);
    prev = s.buffer_read_at;
    s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
    multiplier = -multiplier;
  }
  s.buffer_read_at = (s.buffer_size + s.buffer_read_at - 1) % s.buffer_size;
  this->RemoteReceiverBase::temp_.push_back(multiplier * (int32_t)end_pulse_us_);

  this->call_listeners_dumpers_();
}

/**
 * FIFO mode: poll INT2 pin for PKT_DONE, then read payload over SPI.
 * The received bytes are presented as a raw pulse sequence where each
 * byte is decoded bit-by-bit at the known OOK data rate (~4 kbps).
 * TODO: implement full FIFO byte → timing reconstruction.
 * Currently falls through to direct mode processing as a fallback.
 */
void SH4RfComponent::process_fifo_rx_() {
  /*
   * FIFO mode implementation note:
   * In FIFO mode the CMT2300A demodulates and stores the packet in its
   * 64-byte FIFO, then asserts INT2 (PKT_DONE). The CBU reads the FIFO
   * over SPI (CSB + FCSB) and reconstructs the bit sequence.
   *
   * Full implementation requires:
   *   1. Polling or interrupt on the GPIO wired to CMT2300A INT2
   *   2. SPI read of up to 64 bytes via FCSB
   *   3. Bit-to-timing reconstruction at 4 kbps OOK
   *
   * For now, fall back to direct mode ring buffer processing.
   * Contributions welcome via GitHub issues.
   */
  process_direct_rx_();
}

}  // namespace sh4_rf
}  // namespace esphome
