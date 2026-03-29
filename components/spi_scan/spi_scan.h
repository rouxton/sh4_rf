#pragma once
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace spi_scan {

class SpiScanComponent : public Component {
 public:
  void setup() override {}
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA - 10; }
 private:
  bool done_{false};
  uint8_t spi_read_reg_(uint8_t sclk, uint8_t sdio, uint8_t csb, uint8_t addr);
};

}  // namespace spi_scan
}  // namespace esphome
