// =============================================================================
// badusb.cpp  -  implementation of the USB HID macro runner
// =============================================================================
#include "badusb.h"
#include "../core/storage.h"
#include "../config.h"

#include <USB.h>
#include <USBHIDKeyboard.h>
#include <sstream>
#include <cctype>

using namespace core;

namespace {
USBHIDKeyboard g_kbd;
bool g_usbUp = false;
uint32_t g_defaultDelay = 0;

void usbBegin() {
    if (g_usbUp) return;
    g_kbd.begin();
    USB.begin();
    g_usbUp = true;
    delay(600);     // let the host enumerate the HID interface
}

// Map a single key token to a HID keycode for modifier combos.
uint8_t tokenToKey(const std::string& t) {
    if (t == "ENTER")  return KEY_RETURN;
    if (t == "TAB")    return KEY_TAB;
    if (t == "ESC")    return KEY_ESC;
    if (t == "SPACE")  return ' ';
    if (t == "UP")     return KEY_UP_ARROW;
    if (t == "DOWN")   return KEY_DOWN_ARROW;
    if (t == "LEFT")   return KEY_LEFT_ARROW;
    if (t == "RIGHT")  return KEY_RIGHT_ARROW;
    if (t == "DELETE") return KEY_DELETE;
    if (t == "MENU")   return 0xED;      // application / menu key
    if (t.size() == 1) return (uint8_t)tolower(t[0]);
    return 0;
}

void comboPress(uint8_t mod, const std::string& keyTok) {
    g_kbd.press(mod);
    uint8_t k = tokenToKey(keyTok);
    if (k) g_kbd.press(k);
    delay(30);
    g_kbd.releaseAll();
}

// Execute one script line. Returns false to abort (unused for now).
void runLine(const std::string& raw) {
    // strip CR and leading spaces
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    size_t s = line.find_first_not_of(" \t");
    if (s == std::string::npos) return;
    line = line.substr(s);
    if (line.empty()) return;

    std::istringstream iss(line);
    std::string cmd; iss >> cmd;
    std::string rest; std::getline(iss, rest);
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

    if      (cmd == "REM")          { /* comment */ }
    else if (cmd == "DEFAULTDELAY") { g_defaultDelay = strtoul(rest.c_str(), nullptr, 10); }
    else if (cmd == "DELAY")        { delay(strtoul(rest.c_str(), nullptr, 10)); }
    else if (cmd == "STRING")       { g_kbd.print(rest.c_str()); }
    else if (cmd == "STRINGLN")     { g_kbd.print(rest.c_str()); g_kbd.write(KEY_RETURN); }
    else if (cmd == "ENTER")        { g_kbd.write(KEY_RETURN); }
    else if (cmd == "TAB")          { g_kbd.write(KEY_TAB); }
    else if (cmd == "ESC")          { g_kbd.write(KEY_ESC); }
    else if (cmd == "SPACE")        { g_kbd.write(' '); }
    else if (cmd == "UP")           { g_kbd.write(KEY_UP_ARROW); }
    else if (cmd == "DOWN")         { g_kbd.write(KEY_DOWN_ARROW); }
    else if (cmd == "LEFT")         { g_kbd.write(KEY_LEFT_ARROW); }
    else if (cmd == "RIGHT")        { g_kbd.write(KEY_RIGHT_ARROW); }
    else if (cmd == "GUI" || cmd == "WINDOWS") { comboPress(KEY_LEFT_GUI, rest); }
    else if (cmd == "CTRL" || cmd == "CONTROL"){ comboPress(KEY_LEFT_CTRL, rest); }
    else if (cmd == "ALT")          { comboPress(KEY_LEFT_ALT, rest); }
    else if (cmd == "SHIFT")        { comboPress(KEY_LEFT_SHIFT, rest); }
    else if (cmd == "CTRL-ALT")     { g_kbd.press(KEY_LEFT_CTRL); comboPress(KEY_LEFT_ALT, rest); }
    // Unknown commands are ignored on purpose (forward-compatible scripts).

    if (g_defaultDelay) delay(g_defaultDelay);
}

}  // namespace

namespace apps {

void BadUsb::onEnter() { _view = View::List; refresh(); draw(); }
void BadUsb::onExit()  {}

void BadUsb::refresh() {
    _scripts = Storage::listDir(cfg::DIR_BADUSB);
    _sel = _top = 0;
}

void BadUsb::draw() {
    UI::clear();
    if (_view == View::List) {
        UI::header("USB macro", Storage::isReady() ? "SD ok" : "no SD");
        if (_scripts.empty())
            UI::banner(Storage::isReady() ? "Put .txt scripts in\n/czero/badusb"
                                          : "No SD card", UI::cDim());
        else
            UI::list(_scripts, _sel, _top);
        UI::footer("[ENTER] run  [`] back");
    } else if (_view == View::Confirm) {
        UI::header("Run on THIS computer?");
        UI::banner("This types into the PC\nthe Cardputer is plugged in.\n\n[ENTER] confirm  [`] cancel",
                   UI::cWarn());
        UI::footer("runs on your own machine only");
    }
}

void BadUsb::run(const std::string& name) {
    _view = View::Running;
    UI::clear();
    UI::header("Running macro");
    UI::banner("Starting USB HID...", UI::cWarn());

    std::string txt;
    if (!Storage::read(std::string(cfg::DIR_BADUSB) + "/" + name, txt)) {
        UI::banner("Read failed", UI::cWarn()); delay(1000);
        _view = View::List; draw(); return;
    }
    usbBegin();
    g_defaultDelay = 0;

    core::Console con;
    UI::clear(); UI::header("Running", name.c_str());
    con.line("typing script...");

    std::istringstream iss(txt);
    std::string line; int n = 0;
    while (std::getline(iss, line)) {
        runLine(line);
        if ((++n % 5) == 0) { con.line(std::string("line ") + std::to_string(n)); }
        // allow an emergency stop from the Cardputer keyboard
        auto ev = Input::poll();
        if (ev.key == Key::Back) { con.line("ABORTED"); break; }
    }
    g_kbd.releaseAll();
    UI::banner("Macro finished", UI::cAccent());
    delay(900);
    _view = View::List; draw();
}

core::Signal BadUsb::onTick(const core::KeyEvent& ev) {
    switch (_view) {
    case View::List:
        if (ev.key == Key::Back) return Signal::Back;
        if (!_scripts.empty()) {
            if (ev.key == Key::Up)   { UI::move(-1, _scripts.size(), _sel, _top); draw(); }
            if (ev.key == Key::Down) { UI::move(+1, _scripts.size(), _sel, _top); draw(); }
            if (ev.key == Key::Ok)   { _view = View::Confirm; draw(); }
        }
        break;
    case View::Confirm:
        if (ev.key == Key::Back) { _view = View::List; draw(); }
        if (ev.key == Key::Ok)   { run(_scripts[_sel]); }
        break;
    case View::Running:
        break;
    }
    return Signal::Stay;
}

}  // namespace apps
