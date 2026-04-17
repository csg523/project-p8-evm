#include "system.h"
#include "event_manager.h"
#include "supervisor.h"
#include "uart_parser.h"
#include "power_monitor.h"
#include "vote_manager.h"
#include "storage_manager.h"
#include "tamper_manager.h"
#include "logger.h"
#include <Arduino.h>


#define UART_BAUD               9600


#define SOFT_WDT_TIMEOUT_MS     EVM_SOFT_WDT_TIMEOUT_MS


#define WDT_CHECK_PERIOD_MS     200


#define TAMPER_POLL_PERIOD_MS   20

static uint32_t _last_wdt_kick_ms    = 0;
static uint32_t _last_wdt_check_ms   = 0;
static uint32_t _last_tamper_poll_ms = 0;
static ResetCause _reset_cause = RESET_UNKNOWN;

// ─── Event handlers ───────────────────────────────────────────────────────────

static EvmResult route_to_supervisor(const ParsedEvent* ev) {
    return supervisor_handle_event(ev);
}

static EvmResult handle_reset_cmd(const ParsedEvent* ev) {
    (void)ev;
    logger_log(LOG_ADMIN_CMD, millis(), EVT_RESET);
    system_reset();
    return EVM_OK;
}

static EvmResult handle_watchdog_timer(const ParsedEvent* ev) {
    (void)ev;
    uint32_t now = millis();
    if (now - _last_wdt_kick_ms > SOFT_WDT_TIMEOUT_MS) {
        logger_log(LOG_ERROR, now, 0xD001);
        system_reset();
    }
    return EVM_OK;
}


static EvmResult handle_tamper_poll(const ParsedEvent* ev) {
    (void)ev;
    tamper_manager_poll();
    return EVM_OK;
}

// ─── system_init ──────────────────────────────────────────────────────────────

void system_init(void) {
    Serial.begin(UART_BAUD);
    Serial1.begin(UART_BAUD);

    while (!Serial && millis() < 3000) {} // wait for USB serial (debug only)

    // 1. Logger must be ready before any module emits log entries.
    logger_init();

    // 2. Power monitor reads the SAMD21 RCAUSE register; must run before any
    //    clock or peripheral setup can overwrite the register content.
    power_monitor_init();
    _reset_cause = power_monitor_get_reset_cause();

    // 3. Storage scan rebuilds the _nvm_valid[] index and _record_count.
    //    Must complete before vote_manager queries those values.
    storage_init();

    // 4. Vote manager recovers last_vote_id and per-candidate tally from NVM.
    vote_manager_init();

    // 5. Tamper manager configures GPIO pins.
    tamper_manager_init();

    // 6. UART parser resets frame assembler state.
    uart_parser_init();

   
    event_manager_init();
    event_manager_register_handler(EVT_VOTE,              route_to_supervisor);
    event_manager_register_handler(EVT_TAMPER,            route_to_supervisor);
    event_manager_register_handler(EVT_START,             route_to_supervisor);
    event_manager_register_handler(EVT_END,               route_to_supervisor);
    event_manager_register_handler(EVT_REPORT,            route_to_supervisor);
    event_manager_register_handler(EVT_FRAME_ERROR,       route_to_supervisor);
    event_manager_register_handler(EVT_TIMER_TICK,        route_to_supervisor);
    event_manager_register_handler(EVT_TIMER_WATCHDOG,    handle_watchdog_timer);
    event_manager_register_handler(EVT_RESET,             handle_reset_cmd);
    
    event_manager_register_handler(EVT_TIMER_TAMPER_POLL, handle_tamper_poll);

    // 8. Supervisor starts the state machine now that the queue is ready.
    supervisor_init(_reset_cause);

    _last_wdt_kick_ms    = millis();
    _last_wdt_check_ms   = _last_wdt_kick_ms;
    _last_tamper_poll_ms = _last_wdt_kick_ms;
}

// ─── system_tick ──────────────────────────────────────────────────────────────

void system_tick(void) {
    
    system_kick_watchdog();

    uint32_t now = millis();

  
    if (now - _last_tamper_poll_ms >= TAMPER_POLL_PERIOD_MS) {
        _last_tamper_poll_ms = now;
        event_manager_enqueue_timer(EVT_TIMER_TAMPER_POLL, now, 0);
    }

    // Enqueue a watchdog-check event every 200 ms.
    if (now - _last_wdt_check_ms >= WDT_CHECK_PERIOD_MS) {
        _last_wdt_check_ms = now;
        event_manager_enqueue_timer(EVT_TIMER_WATCHDOG, now, 0);
    }

    // Feed all available UART bytes into the frame assembler.
    while (Serial1.available()) {
        ParsedEvent evt = EVT_EMPTY;
        uint8_t b = (uint8_t)Serial1.read();
        if (uart_parser_feed(b, &evt)) {
            event_manager_enqueue(&evt);
        }
    }

    // Debug quick report trigger — development convenience only.
    // Guard with #ifdef DEBUG or remove before production build.
    if (Serial.available() && Serial.read() == 'R') {
        ParsedEvent e = EVT_EMPTY;
        e.type = EVT_REPORT;
        e.timestamp_ms = millis();
        event_manager_enqueue(&e);
    }

    event_manager_dispatch_all();
}

ResetCause system_get_reset_cause(void) {
    return _reset_cause;
}

void system_kick_watchdog(void) {
    _last_wdt_kick_ms = millis();
}

void system_reset(void) {
    // NVIC software reset (ARM Cortex-M0+ on Nano 33 IoT / SAMD21)
    NVIC_SystemReset();
}
