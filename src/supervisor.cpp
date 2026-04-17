#include "supervisor.h"
#include "vote_manager.h"
#include "tamper_manager.h"
#include "logger.h"
#include <Arduino.h>

// Transition table [from][to] = allowed?
static const bool TT[STATE_COUNT][STATE_COUNT] = {
  /* INIT   */ { false, true,  false, false, true,  true  },
  /* PRE    */ { false, false, true,  false, true,  true  },
  /* VOTING */ { false, false, false, true,  true,  true  },
  /* CLOSED */ { false, false, false, false, true,  false },
  /* TAMPER */ { false, false, false, false, false, false },
  /* ERROR  */ { false, false, false, false, true,  false }
};

static SupervisorState _sv;

static void _do_transition(ElectionState to) {
    _sv.transition_in_progress = true;
    _sv.previous_state = _sv.current_state;
    _sv.current_state = to;
    _sv.transition_count++;
    _sv.last_transition_ms = millis();
    logger_log(LOG_STATE_CHANGE, _sv.last_transition_ms,
               ((uint32_t)_sv.previous_state << 8) | (uint32_t)to);
    _sv.transition_in_progress = false;
}

void supervisor_init(ResetCause cause) {
    _sv.current_state = STATE_INITIALIZATION;
    _sv.previous_state = STATE_INITIALIZATION;
    _sv.transition_count = 0;
    _sv.last_transition_ms = millis();
    _sv.transition_in_progress = false;

    logger_log(LOG_RESET, millis(), (uint32_t)cause);

    if (cause == RESET_WATCHDOG) _do_transition(STATE_ERROR);
    else _do_transition(STATE_PRE_ELECTION);
}

EvmResult supervisor_handle_event(const ParsedEvent* evt) {
    if (!evt || _sv.transition_in_progress) return EVM_ERR_INVALID;

    ElectionState cur = _sv.current_state;

    switch (evt->type) {
        case EVT_VOTE:
            if (cur != STATE_VOTING_ACTIVE) {
                logger_log(LOG_VOTE_REJECTED, evt->timestamp_ms, (uint32_t)cur);
                return EVM_ERR_WRONG_STATE;
            }
            return vote_manager_process(evt->data.vote.vote_id,
                                        evt->data.vote.candidate_id,
                                        evt->timestamp_ms);

        case EVT_TAMPER:
            tamper_manager_report((TamperType)evt->data.tamper.tamper_flags, evt->timestamp_ms);
            supervisor_force_tamper_lockdown((TamperType)evt->data.tamper.tamper_flags);
            return EVM_OK;

        case EVT_START:
            if (cur != STATE_PRE_ELECTION) return EVM_ERR_WRONG_STATE;
            _do_transition(STATE_VOTING_ACTIVE);
            logger_log(LOG_ADMIN_CMD, millis(), EVT_START);
            return EVM_OK;

        case EVT_END:
            if (cur != STATE_VOTING_ACTIVE) return EVM_ERR_WRONG_STATE;
            _do_transition(STATE_VOTING_CLOSED);
            logger_log(LOG_ADMIN_CMD, millis(), EVT_END);
            return EVM_OK;

        case EVT_REPORT:
            if (cur != STATE_VOTING_CLOSED) return EVM_ERR_WRONG_STATE;
            logger_log(LOG_ADMIN_CMD, millis(), EVT_REPORT);
            return EVM_OK;

        case EVT_RESET:
        case EVT_TIMER_WATCHDOG:
        case EVT_TIMER_TAMPER_POLL:
        case EVT_TIMER_TICK:
        case EVT_FRAME_ERROR:
            // These are handled by other handlers or just audited.
            return EVM_OK;

        default:
            return EVM_ERR_INVALID;
    }
}

void supervisor_force_tamper_lockdown(TamperType type) {
    if (_sv.current_state == STATE_TAMPER_DETECTED) return;
    logger_log(LOG_TAMPER, millis(), (uint32_t)type);
    _do_transition(STATE_TAMPER_DETECTED);
}

ElectionState supervisor_get_state(void) { return _sv.current_state; }

bool supervisor_request_transition(ElectionState next) {
    if (!supervisor_is_valid_transition(_sv.current_state, next)) return false;
    _do_transition(next);
    return true;
}

bool supervisor_is_valid_transition(ElectionState from, ElectionState to) {
    if (from >= STATE_COUNT || to >= STATE_COUNT) return false;
    return TT[from][to];
}

uint32_t supervisor_get_transition_count(void) { return _sv.transition_count; }

const char* supervisor_state_name(ElectionState s) {
    switch (s) {
        case STATE_INITIALIZATION:  return "INITIALIZATION";
        case STATE_PRE_ELECTION:    return "PRE_ELECTION";
        case STATE_VOTING_ACTIVE:   return "VOTING_ACTIVE";
        case STATE_VOTING_CLOSED:   return "VOTING_CLOSED";
        case STATE_TAMPER_DETECTED: return "TAMPER_DETECTED";
        case STATE_ERROR:           return "ERROR";
        default:                    return "UNKNOWN";
    }
}