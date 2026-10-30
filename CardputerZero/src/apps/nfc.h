// =============================================================================
// nfc.h  -  13.56 MHz NFC / RFID tool (ST25R3916 on the Cap)
//
// Capabilities (for YOUR OWN tags/cards):
//   * Read     : detect + identify an NFC-A PICC, show UID / type / ATQA / SAK
//   * Save     : append the tag's identity to a log on the microSD card
//   * Dump     : full memory dump over USB serial (uses the library's dump())
//
// Built directly on M5Stack's M5Unit-NFC (M5UnitUnified) library, configured
// for the Cap's SPI-connected ST25R3916 (CapCC1101NFC). Covers the NTAG203 and
// MIFARE Classic 1K tags you have, plus the other NFC-A tags the library lists.
// =============================================================================
#pragma once
#include "../core/app.h"
#include "../core/ui.h"
#include <string>

namespace apps {

class Nfc : public core::App {
public:
    const char* title() const override { return "NFC / RFID"; }
    void   onEnter() override;
    core::Signal onTick(const core::KeyEvent& ev) override;
    void   onExit() override;

private:
    bool _ready = false;         // library begun?
    bool ensureReader();         // lazy init of the ST25R3916 over SPI
    void drawIdle();
    void readOnce();             // one detect+identify pass
    void dumpSerial();           // full dump to USB serial
    void saveIdentity();         // append last read identity to SD

    std::string _lastUid, _lastType;
    unsigned    _lastAtqa = 0, _lastSak = 0, _lastSize = 0;
    bool        _haveTag = false;
};

}  // namespace apps
