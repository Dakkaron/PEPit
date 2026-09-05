/* PEPitCompat — ESP Power Management Shim (esp_pm.h stub, no real PM on Linux) */
#ifndef ESP_PM_H
#define ESP_PM_H

// Power management is not functional on Linux. CONFIG_PM_ENABLE is intentionally
// left undefined so #if CONFIG_PM_ENABLE blocks in the source are excluded.

#endif // ESP_PM_H
