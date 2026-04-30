#pragma once
#include "evm_types.h"

void power_monitor_init(void);
ResetCause power_monitor_get_reset_cause(void);
bool power_monitor_is_power_loss_recovery(void);
uint32_t power_monitor_get_reset_count(void);