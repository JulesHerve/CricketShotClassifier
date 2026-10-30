// =============================================================================
// app.h  -  Base class for every CardputerZero screen/tool
//
// An App owns the full screen while it runs. The main loop calls:
//   onEnter()           once, when the app becomes active
//   onTick()  repeatedly, passing the latest key event; returns Signal
//   onExit()            once, when leaving
//
// Returning Signal::Back pops to the previous app (the main menu). Keeping the
// contract this small means each tool is a self-contained file.
// =============================================================================
#pragma once
#include "input.h"

namespace core {

enum class Signal { Stay, Back };

class App {
public:
    virtual ~App() = default;
    virtual const char* title() const = 0;
    virtual void   onEnter() {}
    virtual Signal onTick(const KeyEvent& ev) = 0;
    virtual void   onExit() {}
};

}  // namespace core
