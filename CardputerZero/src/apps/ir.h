// =============================================================================
// ir.h  -  Infrared tool (built-in LED + M5Stack Mini IR Unit)
//
// Capabilities (for YOUR OWN appliances):
//   * Learn       : capture one button from a real remote (needs the Grove
//                   Mini IR Unit's receiver)
//   * Replay      : re-send the learned button (built-in LED or Grove emitter)
//   * Universal   : sweep common TV power codes across several protocols
//   * Save / Load : store learned buttons as text files on the microSD card
// =============================================================================
#pragma once
#include "../core/app.h"
#include "../core/ui.h"
#include <vector>
#include <string>

namespace apps {

class Ir : public core::App {
public:
    const char* title() const override { return "Infrared"; }
    void   onEnter() override;
    core::Signal onTick(const core::KeyEvent& ev) override;
    void   onExit() override;

private:
    enum class View { Menu, Learn, Universal, Files };
    View _view = View::Menu;
    int  _menuSel = 0, _menuTop = 0;
    int  _fileSel = 0, _fileTop = 0;
    std::vector<std::string> _files;

    void drawMenu();
    void drawFiles();
    void learn();
    void replay();
    void universal();
    bool save();
    bool load(const std::string& name);
    void refreshFiles();
};

}  // namespace apps
