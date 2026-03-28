#pragma once

/**
 * sh4_rf.h
 * ESPHome external component for the Tuya SH4 RF module (CMT2300A).
 *
 * Supports CBU/BK7231N + SH4 devices:
 *   - Tuya S11 universal IR+RF remote
 *   - Moes UFO-R2-RF
 *   - Avatto S16 PRO
 *   - Generic no-name IR+RF blasters using CBU + SH4
 *
 * Features:
 *   - 433.92 MHz OOK, direct GPIO mode (no FIFO packet handler)
 *   - TX: bit-bang DIN pin → CMT2300A → PA → antenna
 *   - RX: CMT2300A DOUT → edge interrupt → microsecond ring buffer
 *         → ESPHome remote_base pipeline (dump: raw, on_receive, etc.)
 *   - Optional SPI init (for boards where SCLK/SDIO are connected)
 *   - Runtime switch between DIRECT and FIFO receive modes
 *   - Actions: sh4_rf.turn_on_receiver / sh4_rf.turn_off_receiver
 *
 * Hardware notes:
 *   Two hardware variants exist:
 *
 *   Variant A (S11, Moes UFO-R2-RF) - SCLK/SDIO connected:
 *     The CBU can fully configure the CMT2300A over SPI at boot.
 *     All 6 register banks are written by the firmware.
 *     Set `spi_enabled: true` (default) in YAML.
 *
 *   Variant B (certain no-name blasters) - SCLK/SDIO NOT connected:
 *     The CMT2300A is pre-configured by Tuya in factory (OTP/EEPROM).
 *     The CBU only drives GPIO1 (DIN), GPIO2 (DOUT), CSB and FCSB.
 *     Set `spi_enabled: false` in YAML to skip the SPI init sequence.
 *
 * Pinout (adjust to your board):
 *   Signal        | SH4 pin | CBU example   | Notes
 *   --------------|---------|---------------|-----------------------------
 *   SCLK          | 4       | P22 (S11)     | SPI clock (Variant A only)
 *   SDIO          | 5       | P28 (S11)     | SPI data  (Variant A only)
 *   CSB           | 6       | P6  (S11/B)   | Register chip select
 *   FCSB          | 7       | P26 (S11)     | FIFO chip select
 *   GPIO1 / DIN   | 11      | P7  (S11)     | TX data input
 *   GPIO2 / DOUT  | 10      | P8  (S11)     | RX demodulated output
 *   GPIO3         | 2       | optional      | INT2 or DCLK (optional)
 *
 * Variant B pinout confirmed:
 *   CSB=P6, FCSB=P26, GPIO1/DIN=P22, GPIO2/DOUT=P20
 */

#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include <vector>

namespace esphome {
namespace sh4_rf {

/* -----------------------------------------------------------------------
   RX ISR ring buffer store
   Same pattern as ESPHome's built-in remote_receiver component.
   Each entry is the timestamp (micros) of a signal edge.
   Even indices = falling edge (mark start), odd = rising edge (space start).
   ----------------------------------------------------------------------- */
struct SH4RfReceiverStore {
  static void gpio_intr(SH4RfReceiverStore *arg);

  volatile uint32_t *buffer{nullptr};
  volatile uint32_t  buffer_write_at{0};
  uint32_t           buffer_read_at{0};
  bool               overflow{false};
  uint32_t           buffer_size{1000};
  uint32_t           filter_us{50};
  ISRInternalGPIOPin pin;
};

/* -----------------------------------------------------------------------
   Receive mode selection
   ----------------------------------------------------------------------- */
enum class RxMode {
  DIRECT, /**< CMT2300A DOUT pin directly to GPIO edge interrupt (default) */
  FIFO,   /**< CMT2300A packet handler + FIFO read over SPI (requires SCLK/SDIO) */
};

/* -----------------------------------------------------------------------
   SH4RfComponent
   Inherits from both RemoteTransmitterBase and RemoteReceiverBase so it
   integrates natively with all ESPHome remote_base protocols and dumpers.
   ----------------------------------------------------------------------- */
class SH4RfComponent : public Component,
                       public remote_base::RemoteTransmitterBase,
                       public remote_base::RemoteReceiverBase {
 public:
  /**
   * Constructor.
   * @param sclk  SPI clock pin (may be nullptr if spi_enabled=false)
   * @param sdio  SPI data pin  (may be nullptr if spi_enabled=false)
   * @param csb   Register chip select (active low)
   * @param fcsb  FIFO chip select (active low)
   * @param tx    DIN data pin → CBU drives this to modulate the PA
   * @param rx    DOUT data pin ← CMT2300A demodulated output
   */
  SH4RfComponent(InternalGPIOPin *sclk, InternalGPIOPin *sdio,
                 InternalGPIOPin *csb,  InternalGPIOPin *fcsb,
                 InternalGPIOPin *tx,   InternalGPIOPin *rx)
      : remote_base::RemoteTransmitterBase(tx),
        remote_base::RemoteReceiverBase(rx),
        sclk_(sclk), sdio_(sdio), csb_(csb), fcsb_(fcsb) {}

  /* --- Component lifecycle ------------------------------------------- */
  void setup()       override;
  void loop()        override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  /* --- Configuration setters (called from __init__.py) --------------- */
  void set_spi_enabled(bool v)              { spi_enabled_ = v; }
  void set_rx_mode(RxMode m)                { rx_mode_ = m; }
  void set_receiver_disabled(bool v)        { receiver_disabled_ = v; }
  void set_buffer_size(uint32_t v)          { store_.buffer_size = v; }
  void set_filter_us(uint32_t v)            { store_.filter_us   = v; }
  void set_start_pulse_min_us(uint32_t v)   { start_pulse_min_us_ = v; }
  void set_start_pulse_max_us(uint32_t v)   { start_pulse_max_us_ = v; }
  void set_end_pulse_us(uint32_t v)         { end_pulse_us_ = v; }

  /* --- Runtime actions (callable from YAML automations) -------------- */
  void turn_on_receiver();
  void turn_off_receiver();
  void set_rx_mode_runtime(RxMode m);

 protected:
  /* RemoteTransmitterBase override */
  void send_internal(uint32_t send_times, uint32_t send_wait) override;

  /* TX helpers */
  void mark_(uint32_t usec);
  void space_(uint32_t usec);
  void await_target_time_();

  /* Radio state management */
  bool cmt_init();      /**< Write all 6 register banks over SPI (Variant A only) */
  bool start_tx();      /**< Configure CMT2300A for TX mode */
  bool start_rx();      /**< Configure CMT2300A for RX mode (direct or FIFO) */
  void go_standby();

  /* SPI bit-bang primitives (3-wire, bidirectional SDIO) */
  void    spi_write_reg(uint8_t addr, uint8_t data);
  uint8_t spi_read_reg(uint8_t addr);
  void    spi_write_bank(uint8_t base_addr, const uint8_t *bank, uint8_t len);

  /* CMT2300A state machine helpers */
  bool wait_state_(uint8_t expected);
  bool go_state_(uint8_t cmd, uint8_t expected);

  /* Receiver management */
  void set_receiver(bool on);

  /* loop() helpers */
  void process_direct_rx_();
  void process_fifo_rx_();

  /* Pins */
  InternalGPIOPin *sclk_{nullptr};
  InternalGPIOPin *sdio_{nullptr};
  InternalGPIOPin *csb_{nullptr};
  InternalGPIOPin *fcsb_{nullptr};

  /* RX ISR store */
  SH4RfReceiverStore store_;
  HighFrequencyLoopRequester high_freq_;

  /* Configuration */
  bool    spi_enabled_{true};          /**< Write register banks at init */
  RxMode  rx_mode_{RxMode::DIRECT};    /**< Direct GPIO or FIFO mode */
  bool    receiver_disabled_{false};

  /* Runtime state */
  bool     transmitting_{false};
  bool     receive_started_{false};
  bool     initialized_{false};
  bool     first_loop_{true};   /* diagnostic: report setup result on first loop() */
  uint32_t target_time_{0};
  uint32_t old_write_at_{0};

  /* RX filtering thresholds (microseconds) */
  uint32_t start_pulse_min_us_{6000};  /**< Minimum start pulse duration */
  uint32_t start_pulse_max_us_{10000}; /**< Maximum start pulse duration */
  uint32_t end_pulse_us_{50000};       /**< End-of-frame pulse threshold */
};

}  // namespace sh4_rf
}  // namespace esphome
