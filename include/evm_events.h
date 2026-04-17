#pragma once
#include "evm_types.h"

// ─── Event Types (produced by Parser, consumed by Supervisor) ─────────────────
typedef enum {
    EVT_NONE              = 0,
    EVT_VOTE              = 1,
    EVT_TAMPER            = 2,
    EVT_START             = 3,
    EVT_END               = 4,
    EVT_REPORT            = 5,
    EVT_RESET             = 6,
    EVT_FRAME_ERROR       = 7,
    EVT_TIMER_WATCHDOG    = 20,
    EVT_TIMER_TAMPER_POLL = 21,
    EVT_TIMER_TICK        = 22,
    EVT_MAX               = 32
} EventType;

// ─── Parsed Event (union payload) ─────────────────────────────────────────────
typedef struct {
    EventType type;
    uint32_t  timestamp_ms;
    union {
        struct {
            uint32_t vote_id;
            uint8_t  candidate_id;
        } vote;
        struct {
            uint32_t tamper_flags;
        } tamper;
    } data;
} ParsedEvent;


typedef ParsedEvent EvmEvent;


#ifdef __cplusplus
static const ParsedEvent EVT_EMPTY = { EVT_NONE, 0, {{0, 0}} };
#else
#define EVT_EMPTY ((ParsedEvent){ EVT_NONE, 0, {{0, 0}} })
#endif