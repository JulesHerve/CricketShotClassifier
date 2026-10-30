// =============================================================================
// badusb.h  -  USB HID keyboard macro runner
//
// Runs a small Ducky-style script from the microSD card, typing it into
// whatever computer the Cardputer is plugged into over USB. Intended for
// automating your OWN machine (launchers, text expansion, demos).
//
// Supported script commands (one per line, in /czero/badusb/*.txt):
//   REM <comment>            - ignored
//   DEFAULTDELAY <ms>        - pause inserted between every line
//   DELAY <ms>               - pause once
//   STRING <text>            - type text literally
//   STRINGLN <text>          - type text then press Enter
//   ENTER | TAB | ESC | SPACE | GUI | MENU
//   GUI <key> | CTRL <key> | ALT <key> | SHIFT <key> | CTRL-ALT <key>
//   UP | DOWN | LEFT | RIGHT
// =============================================================================
#pragma once
#include "../core/app.h"
#include "../core/ui.h"
#include <vector>
#include <string>

namespace apps {

class BadUsb : public core::App {
public:
    const char* title() const override { return "USB macro"; }
    void   onEnter() override;
    core::Signal onTick(const core::KeyEvent& ev) override;
    void   onExit() override;
private:
    enum class View { List, Confirm, Running };
    View _view = View::List;
    int  _sel = 0, _top = 0;
    std::vector<std::string> _scripts;
    void refresh();
    void draw();
    void run(const std::string& name);
};

}  // namespace apps
