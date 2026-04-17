#pragma once
#include "evm_types.h"

// ─────────────────────────────────────────────
//  System – boot sequence, WDT, reset cause
//  Owner: Gagan
// ─────────────────────────────────────────────

// Call this first in Arduino setup()
// Initialises all modules in the correct order
void system_init(void);

// Call in Arduino loop() – kicks watchdog, routes UART bytes to parser
void system_tick(void);

// Returns reset cause detected at boot
ResetCause system_get_reset_cause(void);

// Kick the software watchdog (call every loop iteration)
void system_kick_watchdog(void);

// Trigger a controlled software reset
void system_reset(void);
