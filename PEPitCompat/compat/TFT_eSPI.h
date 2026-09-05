/*
 * PEPitCompat — TFT_eSPI Shim (uses real library with SDL2 processor)
 * 
 * For original source files that do #include <TFT_eSPI.h>.
 * The real library is configured via:
 *   - compat/User_Setup_Select.h → loads our User_Setup.h (TFT_WIDTH, pins, etc.)
 *   - CMake PROCESSOR_INCLUDE → uses our SDL2 processor
 * 
 * This shim just re-includes the real library and adds SDL helpers.
 */

#ifndef TFT_ESPI_H
#define TFT_ESPI_H

// Skip past this compat/ shim and find the real library in third_party/TFT_eSPI
#include_next "TFT_eSPI.h"

// ============================================================
// SDL integration helpers (declared in our custom processor)
// ============================================================

void setSDLWindow(void* window, void* renderer);
void flushSDL(void);

// Convenience: expose the display buffer for direct access
inline uint16_t* getDisplayBuffer() {
    extern uint16_t* sdl_tft_buffer;
    return sdl_tft_buffer;
}

#endif // TFT_ESPI_H
