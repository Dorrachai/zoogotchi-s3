#pragma once
#include <stdint.h>

// =============================================================================
// Onboard Buttons Only (Zero External Soldered Buttons!)
//   PWR  = PIN_SYS_OUT (GPIO40, INPUT only — active LOW when pressed)
//   BOOT = PIN_BOOT_BTN (GPIO0, INPUT_PULLUP — active LOW when pressed)
// =============================================================================

enum class ButtonEvent : uint8_t {
    NONE       = 0,
    PWR_SHORT  = 1,
    PWR_DOUBLE = 2,
    PWR_LONG   = 3,
    BOOT_SHORT = 4,
    BOOT_LONG  = 5,
};

void        buttons_init();
void        buttons_update();
ButtonEvent buttons_get_event();
