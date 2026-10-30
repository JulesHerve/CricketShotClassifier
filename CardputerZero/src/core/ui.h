// =============================================================================
// ui.h  -  Tiny drawing helpers on top of M5GFX (M5Cardputer.Display)
//
// The Cardputer screen is 240x135. Everything here assumes landscape rotation
// (set once in main). The helpers are deliberately small: a title bar, a
// scrolling list, a log/console area and a status footer. Apps compose these.
// =============================================================================
#pragma once
#include <M5Cardputer.h>
#include <vector>
#include <string>

namespace core {

class UI {
public:
    static constexpr int W = 240;
    static constexpr int H = 135;

    // Colour palette (RGB565 via M5GFX helpers).
    static uint16_t cAccent() { return M5Cardputer.Display.color565(0x27, 0xE0, 0x8A); }
    static uint16_t cBg()     { return M5Cardputer.Display.color565(0x0A, 0x0E, 0x12); }
    static uint16_t cBar()    { return M5Cardputer.Display.color565(0x14, 0x1C, 0x24); }
    static uint16_t cText()   { return TFT_WHITE; }
    static uint16_t cDim()    { return M5Cardputer.Display.color565(0x7A, 0x88, 0x92); }
    static uint16_t cWarn()   { return M5Cardputer.Display.color565(0xF0, 0xC0, 0x30); }

    static void clear() {
        auto& d = M5Cardputer.Display;
        d.fillScreen(cBg());
    }

    // Title bar with an optional right-aligned hint (e.g. battery, band).
    static void header(const char* title, const char* right = nullptr) {
        auto& d = M5Cardputer.Display;
        d.fillRect(0, 0, W, 18, cBar());
        d.setTextDatum(middle_left);
        d.setTextColor(cAccent(), cBar());
        d.setTextFont(&fonts::Font2);
        d.drawString(title, 6, 9);
        if (right) {
            d.setTextDatum(middle_right);
            d.setTextColor(cDim(), cBar());
            d.drawString(right, W - 6, 9);
        }
    }

    // Footer hint line (controls legend).
    static void footer(const char* hint) {
        auto& d = M5Cardputer.Display;
        d.fillRect(0, H - 14, W, 14, cBar());
        d.setTextDatum(middle_left);
        d.setTextColor(cDim(), cBar());
        d.setTextFont(&fonts::Font0);
        d.drawString(hint, 6, H - 7);
    }

    // Draw a scrolling single-select list. `top` is the first visible row;
    // `sel` is the highlighted index. Returns nothing; caller owns state.
    static void list(const std::vector<std::string>& items, int sel, int top) {
        auto& d = M5Cardputer.Display;
        const int rowH = 16, x = 0, y0 = 20, rows = 6;
        d.fillRect(0, 18, W, H - 18 - 14, cBg());
        d.setTextFont(&fonts::Font2);
        d.setTextDatum(middle_left);
        for (int i = 0; i < rows; ++i) {
            int idx = top + i;
            if (idx >= (int)items.size()) break;
            int y = y0 + i * rowH;
            bool on = (idx == sel);
            if (on) d.fillRect(x, y - 1, W, rowH, cAccent());
            d.setTextColor(on ? cBg() : cText(), on ? cAccent() : cBg());
            d.drawString(items[idx].c_str(), 8, y + rowH / 2 - 1);
        }
    }

    // One-shot centred message (used for "working...", results, errors).
    static void banner(const char* msg, uint16_t fg) {
        auto& d = M5Cardputer.Display;
        d.fillRect(0, 18, W, H - 18 - 14, cBg());
        d.setTextFont(&fonts::Font2);
        d.setTextDatum(middle_center);
        d.setTextColor(fg, cBg());
        d.drawString(msg, W / 2, H / 2);
    }

    // Helper: compute the new (sel, top) after a vertical move within a list.
    static void move(int delta, int count, int& sel, int& top, int rows = 6) {
        if (count == 0) return;
        sel = (sel + delta + count) % count;
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
    }
};

// A scrolling text console for the radio / scanner apps.
class Console {
public:
    void reset() { _lines.clear(); }
    void line(const std::string& s) {
        _lines.push_back(s);
        if (_lines.size() > 64) _lines.erase(_lines.begin());
        draw();
    }
    void draw() {
        auto& d = M5Cardputer.Display;
        const int y0 = 20, rowH = 12, rows = (UI::H - 20 - 14) / rowH;
        d.fillRect(0, 18, UI::W, UI::H - 18 - 14, UI::cBg());
        d.setTextFont(&fonts::Font0);
        d.setTextDatum(top_left);
        d.setTextColor(UI::cText(), UI::cBg());
        int start = (int)_lines.size() > rows ? _lines.size() - rows : 0;
        for (int i = start; i < (int)_lines.size(); ++i)
            d.drawString(_lines[i].c_str(), 4, y0 + (i - start) * rowH);
    }
private:
    std::vector<std::string> _lines;
};

}  // namespace core
