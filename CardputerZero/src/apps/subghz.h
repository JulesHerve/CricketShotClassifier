// =============================================================================
// subghz.h  -  Sub-GHz tool (TI CC1101 on the Cap)
//
// Capabilities (all for YOUR OWN remotes/sensors):
//   * Receive  : capture a raw OOK burst as edge timings
//   * Replay   : re-transmit the last capture, or one loaded from SD
//   * Save/Load: store captures as .sub text files on the microSD card
//   * Analyze  : sweep RSSI across preset frequencies to find an active one
//
// Only the three bands the Cap's RF matching network passes are offered
// (315 / 433 / 868-915 MHz). Band selection drives RF_SW0 (host GPIO) and
// RF_SW1 (the CC1101's GDO2, set over SPI via IOCFG2).
// =============================================================================
#pragma once
#include "../core/app.h"
#include "../core/ui.h"
#include <vector>
#include <string>

namespace apps {

class SubGhz : public core::App {
public:
    const char* title() const override { return "Sub-GHz"; }
    void   onEnter() override;
    core::Signal onTick(const core::KeyEvent& ev) override;
    void   onExit() override;

private:
    enum class View { Menu, Freq, Receiving, Result, Analyzer, Files };
    View _view = View::Menu;

    int _menuSel = 0, _menuTop = 0;
    int _freqSel = 0;           // index into the frequency preset table
    int _fileSel = 0, _fileTop = 0;
    core::Console _con;
    std::vector<std::string> _files;

    bool  _radioOk = false;

    void drawMenu();
    void drawFreq();
    void drawAnalyzer();
    void drawFiles();

    bool ensureRadio();         // lazy CC1101 init, returns _radioOk
    void doReceive();           // blocking-ish capture with on-screen feedback
    void doReplay();            // transmit the capture currently in RAM
    bool saveCapture();         // write RAM capture to a new .sub file
    bool loadCapture(const std::string& name);
    void refreshFiles();
};

}  // namespace apps
