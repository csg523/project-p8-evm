#include <Arduino.h>

#include "power_monitor.h"
#include "logger.h"

// Power Monitor – Detect and classify system resets from SAMD21 reset cause
// register
#define RCAUSE_POR (1 << 0)    // Power-On Reset
#define RCAUSE_BOD12 (1 << 1)  // Brown-Out Detector 1.2V
#define RCAUSE_BOD33 (1 << 2)  // Brown-Out Detector 3.3V
#define RCAUSE_WDT (1 << 5)    // Watchdog Timer
#define RCAUSE_SYST (1 << 6)   // Software Reset

static ResetCause _cause = RESET_UNKNOWN;
static uint32_t _reset_count = 0;
static bool _recovery = false;

void power_monitor_init(void) {
  uint8_t rcause = PM->RCAUSE.reg;

  if (rcause & RCAUSE_WDT) {
    _cause = RESET_WATCHDOG;
    _recovery = true;
  } else if (rcause & (RCAUSE_BOD33 | RCAUSE_BOD12)) {
    _cause = RESET_POWER_LOSS;
    _recovery = true;
  } else if (rcause & RCAUSE_SYST) {
    _cause = RESET_SOFTWARE;
    _recovery = true;
  } else if (rcause & RCAUSE_POR) {
    _cause = RESET_COLD_BOOT;
    _recovery = false;
  } else {
    _cause = RESET_UNKNOWN;
    _recovery = false;
  }

  _reset_count++;
  logger_log(LOG_RESET, millis(), (uint32_t)_cause);
}

ResetCause power_monitor_get_reset_cause(void) { return _cause; }

bool power_monitor_is_power_loss_recovery(void) { return _recovery; }

uint32_t power_monitor_get_reset_count(void) { return _reset_count; }