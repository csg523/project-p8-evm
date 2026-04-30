#pragma once
#include "evm_events.h"
#include "evm_types.h"

void supervisor_init(ResetCause cause);
EvmResult supervisor_handle_event(const ParsedEvent* ev);

ElectionState supervisor_get_state(void);
bool supervisor_request_transition(ElectionState next);
bool supervisor_is_valid_transition(ElectionState from, ElectionState to);
uint32_t supervisor_get_transition_count(void);

void supervisor_force_tamper_lockdown(void);
const char* supervisor_state_name(ElectionState s);