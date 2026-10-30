// =============================================================================
// config.h  -  Board pin map and build-time options for CardputerZero
//
// Every hardware fact lives here so the rest of the firmware never hard-codes a
// GPIO. All values are for the M5Stack Cardputer Adv + Cap CC1101 & NFC (U219).
//
// Pin sources (cross-checked, see docs/WIRING.md for the full citation list):
//   * M5Stack UIFlow MicroPython Cap driver  (m5stack/uiflow-micropython)
//   * M5Stack M5Unit-NFC library             (m5stack/M5Unit-NFC)
//   * M5Stack Cap CC1101 Arduino tutorial / product page
// =============================================================================
#pragma once
#include <Arduino.h>

namespace cfg {

// ---------------------------------------------------------------------------
// Shared SPI bus
// The Cap's CC1101, the Cap's ST25R3916 NFC front-end and the Cardputer's
// microSD card all sit on ONE SPI bus. Each device has its own chip-select and
// only one may be active at a time (handled by the SpiBus guard in core/).
// ---------------------------------------------------------------------------
constexpr int8_t PIN_SPI_SCK  = 40;
constexpr int8_t PIN_SPI_MISO = 39;
constexpr int8_t PIN_SPI_MOSI = 14;

constexpr int8_t PIN_SD_CS    = 12;   // microSD chip-select

// ---------------------------------------------------------------------------
// Sub-GHz : TI CC1101 on the Cap (SPI)
// RF_SW0 is a host GPIO; RF_SW1 is the CC1101's own GDO2 pin, driven over SPI
// by writing register IOCFG2 (0x2F = force LOW, 0x6F = force HIGH).
// ---------------------------------------------------------------------------
constexpr int8_t PIN_CC1101_CS     = 5;
constexpr int8_t PIN_CC1101_GDO0   = 15;  // packet / async data line
constexpr int8_t PIN_CC1101_RF_SW0 = 13;  // antenna band-select bit 0
constexpr int8_t PIN_CAP_POWER_EN  = 3;   // Cap 5V rail enable (active HIGH)

// CC1101 register + band-switch constants (from the TI datasheet / M5 driver).
constexpr uint8_t CC1101_REG_IOCFG2   = 0x00;
constexpr uint8_t CC1101_GDO2_FORCE_LO = 0x2F;
constexpr uint8_t CC1101_GDO2_FORCE_HI = 0x6F;

// RF front-end matching network only passes these three bands (kHz).
struct Band { uint32_t lo_khz; uint32_t hi_khz; };
constexpr Band CC1101_BANDS[] = {
    {315000, 348000},
    {415000, 464000},
    {830000, 928000},
};

// ---------------------------------------------------------------------------
// NFC / RFID : ST25R3916 on the Cap (SPI)
// The M5Unit-NFC library hard-codes these internally; they are listed here for
// documentation and for the SpiBus guard's awareness.
// ---------------------------------------------------------------------------
constexpr int8_t PIN_NFC_CS  = 6;
constexpr int8_t PIN_NFC_IRQ = 4;

// ---------------------------------------------------------------------------
// Infrared : built-in LED + optional M5Stack Mini IR Unit on the Grove port.
//
// The Cardputer has a built-in IR LED on GPIO44 (send only). The Mini IR Unit
// (Grove) adds a receiver so you can LEARN codes; it also sends. Only one Grove
// unit can be plugged in at a time, so these are compile-time selectable.
//
//   IR_TX_PIN : default = built-in LED (44). Set to GROVE_TX to use the unit.
//   IR_RX_PIN : requires the Mini IR Unit (built-in has no receiver).
// ---------------------------------------------------------------------------
constexpr int8_t GROVE_PIN_A = 2;   // Grove SDA line (G2)
constexpr int8_t GROVE_PIN_B = 1;   // Grove SCL line (G1)

constexpr int8_t PIN_IR_TX_BUILTIN = 44;
constexpr int8_t PIN_IR_TX = PIN_IR_TX_BUILTIN;  // change to GROVE_PIN_A for the unit's emitter
constexpr int8_t PIN_IR_RX = GROVE_PIN_B;        // Mini IR Unit receiver (Grove)

// ---------------------------------------------------------------------------
// Optional 433 MHz OOK transmitter : M5Stack RF Unit (SYN115), single-wire.
// Alternative to the CC1101 for simple 433 replay; shares the Grove port.
// ---------------------------------------------------------------------------
constexpr int8_t PIN_RF433_TX = GROVE_PIN_A;

// ---------------------------------------------------------------------------
// Capture buffer sizing for raw Sub-GHz / IR edge timing.
// ---------------------------------------------------------------------------
constexpr size_t MAX_EDGES = 2048;      // max timing transitions per capture
constexpr uint32_t RF_RX_GAP_US = 15000; // silence that ends a Sub-GHz capture

// ---------------------------------------------------------------------------
// On-SD directory layout. Everything the firmware saves lives under /czero.
// ---------------------------------------------------------------------------
constexpr const char* DIR_ROOT   = "/czero";
constexpr const char* DIR_SUBGHZ = "/czero/subghz";
constexpr const char* DIR_NFC    = "/czero/nfc";
constexpr const char* DIR_IR     = "/czero/ir";
constexpr const char* DIR_BADUSB = "/czero/badusb";

}  // namespace cfg
