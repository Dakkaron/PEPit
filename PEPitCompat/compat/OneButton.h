/*
 * PEPitCompat — OneButton Shim (no-op stub)
 * 
 * Physical button debouncing is not needed on Linux.
 * All methods are no-op, accept nullptr callbacks.
 */

#ifndef ONEBUTTON_H
#define ONEBUTTON_H

#include <cstdint>

class OneButton {
public:
    OneButton(uint8_t pin, bool invert = true, uint32_t pullup = true) {}
    void attachClick(void (*fn)(void)) {} // OK, no-op (accepts nullptr)
    void attachDoubleClick(void (*fn)(void)) {} // OK, no-op
    void attachLongPressStop(void (*fn)(void)) {} // OK, no-op (accepts nullptr)
    void attachLongPressStart(void (*fn)(void)) {} // OK, no-op
    void tick() {} // OK, no-op: no button state polling on Linux
};

#endif // ONEBUTTON_H
