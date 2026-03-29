#include "spi_scan.h"
#include "esphome/core/log.h"

namespace esphome {
namespace spi_scan {

static const char *TAG = "spi_scan";

uint8_t SpiScanComponent::spi_read_reg_(uint8_t sclk, uint8_t sdio,
                                         uint8_t csb, uint8_t addr) {
  pinMode(csb, OUTPUT); digitalWrite(csb, LOW);
  pinMode(sdio, OUTPUT);
  uint8_t abyte = (addr << 1) | 0x01;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(sclk, LOW);
    digitalWrite(sdio, (abyte >> i) & 1);
    digitalWrite(sclk, HIGH);
  }
  digitalWrite(sclk, LOW);
  pinMode(sdio, INPUT);
  uint8_t val = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(sclk, LOW);
    digitalWrite(sclk, HIGH);
    if (digitalRead(sdio)) val |= (1 << i);
  }
  digitalWrite(sclk, LOW);
  digitalWrite(csb, HIGH);
  return val;
}

static void blink(uint8_t pin, int times, int period_ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(period_ms / 2);
    digitalWrite(pin, LOW);
    delay(period_ms / 2);
  }
}

void SpiScanComponent::setup() {
  const uint8_t LED  = 9;   /* P9 status LED */
  const uint8_t CSB  = 6;   /* confirmed */
  const uint8_t candidates[] = {7, 8, 14, 15, 16, 17, 23, 24, 26, 27, 28};
  const uint8_t n = sizeof(candidates);

  pinMode(LED, OUTPUT); digitalWrite(LED, LOW);

  /* 3 slow blinks = scan starting */
  blink(LED, 3, 500);
  delay(500);

  pinMode(CSB, OUTPUT); digitalWrite(CSB, HIGH);
  for (int i = 0; i < n; i++) {
    pinMode(candidates[i], OUTPUT);
    digitalWrite(candidates[i], HIGH);
  }
  delay(10);

  int found = 0;
  uint8_t found_sclk = 0, found_sdio = 0;

  for (int si = 0; si < n; si++) {
    uint8_t sclk = candidates[si];
    if (sclk == CSB) continue;
    pinMode(sclk, OUTPUT); digitalWrite(sclk, LOW);

    for (int di = 0; di < n; di++) {
      uint8_t sdio = candidates[di];
      if (sdio == sclk || sdio == CSB) continue;

      uint8_t pid = spi_read_reg_(sclk, sdio, CSB, 0x01);
      if (pid == 0x66) {
        found++;
        found_sclk = sclk;
        found_sdio = sdio;
        ESP_LOGE(TAG, "CMT2300A FOUND: SCLK=P%d SDIO=P%d", sclk, sdio);
      }
      pinMode(sclk, OUTPUT); digitalWrite(sclk, LOW);
      pinMode(sdio, OUTPUT); digitalWrite(sdio, HIGH);
    }
    digitalWrite(sclk, HIGH);
  }

  if (found > 0) {
    /* FOUND: 10 rapid blinks */
    ESP_LOGE(TAG, "FOUND %d match(es): last SCLK=P%d SDIO=P%d", found, found_sclk, found_sdio);
    blink(LED, 10, 100);
  } else {
    /* NOT FOUND: 3 long blinks */
    ESP_LOGE(TAG, "No CMT2300A response - SCLK/SDIO not connected");
    blink(LED, 3, 1000);
  }

  /* Keep LED on = scan complete */
  digitalWrite(LED, HIGH);
}

}  // namespace spi_scan
}  // namespace esphome
