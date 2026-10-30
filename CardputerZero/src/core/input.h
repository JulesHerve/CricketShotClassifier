// =============================================================================
// input.h  -  Keyboard abstraction over M5Cardputer's keyboard
//
// Turns the raw key matrix into a small set of logical events the apps use:
// Up/Down/Left/Right/Ok/Back plus the raw typed character. Works on both the
// original Cardputer (matrix) and the Cardputer Adv (TCA8418) because
// M5Cardputer.Keyboard already abstracts the two.
// =============================================================================
#pragma once
#include <M5Cardputer.h>

namespace core {

enum class Key { None, Up, Down, Left, Right, Ok, Back, Char };

struct KeyEvent {
    Key  key = Key::None;
    char ch  = 0;      // valid when key == Key::Char
};

class Input {
public:
    // Poll once per loop. Returns an edge-triggered event (one per physical
    // press); holding a key does not autorepeat, which keeps menus sane.
    static KeyEvent poll() {
        M5Cardputer.update();
        KeyEvent ev;
        if (!M5Cardputer.Keyboard.isChange()) return ev;
        if (!M5Cardputer.Keyboard.isPressed()) return ev;  // only act on key-down

        auto st = M5Cardputer.Keyboard.keysState();

        // Navigation: the Cardputer prints ';' '.' ',' '/' for the arrow cluster,
        // and the library also exposes dedicated up/down/left/right flags.
        if (st.up)        { ev.key = Key::Up;    return ev; }
        if (st.down)      { ev.key = Key::Down;  return ev; }
        if (st.left)      { ev.key = Key::Left;  return ev; }
        if (st.right)     { ev.key = Key::Right; return ev; }
        if (st.enter)     { ev.key = Key::Ok;    return ev; }
        if (st.esc || st.del || st.backspace) { ev.key = Key::Back; return ev; }

        // `-mode fallback for the arrow keys printed as characters.
        for (char c : st.word) {
            switch (c) {
                case ';': ev.key = Key::Up;    return ev;
                case '.': ev.key = Key::Down;  return ev;
                case ',': ev.key = Key::Left;  return ev;
                case '/': ev.key = Key::Right; return ev;
                case '`': ev.key = Key::Back;  return ev;
                default:  ev.key = Key::Char; ev.ch = c; return ev;
            }
        }
        return ev;
    }
};

}  // namespace core
