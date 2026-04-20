#pragma once
#include "evm_types.h"

void storage_init();
void storage_reset(void);  // clears NVM state; use in tests only
EvmResult storage_append_vote(const VoteRecord* rec);
uint32_t storage_recover(uint32_t* out_last_vote_id);  // returns count of valid records
bool storage_is_full();
uint32_t storage_get_record_count();
void storage_dump_serial();  // for audit report
uint32_t storage_get_last_vote_id(void);
EvmResult storage_read_record(uint32_t index, VoteRecord* out);
EvmResult storage_append_tamper(const TamperRecord* rec);

bool storage_manager_write_log(const LogEntry* entry);
uint32_t storage_get_log_count(void);
uint32_t storage_get_log_capacity(void);
