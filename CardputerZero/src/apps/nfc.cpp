// =============================================================================
// nfc.cpp  -  implementation of the NFC / RFID tool
// =============================================================================
#include "nfc.h"
#include "../core/spi_bus.h"
#include "../core/storage.h"
#include "../config.h"

#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <M5Utility.h>
#include <wiring/m5_unit_unified_wiring.hpp>

using namespace core;
using namespace m5::nfc::a;

namespace {
// The CapCC1101NFC component knows its own CS (G6) / IRQ (G4) internally.
m5::unit::UnitUnified   g_units;
m5::unit::CapCC1101NFC  g_unit{};
m5::nfc::NFCLayerA      g_nfc{g_unit};
bool                    g_begun = false;
}  // namespace

namespace apps {

bool Nfc::ensureReader() {
    if (_ready) return true;
    // The NFC chip lives on the shared SPI bus; take ownership before begin().
    pinMode(cfg::PIN_CAP_POWER_EN, OUTPUT);
    digitalWrite(cfg::PIN_CAP_POWER_EN, HIGH);
    SpiBus::claim(SpiBus::Owner::Nfc);

    if (!g_begun) {
        // SPI mode 1 @ 10 MHz, exactly as M5Stack's CapCC1101 examples use.
        bool ok = m5::unit::wiring::addSPI(g_units, g_unit, 10000000, 1) && g_units.begin();
        g_begun = ok;
    }
    _ready = g_begun;
    return _ready;
}

void Nfc::onEnter() { drawIdle(); }

void Nfc::onExit() { SpiBus::parkAll(); }

void Nfc::drawIdle() {
    UI::clear();
    UI::header("NFC / RFID", Storage::isReady() ? "SD ok" : "no SD");
    if (_haveTag) {
        auto& d = M5Cardputer.Display;
        d.setTextFont(&fonts::Font2);
        d.setTextDatum(top_left);
        d.setTextColor(UI::cText(), UI::cBg());
        d.fillRect(0, 18, UI::W, UI::H - 32, UI::cBg());
        int y = 24;
        d.setTextColor(UI::cAccent(), UI::cBg());
        d.drawString(_lastType.c_str(), 6, y); y += 18;
        d.setTextColor(UI::cText(), UI::cBg());
        std::string u = "UID " + _lastUid;
        d.drawString(u.c_str(), 6, y); y += 16;
        char l[48];
        snprintf(l, sizeof(l), "ATQA %04X  SAK %02X  %u-byte UID",
                 _lastAtqa, _lastSak, _lastSize);
        d.setTextColor(UI::cDim(), UI::cBg());
        d.setTextFont(&fonts::Font0);
        d.drawString(l, 6, y);
    } else {
        UI::banner("Hold a tag to the\nback of the Cap", UI::cAccent());
    }
    UI::footer("[ENTER] read  [s] save  [d] dump serial  [`] back");
}

void Nfc::readOnce() {
    if (!ensureReader()) { UI::banner("Reader init failed", UI::cWarn()); delay(1200); drawIdle(); return; }
    SpiBus::claim(SpiBus::Owner::Nfc);
    g_units.update();

    PICC picc{};
    if (g_nfc.detect(picc) && g_nfc.identify(picc)) {
        M5.Speaker.tone(3000, 20);
        _lastUid  = picc.uidAsString();
        _lastType = picc.typeAsString();
        _lastAtqa = picc.atqa;
        _lastSak  = picc.sak;
        _lastSize = picc.size;
        _haveTag  = true;
        g_nfc.deactivate();
    } else {
        _haveTag = false;
        UI::banner("No tag found", UI::cWarn());
        delay(700);
    }
    drawIdle();
}

void Nfc::saveIdentity() {
    if (!_haveTag) { UI::banner("Read a tag first", UI::cWarn()); delay(700); drawIdle(); return; }
    if (!Storage::isReady()) { UI::banner("No SD card", UI::cWarn()); delay(700); drawIdle(); return; }
    std::string line = _lastType + "," + _lastUid + ",";
    char meta[32]; snprintf(meta, sizeof(meta), "ATQA=%04X,SAK=%02X\n", _lastAtqa, _lastSak);
    line += meta;
    std::string path = std::string(cfg::DIR_NFC) + "/tags.csv";
    bool ok = Storage::append(path, line);
    UI::banner(ok ? "Saved to /czero/nfc/tags.csv" : "Save failed",
               ok ? UI::cAccent() : UI::cWarn());
    delay(900);
    drawIdle();
}

void Nfc::dumpSerial() {
    if (!ensureReader()) { UI::banner("Reader init failed", UI::cWarn()); delay(1200); drawIdle(); return; }
    SpiBus::claim(SpiBus::Owner::Nfc);
    g_units.update();
    UI::banner("Dumping to USB serial...", UI::cAccent());

    PICC picc{};
    if (g_nfc.detect(picc) && g_nfc.identify(picc) && g_nfc.reactivate(picc)) {
        Serial.printf("\n==== NFC dump %s %s ====\n",
                      picc.uidAsString().c_str(), picc.typeAsString().c_str());
        // dump() uses the default MIFARE key (0xFF..) for Classic; harmless for
        // NTAG/Ultralight which ignore it. Output goes to M5.Log / USB serial.
        g_nfc.dump(m5::nfc::a::mifare::classic::DEFAULT_KEY);
        g_nfc.deactivate();
        UI::banner("Dump complete\n(see USB serial)", UI::cAccent());
    } else {
        UI::banner("No tag / identify failed", UI::cWarn());
    }
    delay(1200);
    drawIdle();
}

core::Signal Nfc::onTick(const core::KeyEvent& ev) {
    if (ev.key == Key::Back) return Signal::Back;
    if (ev.key == Key::Ok)   readOnce();
    if (ev.key == Key::Char && (ev.ch == 's' || ev.ch == 'S')) saveIdentity();
    if (ev.key == Key::Char && (ev.ch == 'd' || ev.ch == 'D')) dumpSerial();
    return Signal::Stay;
}

}  // namespace apps
