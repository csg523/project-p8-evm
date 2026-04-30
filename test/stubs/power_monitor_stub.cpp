// Replaces power_monitor.cpp for native testing.
// Real file accesses PM->RCAUSE.reg (SAMD21 hardware register) which
// doesn't exist on a PC — this stub provides the same API with safe defaults.
#include "power_monitor.h"

static ResetCause _cause       = RESET_COLD_BOOT;
static uint32_t   _reset_count = 0;

void power_monitor_init(void) {
    _cause = RESET_COLD_BOOT;
    _reset_count++;
}

ResetCause power_monitor_get_reset_cause(void)        { return _cause; }
bool       power_monitor_is_power_loss_recovery(void) { return false; }
uint32_t   power_monitor_get_reset_count(void)        { return _reset_count; }
