// =============================================================================
// main.cpp  -  CardputerZero entry point and top-level menu
//
// A Flipper-Zero-style multi-tool for the M5Stack Cardputer Adv + Cap CC1101 &
// NFC. Boots into a scrollable menu; each entry hands the whole screen to one
// App until it returns Signal::Back.
//
// Hardware + libraries are all M5Stack-official (see docs/ and platformio.ini).
// Everything here is for working with devices, remotes, tags and computers you
// own or are authorised to test. See docs/SAFETY_AND_LEGAL.md.
// =============================================================================
#include <M5Cardputer.h>
#include <M5Unified.h>

#include "core/ui.h"
#include "core/input.h"
#include "core/app.h"
#include "core/spi_bus.h"
#include "core/storage.h"

#include "apps/subghz.h"
#include "apps/nfc.h"
#include "apps/ir.h"
#include "apps/wifi_scan.h"
#include "apps/ble_scan.h"
#include "apps/badusb.h"

using namespace core;

namespace {

// The tool roster, in menu order.
apps::SubGhz   appSubGhz;
apps::Nfc      appNfc;
apps::Ir       appIr;
apps::WifiScan appWifi;
apps::BleScan  appBle;
apps::BadUsb   appBadUsb;

App* const APPS[] = { &appSubGhz, &appNfc, &appIr, &appWifi, &appBle, &appBadUsb };
constexpr int N_APPS = sizeof(APPS) / sizeof(APPS[0]);

App* g_active = nullptr;     // nullptr => in the main menu
int  g_menuSel = 0, g_menuTop = 0;

const char* batteryHint() {
    static char buf[12];
    int pct = M5.Power.getBatteryLevel();
    snprintf(buf, sizeof(buf), "%d%%", pct);
    return buf;
}

void drawMainMenu() {
    UI::clear();
    UI::header("CardputerZero", batteryHint());
    std::vector<std::string> items;
    for (int i = 0; i < N_APPS; ++i) items.push_back(APPS[i]->title());
    UI::list(items, g_menuSel, g_menuTop);
    UI::footer("[;/.] move   [ENTER] open   SD: /czero");
}

void enterApp(int idx) {
    g_active = APPS[idx];
    g_active->onEnter();
}

void leaveApp() {
    if (g_active) g_active->onExit();
    g_active = nullptr;
    SpiBus::parkAll();
    drawMainMenu();
}

}  // namespace

void setup() {
    auto m5cfg = M5.config();
    M5Cardputer.begin(m5cfg, /*enableKeyboard=*/true);
    M5Cardputer.Display.setRotation(1);           // landscape 240x135
    M5Cardputer.Display.setBrightness(180);

    Serial.begin(115200);

    // Bring up the shared SPI bus, then the SD card (optional).
    SpiBus::begin();
    Storage::begin();

    // Splash.
    UI::clear();
    UI::header("CardputerZero");
    UI::banner("multi-tool ready", UI::cAccent());
    UI::footer(Storage::isReady() ? "microSD detected" : "no microSD (screen-only)");
    delay(900);

    drawMainMenu();
}

void loop() {
    KeyEvent ev = Input::poll();

    if (g_active == nullptr) {
        // ---- main menu navigation ----
        if (ev.key == Key::Up)   { UI::move(-1, N_APPS, g_menuSel, g_menuTop); drawMainMenu(); }
        if (ev.key == Key::Down) { UI::move(+1, N_APPS, g_menuSel, g_menuTop); drawMainMenu(); }
        if (ev.key == Key::Ok)   enterApp(g_menuSel);
    } else {
        // ---- delegate to the active app ----
        if (g_active->onTick(ev) == Signal::Back) leaveApp();
    }
    delay(2);
}
