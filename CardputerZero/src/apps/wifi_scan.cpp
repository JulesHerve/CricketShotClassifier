// =============================================================================
// wifi_scan.cpp  -  implementation of the passive Wi-Fi scanner
// =============================================================================
#include "wifi_scan.h"
#include <WiFi.h>
#include <vector>
#include <string>

using namespace core;

namespace apps {

void WifiScan::onEnter() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    scan();
}

void WifiScan::onExit() {
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
}

void WifiScan::scan() {
    UI::clear();
    UI::header("WiFi scan");
    UI::banner("Scanning...", UI::cAccent());
    _count = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    _sel = _top = 0;
    draw();
}

static const char* encStr(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN:         return "OPEN";
        case WIFI_AUTH_WEP:          return "WEP";
        case WIFI_AUTH_WPA_PSK:      return "WPA";
        case WIFI_AUTH_WPA2_PSK:     return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA3_PSK:     return "WPA3";
        default:                     return "?";
    }
}

void WifiScan::draw() {
    UI::clear();
    char hdr[20]; snprintf(hdr, sizeof(hdr), "%d APs", _count);
    UI::header("WiFi scan", hdr);
    if (_count <= 0) {
        UI::banner("No networks found", UI::cDim());
    } else {
        std::vector<std::string> rows;
        for (int i = 0; i < _count; ++i) {
            char line[64];
            String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) ssid = "<hidden>";
            snprintf(line, sizeof(line), "%.18s %ddBm c%d %s",
                     ssid.c_str(), WiFi.RSSI(i), WiFi.channel(i),
                     encStr(WiFi.encryptionType(i)));
            rows.push_back(line);
        }
        UI::list(rows, _sel, _top);
    }
    UI::footer("[;/.] scroll  [ENTER] rescan  [`] back");
}

core::Signal WifiScan::onTick(const core::KeyEvent& ev) {
    if (ev.key == Key::Back) return Signal::Back;
    if (ev.key == Key::Ok)   scan();
    if (_count > 0) {
        if (ev.key == Key::Up)   { UI::move(-1, _count, _sel, _top); draw(); }
        if (ev.key == Key::Down) { UI::move(+1, _count, _sel, _top); draw(); }
    }
    return Signal::Stay;
}

}  // namespace apps
