// =============================================================================
// subghz.cpp  -  implementation of the Sub-GHz tool
// =============================================================================
#include "subghz.h"
#include "../core/spi_bus.h"
#include "../core/storage.h"
#include "../config.h"
#include <RadioLib.h>
#include <SPI.h>

using namespace core;

namespace {

// ---- CC1101 radio instance ------------------------------------------------
// Module(cs, gdo0, rst=NC, gdo2=NC). The Cap routes GDO2 to the band switch on
// the board itself, so it is not wired to a host pin => RADIOLIB_NC here.
Module  g_mod(cfg::PIN_CC1101_CS, cfg::PIN_CC1101_GDO0, RADIOLIB_NC, RADIOLIB_NC);
CC1101  g_radio(&g_mod);

// ---- Raw OOK capture buffer -----------------------------------------------
// Edge-to-edge durations in microseconds. The sign is implicit: element 0 is
// the first HIGH duration, then LOW, HIGH, ... (classic RC pulse train).
struct Capture {
    uint32_t edges[cfg::MAX_EDGES];
    size_t   count = 0;
    uint32_t freq_hz = 433920000;
};
Capture g_cap;

// ISR state for edge timing on GDO0 during async RX.
volatile uint32_t g_lastEdge = 0;
volatile size_t   g_isrCount = 0;
volatile bool     g_isrActive = false;

void IRAM_ATTR gdo0Isr() {
    if (!g_isrActive) return;
    uint32_t now = micros();
    uint32_t dt  = now - g_lastEdge;
    g_lastEdge = now;
    if (g_isrCount < cfg::MAX_EDGES) g_cap.edges[g_isrCount++] = dt;
    else g_isrActive = false;  // buffer full, stop
}

// Frequency presets (Hz) that fall inside the Cap's three legal bands.
struct Preset { const char* name; uint32_t hz; };
const Preset PRESETS[] = {
    {"315.00 MHz", 315000000},
    {"318.00 MHz", 318000000},
    {"433.92 MHz", 433920000},
    {"434.42 MHz", 434420000},
    {"868.35 MHz", 868350000},
    {"915.00 MHz", 915000000},
};
constexpr int N_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

// Pick RF_SW0/RF_SW1 for a frequency, matching M5Stack's board truth table:
//   315 band -> SW0=0 SW1=1 ; 433 band -> SW0=1 SW1=0 ; 868+ -> SW0=1 SW1=1
void selectBand(uint32_t hz) {
    uint32_t khz = hz / 1000;
    uint8_t sw0, sw1;
    if (khz < 374000)      { sw0 = 0; sw1 = 1; }
    else if (khz < 650500) { sw0 = 1; sw1 = 0; }
    else                   { sw0 = 1; sw1 = 1; }
    digitalWrite(cfg::PIN_CC1101_RF_SW0, sw0);
    // RF_SW1 = CC1101 GDO2. setDIOMapping(2,...) writes register IOCFG2.
    g_radio.setDIOMapping(2, sw1 ? cfg::CC1101_GDO2_FORCE_HI : cfg::CC1101_GDO2_FORCE_LO);
}

}  // namespace

namespace apps {

bool SubGhz::ensureRadio() {
    if (_radioOk) return true;
    pinMode(cfg::PIN_CAP_POWER_EN, OUTPUT);
    digitalWrite(cfg::PIN_CAP_POWER_EN, HIGH);        // power the Cap's 5V rail
    pinMode(cfg::PIN_CC1101_RF_SW0, OUTPUT);
    SpiBus::claim(SpiBus::Owner::SubGhz);

    // RadioLib 7.x configures the radio through a ConfigFSK_t struct; all
    // values are integer Hz / bps. begin() brings up 2-FSK; we switch to OOK
    // and disable encoding/CRC afterwards for raw remote-style signals.
    ConfigFSK_t rc;
    rc.frequency          = 433920000;  // Hz (re-set per capture below)
    rc.bitRate            = 5000;        // bps
    rc.frequencyDeviation = 47600;       // Hz (ignored in OOK, kept valid)
    rc.receiverBandwidth  = 203000;      // Hz
    rc.power              = 10;          // dBm
    rc.preambleLength     = 16;          // bits

    int st = g_radio.begin(rc);
    if (st == RADIOLIB_ERR_NONE) {
        g_radio.setOOK(true);
        g_radio.setEncoding(RADIOLIB_ENCODING_NRZ);   // no manchester/whitening
        g_radio.setCrcFiltering(false);
        g_radio.setRxBandwidth(203000);
        g_radio.setBitRate(5000);
        g_radio.setOutputPower(10);
        _radioOk = true;
    } else {
        _radioOk = false;
    }
    return _radioOk;
}

void SubGhz::onEnter() {
    _view = View::Menu;
    _menuSel = _menuTop = 0;
    drawMenu();
}

void SubGhz::onExit() {
    g_isrActive = false;
    if (_radioOk) g_radio.standby();
    SpiBus::parkAll();
}

// ---- menus ----------------------------------------------------------------
void SubGhz::drawMenu() {
    UI::clear();
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "%s", PRESETS[_freqSel].name);
    UI::header("Sub-GHz", hdr);
    std::vector<std::string> items = {
        "Receive (raw OOK)",
        "Replay last capture",
        "Frequency analyzer",
        "Set frequency",
        "Load from SD",
    };
    UI::list(items, _menuSel, _menuTop);
    UI::footer("[;/.] move  [ENTER] select  [`] back");
}

void SubGhz::drawFreq() {
    UI::clear();
    UI::header("Set frequency");
    std::vector<std::string> items;
    for (int i = 0; i < N_PRESETS; ++i) items.push_back(PRESETS[i].name);
    UI::list(items, _freqSel, _freqSel < 6 ? 0 : _freqSel - 5);
    UI::footer("[;/.] choose  [ENTER] use  [`] back");
}

void SubGhz::drawAnalyzer() {
    UI::header("Analyzer", PRESETS[_freqSel].name);
    UI::footer("sweeping RSSI...  [`] back");
}

void SubGhz::drawFiles() {
    UI::clear();
    UI::header("Load capture");
    if (_files.empty()) {
        UI::banner(Storage::isReady() ? "No .sub files" : "No SD card", UI::cDim());
    } else {
        UI::list(_files, _fileSel, _fileTop);
    }
    UI::footer("[ENTER] load  [`] back");
}

// ---- actions --------------------------------------------------------------
void SubGhz::doReceive() {
    if (!ensureRadio()) { UI::banner("Radio init failed", UI::cWarn()); delay(1200); return; }
    uint32_t hz = PRESETS[_freqSel].hz;
    SpiBus::claim(SpiBus::Owner::SubGhz);
    g_radio.setFrequency(hz);
    selectBand(hz);

    UI::clear();
    UI::header("Receiving", PRESETS[_freqSel].name);
    UI::banner("Listening...\npress your remote", UI::cAccent());
    UI::footer("[`] stop");

    // Enter async direct RX: GDO0 streams demodulated data; we time the edges.
    g_radio.receiveDirectAsync();
    selectBand(hz);                       // re-assert RF_SW1 after mode change
    pinMode(cfg::PIN_CC1101_GDO0, INPUT);

    g_isrCount = 0; g_cap.count = 0;
    g_lastEdge = micros();
    g_isrActive = true;
    attachInterrupt(digitalPinToInterrupt(cfg::PIN_CC1101_GDO0), gdo0Isr, CHANGE);

    // Wait for activity, then for the trailing gap, or a manual stop / timeout.
    uint32_t start = millis();
    size_t lastSeen = 0;
    uint32_t lastGrow = millis();
    while (true) {
        if (g_isrCount != lastSeen) { lastSeen = g_isrCount; lastGrow = millis(); }
        bool gotSome = g_isrCount > 16;
        if (gotSome && (micros() - g_lastEdge) > cfg::RF_RX_GAP_US) break;  // burst ended
        if (millis() - start > 12000) break;                               // overall timeout
        if (gotSome && millis() - lastGrow > 400) break;                   // idle after a burst
        auto ev = Input::poll();
        if (ev.key == Key::Back) break;
        delay(1);
    }
    g_isrActive = false;
    detachInterrupt(digitalPinToInterrupt(cfg::PIN_CC1101_GDO0));
    g_radio.standby();

    g_cap.count = g_isrCount;
    g_cap.freq_hz = hz;

    _con.reset();
    _view = View::Result;
    UI::clear();
    UI::header("Capture result", PRESETS[_freqSel].name);
    if (g_cap.count < 16) {
        _con.line("Nothing usable captured.");
        _con.line("Try again closer / different freq.");
    } else {
        char l[48];
        snprintf(l, sizeof(l), "Captured %u edges", (unsigned)g_cap.count);
        _con.line(l);
        // show the first few timings as a sanity preview
        for (size_t i = 0; i < g_cap.count && i < 8; ++i) {
            snprintf(l, sizeof(l), "  e[%u] = %lu us", (unsigned)i, (unsigned long)g_cap.edges[i]);
            _con.line(l);
        }
        _con.line("");
        _con.line("[ENTER] replay   [s] save SD   [`] back");
    }
    UI::footer("[ENTER] replay  [s] save  [`] back");
}

void SubGhz::doReplay() {
    if (g_cap.count < 16) { UI::banner("Nothing to replay", UI::cWarn()); delay(1000); return; }
    if (!ensureRadio())  { UI::banner("Radio init failed", UI::cWarn()); delay(1200); return; }

    SpiBus::claim(SpiBus::Owner::SubGhz);
    g_radio.setFrequency(g_cap.freq_hz);
    selectBand(g_cap.freq_hz);

    UI::clear();
    UI::header("Transmitting", PRESETS[_freqSel].name);
    UI::banner("Replaying...", UI::cWarn());

    // Async TX direct mode makes GDO0 an input to the chip; we drive it.
    g_radio.transmitDirectAsync();
    selectBand(g_cap.freq_hz);
    pinMode(cfg::PIN_CC1101_GDO0, OUTPUT);

    // Play the edge train twice for reliability (like a real remote repeats).
    for (int rep = 0; rep < 2; ++rep) {
        bool level = true;                       // first duration is HIGH
        for (size_t i = 0; i < g_cap.count; ++i) {
            digitalWrite(cfg::PIN_CC1101_GDO0, level ? HIGH : LOW);
            delayMicroseconds(g_cap.edges[i]);
            level = !level;
        }
        digitalWrite(cfg::PIN_CC1101_GDO0, LOW);
        delay(20);
    }
    g_radio.standby();
    UI::banner("Done", UI::cAccent());
    delay(700);
}

// ---- SD persistence --------------------------------------------------------
// Minimal Flipper-ish .sub-like text: a header plus the raw edge list. This is
// our own format (not byte-compatible with Flipper), kept human-readable.
bool SubGhz::saveCapture() {
    if (g_cap.count < 16) return false;
    if (!Storage::isReady()) return false;
    std::string path = Storage::uniqueName(cfg::DIR_SUBGHZ, "cap", "sub");
    std::string body = "CardputerZero RAW\nFrequency: ";
    body += std::to_string(g_cap.freq_hz);
    body += "\nEdges: " + std::to_string(g_cap.count) + "\nRAW_Data:";
    for (size_t i = 0; i < g_cap.count; ++i) {
        body += ' ';
        body += std::to_string(g_cap.edges[i]);
    }
    body += "\n";
    return Storage::write(path, body);
}

bool SubGhz::loadCapture(const std::string& name) {
    std::string path = std::string(cfg::DIR_SUBGHZ) + "/" + name;
    std::string txt;
    if (!Storage::read(path, txt)) return false;
    // parse "Frequency:" and the RAW_Data list
    size_t fp = txt.find("Frequency:");
    if (fp != std::string::npos) g_cap.freq_hz = strtoul(txt.c_str() + fp + 10, nullptr, 10);
    size_t rp = txt.find("RAW_Data:");
    if (rp == std::string::npos) return false;
    const char* p = txt.c_str() + rp + 9;
    g_cap.count = 0;
    while (*p && g_cap.count < cfg::MAX_EDGES) {
        char* end;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        if (v > 0) g_cap.edges[g_cap.count++] = (uint32_t)v;
        p = end;
    }
    // align the on-screen frequency label to the loaded capture if it matches
    for (int i = 0; i < N_PRESETS; ++i)
        if (PRESETS[i].hz == g_cap.freq_hz) { _freqSel = i; break; }
    return g_cap.count >= 16;
}

void SubGhz::refreshFiles() {
    _files = Storage::listDir(cfg::DIR_SUBGHZ);
    _fileSel = _fileTop = 0;
}

// ---- state machine ---------------------------------------------------------
core::Signal SubGhz::onTick(const core::KeyEvent& ev) {
    switch (_view) {
    case View::Menu:
        if (ev.key == Key::Up)   { UI::move(-1, 5, _menuSel, _menuTop); drawMenu(); }
        if (ev.key == Key::Down) { UI::move(+1, 5, _menuSel, _menuTop); drawMenu(); }
        if (ev.key == Key::Back) return Signal::Back;
        if (ev.key == Key::Ok) {
            switch (_menuSel) {
            case 0: doReceive(); break;
            case 1: doReplay(); drawMenu(); break;
            case 2: _view = View::Analyzer; break;
            case 3: _view = View::Freq; drawFreq(); break;
            case 4: refreshFiles(); _view = View::Files; drawFiles(); break;
            }
        }
        break;

    case View::Freq:
        if (ev.key == Key::Up)   { UI::move(-1, N_PRESETS, _freqSel, _menuTop, N_PRESETS); drawFreq(); }
        if (ev.key == Key::Down) { UI::move(+1, N_PRESETS, _freqSel, _menuTop, N_PRESETS); drawFreq(); }
        if (ev.key == Key::Ok || ev.key == Key::Back) { _view = View::Menu; drawMenu(); }
        break;

    case View::Result:
        if (ev.key == Key::Ok) { doReplay(); }
        if (ev.key == Key::Char && (ev.ch == 's' || ev.ch == 'S')) {
            bool ok = saveCapture();
            UI::banner(ok ? "Saved to SD" : "Save failed", ok ? UI::cAccent() : UI::cWarn());
            delay(800);
        }
        if (ev.key == Key::Back) { _view = View::Menu; drawMenu(); }
        break;

    case View::Analyzer: {
        if (ev.key == Key::Back) { _view = View::Menu; drawMenu(); break; }
        if (!ensureRadio()) { UI::banner("Radio init failed", UI::cWarn()); delay(1000); _view = View::Menu; drawMenu(); break; }
        // One sweep per tick: measure RSSI at each preset, draw bars.
        UI::clear();
        UI::header("Analyzer");
        auto& d = M5Cardputer.Display;
        SpiBus::claim(SpiBus::Owner::SubGhz);
        for (int i = 0; i < N_PRESETS; ++i) {
            g_radio.setFrequency(PRESETS[i].hz);
            selectBand(PRESETS[i].hz);
            g_radio.receiveDirectAsync();
            delay(8);
            float rssi = g_radio.getRSSI();           // dBm, ~ -110..-30
            g_radio.standby();
            int barW = (int)((rssi + 120.0f) / 90.0f * 150.0f);
            if (barW < 0) barW = 0; if (barW > 150) barW = 150;
            int y = 22 + i * 14;
            d.setTextFont(&fonts::Font0);
            d.setTextColor(UI::cDim(), UI::cBg());
            d.setTextDatum(middle_left);
            d.drawString(PRESETS[i].name, 2, y + 4);
            d.fillRect(80, y, barW, 9, UI::cAccent());
            char v[12]; snprintf(v, sizeof(v), "%d", (int)rssi);
            d.setTextColor(UI::cText(), UI::cBg());
            d.drawString(v, 232 - 18, y + 4);
        }
        UI::footer("live RSSI sweep   [`] back");
        break;
    }

    case View::Files:
        if (ev.key == Key::Up)   { UI::move(-1, _files.size(), _fileSel, _fileTop); drawFiles(); }
        if (ev.key == Key::Down) { UI::move(+1, _files.size(), _fileSel, _fileTop); drawFiles(); }
        if (ev.key == Key::Back) { _view = View::Menu; drawMenu(); }
        if (ev.key == Key::Ok && !_files.empty()) {
            bool ok = loadCapture(_files[_fileSel]);
            UI::banner(ok ? "Loaded - [ENTER] replays" : "Load failed",
                       ok ? UI::cAccent() : UI::cWarn());
            delay(900);
            _view = View::Menu; drawMenu();
        }
        break;

    case View::Receiving:
        break;  // handled synchronously inside doReceive()
    }
    return Signal::Stay;
}

}  // namespace apps
