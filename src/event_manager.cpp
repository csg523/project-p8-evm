#include "event_manager.h"
#include "logger.h"
#include <Arduino.h>
#include <string.h>

#ifdef NATIVE_TEST
#define EVM_CRIT_ENTER() ((void)0)
#define EVM_CRIT_EXIT()  ((void)0)
#else
#define EVM_CRIT_ENTER() noInterrupts()
#define EVM_CRIT_EXIT()  interrupts()
#endif

typedef struct {
    ParsedEvent events[EVM_EVENT_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t total_enqueued;
    uint32_t dropped;
} EventQueue;

static EventQueue g_q;
static EventHandler g_handlers[EVT_MAX];
static bool g_drain_pending = false;

static bool is_valid_event_type(EventType t) {
    return ((uint32_t)t < (uint32_t)EVT_MAX);
}

static uint32_t inc_idx(uint32_t i) {
    return (i + 1u) % EVM_EVENT_QUEUE_SIZE;
}

void event_manager_init(void) {
    memset(&g_q, 0, sizeof(g_q));
    memset(g_handlers, 0, sizeof(g_handlers));
    g_drain_pending = false;
}

bool event_manager_enqueue(const ParsedEvent* event) {
    if (!event) return false;

    EVM_CRIT_ENTER();

    if (!is_valid_event_type(event->type)) {
        g_q.dropped++;
        EVM_CRIT_EXIT();
        logger_log(LOG_ERROR, millis(), 0xEE01); // invalid event type
        return false;
    }

    if (g_q.count >= EVM_EVENT_QUEUE_SIZE) {
        g_q.dropped++;
        EVM_CRIT_EXIT();
        logger_log(LOG_ERROR, millis(), 0xEE02); // queue full
        return false;
    }

    g_q.events[g_q.head] = *event;
    g_q.head = inc_idx(g_q.head);
    g_q.count++;
    g_q.total_enqueued++;
    EVM_CRIT_EXIT();
    return true;
}

bool event_manager_enqueue_timer(EventType type, uint32_t timestamp_ms, uint32_t data) {
    if (!is_valid_event_type(type)) {
        EVM_CRIT_ENTER();
        g_q.dropped++;
        EVM_CRIT_EXIT();
        logger_log(LOG_ERROR, millis(), 0xEE06); // invalid timer event type
        return false;
    }

    ParsedEvent e = EVT_EMPTY;
    e.type = type;
    e.timestamp_ms = timestamp_ms;
    e.data.tamper.tamper_flags = data;
    return event_manager_enqueue(&e);
}

bool event_manager_dispatch_one(void) {
    ParsedEvent e = EVT_EMPTY;

    EVM_CRIT_ENTER();
    if (g_q.count == 0) {
        EVM_CRIT_EXIT();
        return false;
    }
    e = g_q.events[g_q.tail];
    g_q.tail = inc_idx(g_q.tail);
    g_q.count--;
    EVM_CRIT_EXIT();

    if (!is_valid_event_type(e.type)) {
        logger_log(LOG_ERROR, millis(), 0xEE05); // corrupted/invalid queued type
        return true;
    }

    EventHandler handler = g_handlers[e.type];
    if (handler == 0) {
        logger_log(LOG_ERROR, millis(), 0xEE03);
        return true;
    }

    EvmResult r = handler(&e);
    if (r != EVM_OK) {
        logger_log(LOG_ERROR, millis(), (uint32_t)r);
    }
    return true;
}

// g_drain_pending: set when dispatch_all() hits the hard-stop limit and leaves
// events in the queue to be drained on the next tick (see declaration above).

uint32_t event_manager_dispatch_all(void) {
    uint32_t n = 0;
    while (event_manager_dispatch_one()) {
        n++;
        // Hard-stop: a handler re-enqueuing on every call would loop forever.
        // EVM_EVENT_QUEUE_SIZE * 4 = 256 dispatches per tick is several times
        // the queue capacity (64), giving legitimate burst events room to drain
        // while bounding the worst-case tick duration to ~256 handler calls.
        
        if (n >= (EVM_EVENT_QUEUE_SIZE * 4u)) {
            logger_log(LOG_ERROR, millis(), 0xEE04);
            if (g_q.count > 0) {
                g_drain_pending = true;
            }
            break;
        }
    }
    // If a previous tick left a drain pending and we have just cleared the
    // queue, reset the flag.
    if (g_q.count == 0) {
        g_drain_pending = false;
    }
    return n;
}

void event_manager_register_handler(EventType type, EventHandler handler) {
    if ((uint32_t)type >= EVT_MAX) return;
    g_handlers[type] = handler;
}

bool event_manager_is_drain_pending(void) {
    return g_drain_pending;
}

bool event_manager_is_queue_full(void) {
    return g_q.count >= EVM_EVENT_QUEUE_SIZE;
}

uint32_t event_manager_get_queue_depth(void) {
    return g_q.count;
}

uint32_t event_manager_get_total_enqueued(void) {
    return g_q.total_enqueued;
}

uint32_t event_manager_get_dropped_count(void) {
    return g_q.dropped;
}