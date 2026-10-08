#ifndef RTC_PORT_H
#define RTC_PORT_H
#include <stdbool.h>
#include "driver/i2c_master.h"
/* call after the board I2C bus exists (display_port_init creates it) */
bool rtc_port_init(i2c_master_bus_handle_t bus);
/* A board with NO RTC chip (the round 1.75C): the system clock is the ESP32's
 * own RTC timer - it counts through deep sleep and restarts, not through a
 * power cut. At a boot that finds it unset (rtc_port_init returned false and
 * time() is still 1970) call this once the save is loaded: the clock starts
 * at the build time or at `not_before` (the save's stamp), whichever is
 * later, so the tank's timeline only moves forward and a power cut reads as
 * no time at all - never as an absence nobody can vouch for. */
void rtc_port_seed(int64_t not_before);
#endif
