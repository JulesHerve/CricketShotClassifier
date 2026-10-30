// =============================================================================
// ir.cpp  -  implementation of the Infrared tool
//
// IRremote is instantiated in this single translation unit (the library's
// IrReceiver / IrSender globals must be defined exactly once).
// =============================================================================
#include "ir.h"
#include "../core/storage.h"
#include "../config.h"

// IRremote build knobs are set as -D flags in platformio.ini (SEND_PWM_BY_TIMER,
// RAW_BUFFER_LENGTH). Include the implementation header exactly once, here.
#include <IRremote.hpp>

using namespace core;

namespace {

// A single learned button. Either a decoded protocol frame, or raw timing.
struct Learned {
    bool            valid = false;
    bool            raw   = false;
    decode_type_t   proto = UNKNOWN;
    uint16_t        address = 0;
    uint16_t        command = 0;
    uint8_t         bits = 0;
    uint16_t        rawLen = 0;
    uint16_t        rawBuf[RAW_BUFFER_LENGTH];
};
Learned g_learned;

bool g_rxStarted = false;
bool g_txStarted = false;

void startRx() {
    if (g_rxStarted) return;
    IrReceiver.begin(cfg::PIN_IR_RX, ENABLE_LED_FEEDBACK);
    g_rxStarted = true;
}
void stopRx() {
    if (!g_rxStarted) return;
    IrReceiver.stop();
    g_rxStarted = false;
}
void startTx() {
    if (g_txStarted) return;
    IrSender.begin(cfg::PIN_IR_TX, DISABLE_LED_FEEDBACK);
    g_txStarted = true;
}

// Universal TV-power sweep. These are well-known, widely-used power commands
// across the big IR protocol families. Not every TV responds; the sweep just
// fires each one in turn (handy only for a TV you own).
void sweepPower() {
    startTx();
    // Sony SIRC (address 1, command 21 = power), sent as 12/15/20-bit variants.
    IrSender.sendSony(1, 21, 2, 12);
    IrSender.sendSony(1, 21, 2, 15);
    IrSender.sendSony(1, 21, 2, 20);
    delay(40);
    // RC5 standby (system 0, command 12) - many European sets.
    IrSender.sendRC5(0, 12, 2);
    delay(40);
    // Samsung power.
    IrSender.sendSamsung(0x0707, 0x02, 2);
    delay(40);
    // NEC generic power (address 0x00, command 0x02) - common on cheap sets.
    IrSender.sendNEC(0x00, 0x02, 2);
}

}  // namespace

namespace apps {

void Ir::onEnter() { _view = View::Menu; _menuSel = _menuTop = 0; drawMenu(); }
void Ir::onExit()  { stopRx(); }

void Ir::drawMenu() {
    UI::clear();
    UI::header("Infrared", g_learned.valid ? "1 learned" : "empty");
    std::vector<std::string> items = {
        "Learn a button",
        "Replay learned",
        "Universal TV power",
        "Load from SD",
    };
    UI::list(items, _menuSel, _menuTop);
    UI::footer("[;/.] move  [ENTER] select  [`] back");
}

void Ir::drawFiles() {
    UI::clear();
    UI::header("Load IR button");
    if (_files.empty()) UI::banner(Storage::isReady() ? "No IR files" : "No SD card", UI::cDim());
    else UI::list(_files, _fileSel, _fileTop);
    UI::footer("[ENTER] load  [`] back");
}

void Ir::learn() {
    startTx();          // ensure sender pin is configured but idle
    startRx();
    UI::clear();
    UI::header("Learn");
    UI::banner("Point remote at the\nMini IR Unit, press a key", UI::cAccent());
    UI::footer("[`] cancel");

    uint32_t start = millis();
    while (millis() - start < 10000) {
        if (IrReceiver.decode()) {
            auto& d = IrReceiver.decodedIRData;
            g_learned = Learned{};
            g_learned.valid = true;
            if (d.protocol != UNKNOWN && !(d.flags & IRDATA_FLAGS_IS_REPEAT)) {
                g_learned.raw     = false;
                g_learned.proto   = d.protocol;
                g_learned.address = d.address;
                g_learned.command = d.command;
                g_learned.bits    = d.numberOfBits;
            } else {
                // Fall back to raw timing (decode unknown or a repeat frame).
                // In IRremote 4.x the raw buffer lives in irparams.rawbuf as
                // 50 us tick counts; entry [0] is an empty backwards-compat
                // slot, so real mark/space durations start at index 1.
                g_learned.raw = true;
                uint16_t rl = IrReceiver.decodedIRData.rawlen;
                uint16_t n  = (rl > 1) ? (uint16_t)(rl - 1) : 0;
                if (n > 255) n = 255;    // IrSender.sendRaw length is 8-bit
                g_learned.rawLen = n;
                for (uint16_t i = 0; i < n; ++i)
                    g_learned.rawBuf[i] =
                        (uint16_t)IrReceiver.irparams.rawbuf[i + 1] * MICROS_PER_TICK;
            }
            M5Cardputer.Speaker.tone(4000, 20);
            char l[48];
            if (g_learned.raw)
                snprintf(l, sizeof(l), "Learned RAW (%u marks)", g_learned.rawLen);
            else
                snprintf(l, sizeof(l), "Learned %s a=%X c=%X",
                         getProtocolString(g_learned.proto), g_learned.address, g_learned.command);
            UI::banner(l, UI::cAccent());
            IrReceiver.resume();
            delay(1100);
            stopRx();
            drawMenu();
            return;
        }
        auto ev = Input::poll();
        if (ev.key == Key::Back) break;
        delay(2);
    }
    stopRx();
    UI::banner("Nothing learned", UI::cWarn());
    delay(800);
    drawMenu();
}

void Ir::replay() {
    if (!g_learned.valid) { UI::banner("Learn a button first", UI::cWarn()); delay(800); drawMenu(); return; }
    stopRx();
    startTx();
    UI::banner("Sending...", UI::cWarn());
    if (g_learned.raw) {
        IrSender.sendRaw(g_learned.rawBuf, g_learned.rawLen, 38);  // 38 kHz carrier
    } else {
        // Re-emit the decoded frame. write() dispatches on the stored protocol.
        IRData d{};
        d.protocol     = g_learned.proto;
        d.address      = g_learned.address;
        d.command      = g_learned.command;
        d.numberOfBits = g_learned.bits;
        d.flags        = IRDATA_FLAGS_EMPTY;
        IrSender.write(&d, 2);
    }
    UI::banner("Done", UI::cAccent());
    delay(600);
    drawMenu();
}

void Ir::universal() {
    stopRx();
    UI::clear();
    UI::header("Universal TV power");
    UI::banner("Firing common power\ncodes at your TV...", UI::cWarn());
    sweepPower();
    UI::banner("Sweep done", UI::cAccent());
    delay(800);
    _view = View::Menu;
    drawMenu();
}

bool Ir::save() {
    if (!g_learned.valid || !Storage::isReady()) return false;
    std::string path = Storage::uniqueName(cfg::DIR_IR, "btn", "ir");
    std::string body = "CardputerZero IR\n";
    if (g_learned.raw) {
        body += "type: raw\nfreq: 38000\ndata:";
        for (uint16_t i = 0; i < g_learned.rawLen; ++i) { body += ' '; body += std::to_string(g_learned.rawBuf[i]); }
        body += "\n";
    } else {
        body += "type: parsed\nprotocol: " + std::string(getProtocolString(g_learned.proto)) + "\n";
        char l[64];
        snprintf(l, sizeof(l), "address: %u\ncommand: %u\nbits: %u\n",
                 g_learned.address, g_learned.command, g_learned.bits);
        body += l;
    }
    return Storage::write(path, body);
}

bool Ir::load(const std::string& name) {
    std::string txt;
    if (!Storage::read(std::string(cfg::DIR_IR) + "/" + name, txt)) return false;
    g_learned = Learned{};
    if (txt.find("type: raw") != std::string::npos) {
        g_learned.raw = true;
        size_t dp = txt.find("data:");
        if (dp == std::string::npos) return false;
        const char* p = txt.c_str() + dp + 5;
        g_learned.rawLen = 0;
        while (*p && g_learned.rawLen < RAW_BUFFER_LENGTH) {
            char* end; long v = strtol(p, &end, 10);
            if (end == p) break;
            if (v > 0) g_learned.rawBuf[g_learned.rawLen++] = (uint16_t)v;
            p = end;
        }
        g_learned.valid = g_learned.rawLen > 0;
    } else {
        g_learned.raw = false;
        size_t ap = txt.find("address:"), cp = txt.find("command:"), bp = txt.find("bits:");
        if (ap != std::string::npos) g_learned.address = atoi(txt.c_str() + ap + 8);
        if (cp != std::string::npos) g_learned.command = atoi(txt.c_str() + cp + 8);
        if (bp != std::string::npos) g_learned.bits    = atoi(txt.c_str() + bp + 5);
        // Protocol defaults to NEC if the saved name is not re-matched; most
        // parsed saves are NEC-family on consumer gear.
        g_learned.proto = NEC;
        g_learned.valid = true;
    }
    return g_learned.valid;
}

void Ir::refreshFiles() { _files = Storage::listDir(cfg::DIR_IR); _fileSel = _fileTop = 0; }

core::Signal Ir::onTick(const core::KeyEvent& ev) {
    switch (_view) {
    case View::Menu:
        if (ev.key == Key::Up)   { UI::move(-1, 4, _menuSel, _menuTop); drawMenu(); }
        if (ev.key == Key::Down) { UI::move(+1, 4, _menuSel, _menuTop); drawMenu(); }
        if (ev.key == Key::Back) return Signal::Back;
        if (ev.key == Key::Char && (ev.ch == 's' || ev.ch == 'S')) {
            bool ok = save();
            UI::banner(ok ? "Saved to SD" : "Save failed", ok ? UI::cAccent() : UI::cWarn());
            delay(800); drawMenu();
        }
        if (ev.key == Key::Ok) {
            switch (_menuSel) {
            case 0: learn(); break;
            case 1: replay(); break;
            case 2: universal(); break;
            case 3: refreshFiles(); _view = View::Files; drawFiles(); break;
            }
        }
        break;
    case View::Files:
        if (ev.key == Key::Up)   { UI::move(-1, _files.size(), _fileSel, _fileTop); drawFiles(); }
        if (ev.key == Key::Down) { UI::move(+1, _files.size(), _fileSel, _fileTop); drawFiles(); }
        if (ev.key == Key::Back) { _view = View::Menu; drawMenu(); }
        if (ev.key == Key::Ok && !_files.empty()) {
            bool ok = load(_files[_fileSel]);
            UI::banner(ok ? "Loaded - [ENTER] replays" : "Load failed", ok ? UI::cAccent() : UI::cWarn());
            delay(900); _view = View::Menu; drawMenu();
        }
        break;
    default: _view = View::Menu; drawMenu(); break;
    }
    return Signal::Stay;
}

}  // namespace apps
