# sh4_rf

ESPHome external component for the **Tuya SH4 RF module** (CMT2300A transceiver).

Supports CBU/BK7231N + SH4 devices operating at 433.92 MHz OOK, including:
- Tuya S11 universal IR+RF remote
- Moes UFO-R2-RF
- Avatto S16 PRO
- Generic no-name IR+RF blasters (CBU + SH4)

## Features

| Feature | Status |
|---------|--------|
| TX – transmit_raw (send learned codes) | ✅ Working |
| RX – dump: raw (learn remote codes) | ✅ Working |
| RX – direct GPIO mode | ✅ Working |
| RX – FIFO packet mode | 🔧 Experimental |
| SPI init (Variant A: SCLK/SDIO connected) | ✅ Supported |
| No SPI init (Variant B: factory pre-configured) | ✅ Supported |
| Runtime RX mode switch (direct ↔ FIFO) | ✅ Supported |
| ESPHome remote_base integration | ✅ Full |
| Home Assistant integration | ✅ Via ESPHome API |

## Hardware variants

Two hardware variants of CBU + SH4 boards exist:

### Variant A – SCLK/SDIO connected (S11, Moes UFO-R2-RF)

The CBU is fully wired to the CMT2300A SPI bus. The firmware writes all
register banks at boot. Set `spi_enabled: true` (default).

### Variant B – SCLK/SDIO not connected (certain no-name blasters)

The CMT2300A is pre-configured by Tuya in factory. Only GPIO1 (DIN),
GPIO2 (DOUT), CSB, and FCSB are connected to the CBU.
Set `spi_enabled: false` to skip the SPI init sequence.

> Variant B confirmed on a no-name IR+RF blaster with the following pinout:
> `CSB=P6, FCSB=P26, GPIO1/DIN=P22, GPIO2/DOUT=P20`

## Pinout

| SH4 signal | SH4 pin | S11 / Moes | Variant B blaster | Notes |
|------------|---------|------------|-------------------|-------|
| SCLK | 4 | P22 | NC | SPI clock (Variant A only) |
| SDIO | 5 | P28 | NC | SPI data (Variant A only) |
| CSB | 6 | P26 | P6 | Register chip select |
| FCSB | 7 | P16 | P26 | FIFO chip select |
| GPIO1 / DIN | 11 | P7 | P22 | TX data input to radio |
| GPIO2 / DOUT | 10 | P8 | P20 | RX demodulated output |
| GPIO3 | 2 | — | — | Optional INT2 / DCLK |

> **Always verify your pinout with a continuity tester or logic analyser
> before flashing.** Pin assignments vary between board revisions.

## Installation

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/rouxton/sh4_rf
      ref: main
    components: [sh4_rf]
```

Requires **LibreTiny ≥ 1.7.0** (GPIO interrupt fix required for RX).

## Minimal configuration

### Variant A (S11 / Moes, SCLK/SDIO connected)

```yaml
sh4_rf:
  id: rf
  spi_enabled: true
  sclk_pin: P22
  sdio_pin: P28
  csb_pin:  P26
  fcsb_pin: P16
  tx_pin:   P7
  rx_pin:   P8
  dump: raw
```

### Variant B (no-name blaster, SCLK/SDIO not connected)

```yaml
sh4_rf:
  id: rf
  spi_enabled: false
  csb_pin:  P6
  fcsb_pin: P26
  tx_pin:   P22
  rx_pin:   P20
  dump: raw
```

## Full configuration reference

```yaml
sh4_rf:
  id: rf

  # SPI pins (Variant A only - omit for Variant B)
  sclk_pin: P22
  sdio_pin: P28

  # Chip select pins (required for both variants)
  csb_pin:  P6
  fcsb_pin: P26

  # Data pins (required)
  tx_pin: P22    # → SH4 GPIO1/DIN  (TX modulation input)
  rx_pin: P20    # ← SH4 GPIO2/DOUT (RX demodulated output)

  # Hardware variant
  spi_enabled: false   # true = write register banks at boot (Variant A)
                       # false = skip SPI init, factory pre-configured (Variant B)

  # Receive mode
  rx_mode: direct   # direct (default): GPIO edge ISR → ring buffer
                    # fifo: CMT2300A packet handler → SPI FIFO read (experimental)

  # ESPHome remote_base settings
  dump: raw          # dump all received codes to the log
  receiver_disabled: false
  buffer_size: 1000
  filter: 50us       # ignore edges shorter than this
  tolerance: 25%

  # RF pulse thresholds (tune for your remote control)
  start_pulse_min: 4ms    # minimum start/sync pulse duration
  start_pulse_max: 12ms   # maximum start/sync pulse duration
  end_pulse: 50ms         # end-of-frame pulse threshold
```

## Workflow: learn a remote control

1. Flash `examples/learn.yaml` to your device
2. Open ESPHome logs (set `logger: level: DEBUG`)
3. Press a button on your 433 MHz remote
4. In the log:
   ```
   [D][remote.raw] Received Raw: 9050, -4480, 565, -1680, 565, -570 ...
   ```
5. Copy the values into `examples/replay.yaml` under the appropriate button
6. Reflash with `examples/replay.yaml`

## Available actions

```yaml
# Enable the RF receiver
- sh4_rf.turn_on_receiver:
    receiver_id: rf

# Disable the RF receiver (standby)
- sh4_rf.turn_off_receiver:
    receiver_id: rf

# Switch receive mode at runtime
- sh4_rf.set_rx_mode:
    receiver_id: rf
    mode: fifo    # or: direct
```

## Technical notes

### CMT2300A operating mode

The CMT2300A is configured in **direct mode** (`PKT1[1:0] = 0x00`):

- **TX**: the CBU bit-bangs the DIN pin. The CMT2300A passes the signal
  directly to the PA without encoding. Timing is done with a busy-wait
  loop accurate to ±1 µs.

- **RX**: the CMT2300A outputs the raw demodulated OOK signal on GPIO2/DOUT.
  A GPIO edge interrupt on the CBU captures timestamps into a ring buffer,
  which is processed in `loop()` to extract RF frames.

This mode is identical to what the original Tuya firmware uses, confirmed
by SPI capture (@olivluca) and by `PKT1=0x10` in the Tuya BSP source.

### Register parameters

The CMT2300A register tables (`cmt2300a_params_433.h`) originate from:
1. SPI capture from a live CBU+SH4 device by [@olivluca](https://github.com/olivluca/tuya_rf)
2. Cross-validated against [tuya/tuya-bsp-gpl-public-components](https://github.com/tuya/tuya-bsp-gpl-public-components)
3. Cross-validated against NVS parameters extracted from a flash dump of a
   Tuya no-name IR+RF blaster (CBU/BK7231N + SH4)

### Frequency

The Frequency Bank registers produce:
- RX: N=0x42, K=0xCCE71 → 433.92 MHz
- TX: N=0x42, K=0xC1C5B → 433.92 MHz

## Troubleshooting

| Symptom | Probable cause | Fix |
|---------|---------------|-----|
| `CMT2300A not found! reg[0x01]=0xFF` | Wrong SPI pinout | Check SCLK/SDIO/CSB wiring |
| TX works, RX nothing | Wrong RX pin | Check which CBU pin receives SH4 GPIO2 |
| RX captures noise | filter too small | Increase `filter: 100us` |
| Incomplete frames | start_pulse_min too high | Reduce to `3ms` |
| TX timing off | LibreTiny version | Upgrade to ≥ 1.7.0 |

## Related projects

- [olivluca/tuya_rf](https://github.com/olivluca/tuya_rf) – the original ESPHome component
  that inspired this one; SPI captures and initial implementation
- [tuya/tuya-bsp-gpl-public-components](https://github.com/tuya/tuya-bsp-gpl-public-components) –
  Tuya GPL BSP with CMT2300A driver source
- [CMT2300A datasheet (HopeRF)](https://www.hoperf.com/uploads/CMT2300ADatasheetEN-V1.7-202307_1695350200.pdf)

## License

MIT
