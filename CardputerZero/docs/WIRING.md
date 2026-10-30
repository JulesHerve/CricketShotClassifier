# Wiring & hardware

This file is the single source of truth for what connects where. Every GPIO the
firmware touches is defined in [`../src/config.h`](../src/config.h); the tables
below explain them.

## 1. The two connectors you'll use

The Cardputer Adv has **two** places things plug in:

1. **The Cap-Bus connector on top** — the **Cap CC1101 & NFC** clicks on here.
   It carries the shared SPI bus, the band-switch line and the NFC interrupt.
   *Nothing else competes for this; the Cap owns it.*
2. **The Grove port on the side** — a single 4-pin Grove (I²C + 2 GPIO). Your
   **Mini IR Unit**, the **433 TX Unit**, or the **I²C Hub** plug in here —
   **only one at a time** (see §5).

The microSD slot and USB-C are always available.

## 2. Shared SPI bus (Cap + SD card)

The Cap's CC1101, the Cap's NFC chip **and** the microSD card are all on **one**
SPI bus. They share SCK/MOSI/MISO and each has its own chip-select. The firmware
keeps all three chip-selects parked HIGH and only lets one device talk at a time
(`core/spi_bus.h`).

| Signal | GPIO | Used by |
|--------|------|---------|
| SPI SCK | **40** | CC1101 + NFC + SD |
| SPI MISO | **39** | CC1101 + NFC + SD |
| SPI MOSI | **14** | CC1101 + NFC + SD |
| microSD CS | **12** | SD card |

## 3. Sub-GHz — TI CC1101 (on the Cap)

| Signal | GPIO | Notes |
|--------|------|-------|
| CC1101 CS | **5** | chip-select |
| CC1101 GDO0 | **15** | packet / async data line (RX edge capture + TX bit-bang) |
| RF_SW0 | **13** | antenna band-select bit 0 (host GPIO) |
| Cap power enable | **3** | drive HIGH to power the Cap's 5 V rail |

**Band switching.** The Cap's antenna matching network only passes three bands:
**315 MHz, 433 MHz, 868–915 MHz.** Two control lines pick the path:

- **RF_SW0** = host GPIO **13**.
- **RF_SW1** = the CC1101's own **GDO2** pin — it never leaves the Cap, so you
  set it over SPI by writing register **IOCFG2** (`0x2F` = force LOW, `0x6F` =
  force HIGH). The firmware does this in `selectBand()`.

Truth table (matches M5Stack's product page and MicroPython driver):

| Band | RF_SW0 | RF_SW1 (GDO2) |
|------|:------:|:-------------:|
| 315 MHz | 0 | 1 |
| 433 MHz | 1 | 0 |
| 868 / 915 MHz | 1 | 1 |

## 4. NFC / RFID — ST25R3916 (on the Cap)

| Signal | GPIO | Notes |
|--------|------|-------|
| NFC CS | **6** | chip-select (set inside the M5Unit-NFC library) |
| NFC IRQ | **4** | interrupt |

Driven entirely by M5Stack's `M5Unit-NFC` library in SPI mode (mode 1, 10 MHz),
exactly as M5Stack's own `CapCC1101` examples configure it.

## 5. Grove port — IR / 433 / I²C Hub (pick ONE)

The Grove port exposes two GPIOs plus I²C:

| Grove pin | GPIO |
|-----------|------|
| Grove A (SDA) | **2** |
| Grove B (SCL) | **1** |

Because there is only one Grove connector, these units are mutually exclusive
unless you add a Grove splitter:

| Unit | How it's wired by default | Config symbol |
|------|---------------------------|---------------|
| **Mini IR Unit** | RX on Grove B (**1**), emitter on Grove A (**2**) | `PIN_IR_RX`, (`PIN_IR_TX` → `GROVE_PIN_A`) |
| **433 TX Unit (SYN115)** | single data wire on Grove A (**2**) | `PIN_RF433_TX` |
| **I²C Hub (PCA9548A)** | I²C on Grove A/B | add I²C units behind it |

### Recommended default

- **Sub-GHz and NFC need nothing on the Grove port** — they're on the Cap. Good.
- **IR send** uses the Cardputer's **built-in IR LED on GPIO 44** by default
  (`PIN_IR_TX = 44`), so IR *replay* and the *universal TV power* feature work
  with **no Grove unit plugged in at all.**
- **IR learn** needs a receiver, which the built-in LED doesn't have — so plug
  the **Mini IR Unit** into Grove when you want to learn codes.

To make IR *send* through the Mini IR Unit's (stronger) emitter instead of the
built-in LED, change one line in `config.h`:

```cpp
constexpr int8_t PIN_IR_TX = GROVE_PIN_A;   // was PIN_IR_TX_BUILTIN
```

### About the I²C Hub

The PCA9548A hub multiplexes **I²C only**. It does **not** carry the IR or 433
single-wire signals, so it won't let you run the IR unit and the 433 unit at the
same time. It's useful later if you add several I²C Grove sensors.

## 6. Parts intentionally not wired

- **RFID Unit 2 (WS1850S)** — redundant second 13.56 MHz reader; the Cap's
  ST25R3916 does more. Left unconnected.
- **IR Reflective Sensor** — proximity sensor, not part of a Flipper-style tool.

## 7. Pin-source citations

The pin map was cross-checked against several independent sources because
M5Stack's own pages were inconsistent on the NFC pins:

- M5Stack UIFlow MicroPython Cap driver — `m5stack/uiflow-micropython`,
  `m5stack/libs/cap/cc1101.py` (SPI 40/14/39, CC1101 CS 5, GDO0 15, RF_SW0 13,
  NFC CS 6, NFC IRQ 4, power-enable 3, and the band truth table).
- M5Stack NFC library — `m5stack/M5Unit-NFC`,
  `src/unit/unit_ST25R3916.cpp` (NFC CS 6, IRQ 4; CC1101 CS 5).
- M5Stack Cap CC1101 product page & Arduino tutorial (RadioLib, bands,
  RF_SW0 = G13, RF_SW1 = CC1101 GDO2).
- Community hardware teardown of the Cap (schematic V0.3) corroborating GDO0 on
  G15 via a 330 Ω series resistor.

> Note: a few M5Stack table pages list the NFC pins differently. The values here
> match the **shipping driver code**, which is the most reliable source. If a
> future hardware revision moves a pin, change it once in `config.h`.
