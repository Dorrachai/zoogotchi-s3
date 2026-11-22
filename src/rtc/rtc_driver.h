#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// =============================================================================
// RTC Driver — PCF85063A (I2C 0x51) with Rechargeable Backup Coin Cell
// Preserves wall-clock epoch across main LiPo swaps and deep sleep cycles.
// =============================================================================

void    rtc_driver_init();
bool    rtc_driver_is_ready();
bool    rtc_driver_read(struct tm* out_tm);
time_t  rtc_get_epoch();
void    rtc_set_epoch(time_t epoch_utc);
void    rtc_driver_sync_from_ntp(time_t epoch_utc);
uint8_t  rtc_get_hour(int16_t tz_offset_min = 0);
uint8_t  rtc_get_minute(int16_t tz_offset_min = 0);
void     rtc_epoch_to_local_hm(time_t epoch_utc, int16_t tz_offset_min, uint8_t* out_hour, uint8_t* out_min);
uint32_t rtc_epoch_to_local_day(time_t epoch_utc, int16_t tz_offset_min);

// Program PCF85063A countdown timer interrupt (GPIO39 active-LOW) for deep sleep.
void    rtc_set_wakeup_minutes(uint16_t minutes);
void    rtc_clear_alarm();
