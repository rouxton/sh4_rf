#pragma once
/**
 * automation.h
 * ESPHome automation actions for the sh4_rf component.
 *
 * Available actions:
 *   sh4_rf.turn_on_receiver   - enable the RF receiver
 *   sh4_rf.turn_off_receiver  - disable the RF receiver (standby)
 *   sh4_rf.set_rx_mode        - switch between direct and FIFO receive modes
 */

#include "sh4_rf.h"
#include "esphome/core/automation.h"

namespace esphome {
namespace sh4_rf {

/** Enable the RF receiver. No-op if already enabled. */
template<typename... Ts>
class TurnOnReceiverAction : public Action<Ts...>, public Parented<SH4RfComponent> {
 public:
  void play(Ts... x) override { this->parent_->turn_on_receiver(); }
};

/** Disable the RF receiver (put CMT2300A into standby). No-op if already disabled. */
template<typename... Ts>
class TurnOffReceiverAction : public Action<Ts...>, public Parented<SH4RfComponent> {
 public:
  void play(Ts... x) override { this->parent_->turn_off_receiver(); }
};

/** Switch the receive mode at runtime between DIRECT and FIFO. */
template<typename... Ts>
class SetRxModeAction : public Action<Ts...>, public Parented<SH4RfComponent> {
 public:
  void set_mode(RxMode m) { mode_ = m; }
  void play(Ts... x) override { this->parent_->set_rx_mode_runtime(mode_); }
 private:
  RxMode mode_{RxMode::DIRECT};
};

}  // namespace sh4_rf
}  // namespace esphome
