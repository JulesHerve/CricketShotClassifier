// =============================================================================
// ble_scan.h  -  Passive Bluetooth LE device scanner (read-only)
//
// Lists nearby BLE advertisers with name (if any), address and RSSI. Listen
// only: it never pairs or connects.
// =============================================================================
#pragma once
#include "../core/app.h"
#include "../core/ui.h"
#include <vector>
#include <string>

namespace apps {

class BleScan : public core::App {
public:
    const char* title() const override { return "BLE scan"; }
    void   onEnter() override;
    core::Signal onTick(const core::KeyEvent& ev) override;
    void   onExit() override;
private:
    int _sel = 0, _top = 0;
    std::vector<std::string> _rows;
    void scan();
    void draw();
};

}  // namespace apps
