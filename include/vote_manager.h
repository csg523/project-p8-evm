#pragma once
#include "evm_events.h"
#include "evm_types.h"

void vote_manager_init(void);
void vote_manager_recover_from_storage(void);

EvmResult vote_manager_process(uint32_t vote_id, uint8_t candidate_id,
                               uint32_t timestamp_ms);
bool vote_manager_is_duplicate(uint32_t vote_id);

uint32_t vote_manager_get_count(uint8_t candidate_id);
uint32_t vote_manager_get_total(void);
uint32_t vote_manager_get_last_vote_id(void);

const VoteManagerState* vote_manager_get_state(void);
void vote_manager_dump_tally_serial(void);