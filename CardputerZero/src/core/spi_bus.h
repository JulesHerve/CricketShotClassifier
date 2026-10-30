// =============================================================================
// spi_bus.h  -  Shared-SPI arbitration for CC1101 / NFC / microSD
//
// The Cap's CC1101, the Cap's NFC chip and the SD card all hang off the same
// SPI pins. Only one chip-select may be LOW at any moment. This tiny guard
// parks every other CS HIGH before an app takes the bus, so a half-finished
// NFC transaction can never corrupt an SD write (or vice-versa).
// =============================================================================
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "../config.h"

namespace core {

class SpiBus {
public:
    // Call once in setup(), after M5 begins. Brings the whole bus up and drives
    // every chip-select HIGH (de-selected) so nothing is accidentally active.
    static void begin() {
        if (_started) return;
        parkAll();
        SPI.begin(cfg::PIN_SPI_SCK, cfg::PIN_SPI_MISO, cfg::PIN_SPI_MOSI);
        _started = true;
    }

    // Drive all three chip-selects HIGH (de-selected).
    static void parkAll() {
        for (int8_t cs : {cfg::PIN_SD_CS, cfg::PIN_CC1101_CS, cfg::PIN_NFC_CS}) {
            pinMode(cs, OUTPUT);
            digitalWrite(cs, HIGH);
        }
    }

    // Owner identifiers for the bus.
    enum class Owner { None, SubGhz, Nfc, Sd };

    // An app announces it is about to use a device. Everything else is parked.
    static void claim(Owner who) {
        if (who == _owner) return;
        parkAll();          // de-select everybody first
        _owner = who;       // the device's own driver will pull its CS low
    }

    static Owner owner() { return _owner; }

private:
    static inline bool  _started = false;
    static inline Owner _owner   = Owner::None;
};

}  // namespace core
