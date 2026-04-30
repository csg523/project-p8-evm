#pragma once
#include "evm_events.h"
#include "evm_types.h"

#define EVM_EVENT_QUEUE_SIZE 64

typedef EvmResult (*EventHandler)(const ParsedEvent*);

void event_manager_init(void);
bool event_manager_enqueue(const ParsedEvent* event);
bool event_manager_enqueue_timer(EventType type, uint32_t timestamp_ms, uint32_t data);
bool event_manager_dispatch_one(void);
uint32_t event_manager_dispatch_all(void);
void event_manager_register_handler(EventType type, EventHandler handler);

bool event_manager_is_queue_full(void);
uint32_t event_manager_get_queue_depth(void);
uint32_t event_manager_get_total_enqueued(void);
uint32_t event_manager_get_dropped_count(void);

bool event_manager_is_drain_pending(void);