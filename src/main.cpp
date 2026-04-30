#include <Arduino.h>
#include "system.h"
#include "supervisor.h"
#include "vote_manager.h"
#include "logger.h"

void setup() {
    system_init();
}

void loop() {
    // Main loop only ticks the event system.
    system_tick();

}