#pragma once
#include "evm_types.h"

// ─────────────────────────────────────────────
//  Logger – immutable audit trail
//  Write-only from all other modules.
//  Never influences control flow.
// ─────────────────────────────────────────────

void logger_init(void);
void logger_log(LogEventType type, uint32_t timestamp_ms, uint32_t data);
void logger_flush(void);
void logger_print_entry(uint32_t index);

uint32_t logger_get_count(void);
bool logger_get_entry(uint32_t index, LogEntry* out);