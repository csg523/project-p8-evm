#pragma once
#include "evm_events.h"
#include "evm_types.h"

void tamper_manager_init(void);
void tamper_manager_poll(void);
void tamper_manager_report(TamperType type, uint32_t timestamp_ms);

bool tamper_manager_is_triggered(void);
bool tamper_manager_is_locked(void);
uint32_t tamper_manager_get_flags(void);
uint32_t tamper_manager_get_count(void);

const TamperManagerState* tamper_manager_get_state(void);