// =============================================================================
// wifi_scan.h  -  Passive Wi-Fi network scanner (read-only)
//
// Lists the access points in range with SSID, RSSI, channel and encryption.
// This only *listens*; it never connects, deauthenticates or transmits frames.
// =============================================================================
#pragma once
#include "../core/app.h"
#include "../core/ui.h"

namespace apps {

class WifiScan : public core::App {
public:
    const char* title() const override { return "WiFi scan"; }
    void   onEnter() override;
    core::Signal onTick(const core::KeyEvent& ev) override;
    void   onExit() override;
private:
    int _sel = 0, _top = 0, _count = 0;
    void scan();
    void draw();
};

}  // namespace apps
