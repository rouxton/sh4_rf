#include "spi_scan.h"
#include "esphome/core/log.h"

namespace esphome {
namespace spi_scan {

static const char *TAG = "spi_scan";

/* CMT2300A 3-wire SPI: addr byte = (addr<<1)|R/W, then data byte */
uint8_t SpiScanComponent::spi_read_reg_(uint8_t sclk, uint8_t sdio, uint8_t csb, uint8_t addr) {
  /* CSB low */
  pinMode(csb, OUTPUT); digitalWrite(csb, LOW);
  /* Send address byte: (addr<<1)|1 for read */
  pinMode(sdio, OUTPUT);
  uint8_t abyte = (addr << 1) | 0x01;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(sclk, LOW);
    digitalWrite(sdio, (abyte >> i) & 1);
    digitalWrite(sclk, HIGH);
  }
  digitalWrite(sclk, LOW);
  /* Switch SDIO to input */
  pinMode(sdio, INPUT);
  /* Read data byte */
  uint8_t val = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(sclk, LOW);
    digitalWrite(sclk, HIGH);
    if (digitalRead(sdio)) val |= (1 << i);
  }
  digitalWrite(sclk, LOW);
  /* CSB high */
  digitalWrite(csb, HIGH);
  return val;
}

void SpiScanComponent::loop() {
  if (done_) return;
  done_ = true;

  /* Known: CSB=P6, FCSB=P26 on this device */
  const uint8_t CSB = 6;

  /* All candidate pins for SCLK and SDIO */
  const uint8_t candidates[] = {7, 8, 14, 15, 16, 17, 23, 24, 26, 27, 28};
  const uint8_t n = sizeof(candidates);

  ESP_LOGE(TAG, "=== SPI scanner starting, CSB=P6 ===");
  ESP_LOGE(TAG, "Testing %d x %d = %d combinations", n, n, n * n);

  /* Init CSB high, all candidates high */
  pinMode(CSB, OUTPUT); digitalWrite(CSB, HIGH);
  for (int i = 0; i < n; i++) {
    pinMode(candidates[i], OUTPUT);
    digitalWrite(candidates[i], HIGH);
  }
  delay(10);

  int found = 0;
  for (int si = 0; si < n; si++) {
    uint8_t sclk = candidates[si];
    if (sclk == CSB) continue;
    pinMode(sclk, OUTPUT); digitalWrite(sclk, LOW);

    for (int di = 0; di < n; di++) {
      uint8_t sdio = candidates[di];
      if (sdio == sclk || sdio == CSB) continue;

      uint8_t pid = spi_read_reg_(sclk, sdio, CSB, 0x01);
      if (pid == 0x66) {
        ESP_LOGE(TAG, "*** CMT2300A FOUND! SCLK=P%d SDIO=P%d CSB=P%d product_id=0x66 ***",
                 sclk, sdio, CSB);
        found++;
      }
      /* Restore pins */
      pinMode(sclk, OUTPUT); digitalWrite(sclk, LOW);
      pinMode(sdio, OUTPUT); digitalWrite(sdio, HIGH);
    }
    digitalWrite(sclk, HIGH);
  }

  if (found == 0) {
    ESP_LOGE(TAG, "=== No CMT2300A response on any combination ===");
    ESP_LOGE(TAG, "    SCLK/SDIO are confirmed NOT connected to CBU");
  }
  ESP_LOGE(TAG, "=== SPI scan complete ===");
}

}  // namespace spi_scan
}  // namespace esphome
