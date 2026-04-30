//system act as a centre to connect all these modules
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

#define UART_BAUD             9600             //speed of communication (bits/second)
#define SOFT_WDT_TIMEOUT_MS    EVM_SOFT_WDT_TIMEOUT_MS //for 2000ms if response dosent come then reset the system
#define WDT_CHECK_PERIOD_MS    200 //Interval (in ms) at which watchdog health is checked
#define TAMPER_POLL_PERIOD_MS  20 //Interval (in ms) at which tamper status is polled

static uint32_t _last_wdt_kick_ms    = 0;//last time system said im alive to watchdog timer, if this value is too old then system will reset
static uint32_t _last_wdt_check_ms   = 0;//when did we last checked watchdog
static uint32_t _last_tamper_poll_ms = 0;//when did we last checked tamper status
static ResetCause _reset_cause = RESET_UNKNOWN;//(enum)Reset Cause tells the reason for system reset(ex-power failure,software reset etc)

// ─── Event handlers ───────────────────────────────────────────────────────────

//simply forwards event to supervisor, which will decide what to do with it based on current state and event type
static EvmResult route_to_supervisor(const ParsedEvent* ev) {
    return supervisor_handle_event(ev);
}
//log that reset command was recieved and then reset the system
//parsedEvent(struct) have type(vote,tamper),timestamp and data
//EvmResult(enum) tells the outcome of fxn(evm_ok,evm_err_wrong_state etc)
static EvmResult handle_reset_cmd(const ParsedEvent* ev) {
    (void)ev;
    logger_log(LOG_ADMIN_CMD, millis(), EVT_RESET);
    system_reset();
    return EVM_OK;
}
//0xD001 is code for watchdog timeout error
static EvmResult handle_watchdog_timer(const ParsedEvent* ev) {
    (void)ev;
    uint32_t now = millis();

    if (now - _last_wdt_kick_ms > SOFT_WDT_TIMEOUT_MS) {
        logger_log(LOG_ERROR, now, 0xD001);
        system_reset();
    }
    return EVM_OK;
}
//poll tamper status and log if any tamper event is detected
//EVT_Timer_Tamper_POLL got enqued-event manager deques it and call handle tamper poll
static EvmResult handle_tamper_poll(const ParsedEvent* ev) {
    (void)ev;
    tamper_manager_poll();
    return EVM_OK;
}

// ─── system_init ──────────────────────────────────────────────────────────────

void system_init(void) {
    Serial.begin(UART_BAUD);

#if defined(ARDUINO_ARCH_SAMD)
    Serial1.begin(UART_BAUD);
#endif

    while (!Serial && millis() < 3000) {}

    // 1. Logger must be ready before any module emits log entries.
    logger_init();

    // 2. it reads reset cause early as it can get overwritten 
    power_monitor_init();
    _reset_cause = power_monitor_get_reset_cause();

    // 3. Initialize storage and rebuild data from memory
    //    Must complete before vote_manager queries those values.
    storage_init();

    // 4. Initialize vote manager using data from storage like vote count and last vote id
    vote_manager_init();

    // 5. Tamper manager configures GPIO pins.
    tamper_manager_init();

    // 6. UART parser resets frame assembler state.
    uart_parser_init();

    event_manager_init();//create event queue and laterevent manager will dispatch events to thier respective modules 
    // 7. whenever this event comes call this function(event manager calls the function registered for that event)
    event_manager_register_handler(EVT_VOTE,              route_to_supervisor);
    event_manager_register_handler(EVT_TAMPER,            route_to_supervisor);
    event_manager_register_handler(EVT_START,             route_to_supervisor);
    event_manager_register_handler(EVT_END,               route_to_supervisor);
    event_manager_register_handler(EVT_STATUS,               route_to_supervisor);
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

#ifdef WOKWI_SIM
    while (Serial.available()) {
        ParsedEvent evt = EVT_EMPTY;
        uint8_t b = (uint8_t)Serial.read();
        if (uart_parser_feed(b, &evt)) {
            event_manager_enqueue(&evt);
        }
    }
#else
    while (Serial1.available()) {
        ParsedEvent evt = EVT_EMPTY;
        uint8_t b = (uint8_t)Serial1.read();
        if (uart_parser_feed(b, &evt)) {
            event_manager_enqueue(&evt);
        }
    }
#endif

    // Debug quick report trigger — development convenience only.
    // Guard with #ifdef DEBUG or remove before production build.
    if (Serial.available() && Serial.peek() == 'R') {
        Serial.read();
        ParsedEvent e = EVT_EMPTY;
        e.type = EVT_REPORT;
        e.timestamp_ms = millis();
        event_manager_enqueue(&e);
    }

    event_manager_dispatch_all();

    system_kick_watchdog();
}

ResetCause system_get_reset_cause(void) {
    return _reset_cause;
}

void system_kick_watchdog(void) {
    _last_wdt_kick_ms = millis();
}

void system_reset(void) {
#if defined(ARDUINO_ARCH_SAMD)
    NVIC_SystemReset();
#else
    while (true) {}
#endif
}
