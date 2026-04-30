#pragma once
#include "evm_types.h"

// ─── Core storage ────────────────────────────────────────────────────────────
void storage_init(void);
void storage_reset(void);  // clears NVM state; use in tests only

// ─── Vote records ────────────────────────────────────────────────────────────
EvmResult storage_append_vote(const VoteRecord* rec);
uint32_t  storage_recover(uint32_t* out_last_vote_id);
bool      storage_is_full(void);
uint32_t  storage_get_record_count(void);
void      storage_dump_serial(void);
uint32_t  storage_get_last_vote_id(void);
EvmResult storage_read_record(uint32_t index, VoteRecord* out);

// ─── Tamper records (flash-persisted) ────────────────────────────────────────
EvmResult storage_append_tamper(const TamperRecord* rec);
uint32_t  storage_get_tamper_count(void);
EvmResult storage_read_tamper(uint32_t index, TamperRecord* out);
void      storage_dump_tampers_serial(void);

// ─── Election state snapshot (power-loss recovery) ───────────────────────────
// Call on every state transition so the MCU can detect mid-election power loss.
void     storage_save_election_state(ElectionState s, uint32_t vote_count);
bool     storage_load_election_snapshot(ElectionState* out_state,
                                        uint32_t*      out_vote_count);
void     storage_clear_election_snapshot(void);

// ─── Log records ─────────────────────────────────────────────────────────────
bool     storage_manager_write_log(const LogEntry* entry);
uint32_t storage_get_log_count(void);
uint32_t storage_get_log_capacity(void);
