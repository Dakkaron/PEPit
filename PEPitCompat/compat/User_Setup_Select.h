/* PEPitCompat — User_Setup_Select Shim */
/* Intercepts the real library's #include <User_Setup_Select.h> and loads our setup instead. */

#ifndef USER_SETUP_SELECT_H
#define USER_SETUP_SELECT_H

// Include our custom setup (defines driver type, fonts, pins, etc.)
#include "User_Setup.h"

// Load the driver-specific command/rotation defines.
// The real User_Setup_Select.h has a big #elif chain for this — we replicate it here
// so the driver constants (TFT_SWRST, TFT_MADCTL, ST7789_*, ILI9341_*, etc.) are defined.
#if defined (ILI9341_DRIVER)
    #include "TFT_Drivers/ILI9341_Defines.h"
    #define  TFT_DRIVER 0x9341
#elif defined (ST7735_DRIVER)
    #include "TFT_Drivers/ST7735_Defines.h"
    #define  TFT_DRIVER 0x7735
#elif defined (ILI9163_DRIVER)
    #include "TFT_Drivers/ILI9163_Defines.h"
    #define  TFT_DRIVER 0x9163
#elif defined (S6D02A1_DRIVER)
    #include "TFT_Drivers/S6D02A1_Defines.h"
    #define  TFT_DRIVER 0x02A1
#elif defined (ST7796_DRIVER)
    #include "TFT_Drivers/ST7796_Defines.h"
    #define  TFT_DRIVER 0x7796
#elif defined (ILI9486_DRIVER)
    #include "TFT_Drivers/ILI9486_Defines.h"
    #define  TFT_DRIVER 0x9486
#elif defined (ILI9481_DRIVER)
    #include "TFT_Drivers/ILI9481_Defines.h"
    #define  TFT_DRIVER 0x9481
#elif defined (ILI9488_DRIVER)
    #include "TFT_Drivers/ILI9488_Defines.h"
    #define  TFT_DRIVER 0x9488
#elif defined (HX8357D_DRIVER)
    #include "TFT_Drivers/HX8357D_Defines.h"
    #define  TFT_DRIVER 0x8357
#elif defined (EPD_DRIVER)
    #include "TFT_Drivers/EPD_Defines.h"
    #define  TFT_DRIVER 0xE9D
#elif defined (ST7789_DRIVER)
    #include "TFT_Drivers/ST7789_Defines.h"
    #define  TFT_DRIVER 0x7789
#elif defined (R61581_DRIVER)
    #include "TFT_Drivers/R61581_Defines.h"
    #define  TFT_DRIVER 0x61581
#elif defined (ST7789_2_DRIVER)
    #include "TFT_Drivers/ST7789_2_Defines.h"
    #define  TFT_DRIVER 0x778B
#elif defined (RM68140_DRIVER)
    #include "TFT_Drivers/RM68140_Defines.h"
    #define  TFT_DRIVER 0x6814
#elif defined (SSD1351_DRIVER)
    #include "TFT_Drivers/SSD1351_Defines.h"
    #define  TFT_DRIVER 0x1351
#elif defined (SSD1963_480_DRIVER)
    #include "TFT_Drivers/SSD1963_Defines.h"
    #define  TFT_DRIVER 0x1963
#elif defined (SSD1963_800_DRIVER)
    #include "TFT_Drivers/SSD1963_Defines.h"
    #define  TFT_DRIVER 0x1963
#elif defined (SSD1963_800ALT_DRIVER)
    #include "TFT_Drivers/SSD1963_Defines.h"
    #define  TFT_DRIVER 0x1963
#elif defined (SSD1963_800WT_DRIVER)
    #include "TFT_Drivers/SSD1963_Defines.h"
    #define  TFT_DRIVER 0x1963
#elif defined (GC9A01_DRIVER)
    #include "TFT_Drivers/GC9A01_Defines.h"
    #define  TFT_DRIVER 0x9A01
#elif defined (ILI9225_DRIVER)
    #include "TFT_Drivers/ILI9225_Defines.h"
    #define  TFT_DRIVER 0x9225
#elif defined (RM68120_DRIVER)
    #include "TFT_Drivers/RM68120_Defines.h"
    #define  TFT_DRIVER 0x6812
#elif defined (HX8357B_DRIVER)
    #include "TFT_Drivers/HX8357B_Defines.h"
    #define  TFT_DRIVER 0x835B
#elif defined (HX8357C_DRIVER)
    #include "TFT_Drivers/HX8357C_Defines.h"
    #define  TFT_DRIVER 0x835C

// No driver explicitly defined — default to ILI9341
#else
    #include "TFT_Drivers/ILI9341_Defines.h"
    #define  TFT_DRIVER 0x9341
#endif

// Mark setup as loaded so the real User_Setup_Select.h doesn't try to pick one
#define USER_SETUP_LOADED

#endif // USER_SETUP_SELECT_H
