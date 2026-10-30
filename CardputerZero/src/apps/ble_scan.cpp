// =============================================================================
// ble_scan.cpp  -  implementation of the passive BLE scanner
// =============================================================================
#include "ble_scan.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

using namespace core;

namespace apps {

void BleScan::onEnter() {
    BLEDevice::init("");
    scan();
}

void BleScan::onExit() {
    BLEDevice::deinit(true);
}

void BleScan::scan() {
    UI::clear();
    UI::header("BLE scan");
    UI::banner("Scanning 5s...", UI::cAccent());

    BLEScan* s = BLEDevice::getScan();
    s->setActiveScan(true);
    s->setInterval(100);
    s->setWindow(99);
    BLEScanResults* results = s->start(5, false);

    _rows.clear();
    int n = results ? results->getCount() : 0;
    for (int i = 0; i < n; ++i) {
        BLEAdvertisedDevice dev = results->getDevice(i);
        char line[80];
        // getName()/toString() return Arduino String on arduino-esp32 3.x.
        String name = dev.haveName() ? dev.getName() : String("<no name>");
        snprintf(line, sizeof(line), "%.16s %ddBm %.17s",
                 name.c_str(), dev.getRSSI(), dev.getAddress().toString().c_str());
        _rows.push_back(line);
    }
    s->clearResults();
    _sel = _top = 0;
    draw();
}

void BleScan::draw() {
    UI::clear();
    char hdr[20]; snprintf(hdr, sizeof(hdr), "%d dev", (int)_rows.size());
    UI::header("BLE scan", hdr);
    if (_rows.empty()) UI::banner("No devices found", UI::cDim());
    else UI::list(_rows, _sel, _top);
    UI::footer("[;/.] scroll  [ENTER] rescan  [`] back");
}

core::Signal BleScan::onTick(const core::KeyEvent& ev) {
    if (ev.key == Key::Back) return Signal::Back;
    if (ev.key == Key::Ok)   scan();
    if (!_rows.empty()) {
        if (ev.key == Key::Up)   { UI::move(-1, _rows.size(), _sel, _top); draw(); }
        if (ev.key == Key::Down) { UI::move(+1, _rows.size(), _sel, _top); draw(); }
    }
    return Signal::Stay;
}

}  // namespace apps
