#include "rtc_driver.h"
#include "../config.h"
#include "../hal/i2c_lock.h"
#include <Arduino.h>
#include <Wire.h>

// PCF85063A registers
static constexpr uint8_t PCF_REG_CTRL1      = 0x00;
static constexpr uint8_t PCF_REG_CTRL2      = 0x01;
static constexpr uint8_t PCF_REG_SECONDS    = 0x04;
static constexpr uint8_t PCF_REG_TIMER_VAL  = 0x10;
static constexpr uint8_t PCF_REG_TIMER_MODE = 0x11;

static uint8_t bcd_to_dec(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }
static uint8_t dec_to_bcd(uint8_t d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

static bool _ready = false;

static void pcf_write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

void rtc_driver_init() {
    i2c_bus_init();
    pinMode(PIN_RTC_INT, INPUT_PULLUP);

    I2CScopeGuard guard(50);
    if (!guard.locked) return;

    Wire.beginTransmission(RTC_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[rtc] PCF85063A not found");
        _ready = false;
        return;
    }

    pcf_write_reg(PCF_REG_CTRL1, 0x00);  // normal mode, 24h, oscillator running
    pcf_write_reg(PCF_REG_CTRL2, 0x00);  // clear alarm/timer flags
    _ready = true;

    struct tm t = {};
    if (rtc_driver_read(&t)) {
        // If RTC lost backup battery (year < 2025), seed a sensible default date
        if (t.tm_year + 1900 < 2025) {
            rtc_set_epoch(1767268800L); // 2026-01-01T12:00:00Z
        }
    }
    Serial.println("[rtc] PCF85063A ready");
}

bool rtc_driver_is_ready() { return _ready; }

bool rtc_driver_read(struct tm* out_tm) {
    if (!_ready || !out_tm) return false;
    I2CScopeGuard guard(30);
    if (!guard.locked) return false;

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(PCF_REG_SECONDS);
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)7) < 7) return false;

    uint8_t sec  = Wire.read() & 0x7F;
    uint8_t min  = Wire.read() & 0x7F;
    uint8_t hour = Wire.read() & 0x3F;
    uint8_t day  = Wire.read() & 0x3F;
    /* wday */     Wire.read();
    uint8_t mon  = Wire.read() & 0x1F;
    uint8_t year = Wire.read();

    out_tm->tm_sec   = bcd_to_dec(sec);
    out_tm->tm_min   = bcd_to_dec(min);
    out_tm->tm_hour  = bcd_to_dec(hour);
    out_tm->tm_mday  = bcd_to_dec(day);
    out_tm->tm_mon   = bcd_to_dec(mon) - 1;
    out_tm->tm_year  = bcd_to_dec(year) + 100;
    out_tm->tm_isdst = 0;

    if (out_tm->tm_mday < 1 || out_tm->tm_mday > 31) out_tm->tm_mday = 1;
    if (out_tm->tm_mon < 0 || out_tm->tm_mon > 11)   out_tm->tm_mon  = 0;
    return true;
}

// Howard Hinnant civil-from-days / days-from-civil UTC epoch converter
// (avoids timezone distortion inside Newlib mktime)
static time_t tm_to_utc_epoch(const struct tm* t) {
    int64_t y = (int64_t)t->tm_year + 1900;
    uint32_t m = (uint32_t)t->tm_mon + 1;
    uint32_t d = (uint32_t)t->tm_mday;
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = (uint32_t)(y - era * 400);
    const uint32_t doy = (153 * (m + (m > 2 ? (uint32_t)-3 : 9)) + 2) / 5 + d - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097 + (int64_t)doe - 719468;
    return (time_t)(days * 86400LL + t->tm_hour * 3600LL + t->tm_min * 60LL + t->tm_sec);
}

time_t rtc_get_epoch() {
    struct tm t = {};
    if (rtc_driver_read(&t)) {
        return tm_to_utc_epoch(&t);
    }
    return 1767268800L + (time_t)(millis() / 1000UL);
}

void rtc_set_epoch(time_t epoch_utc) {
    if (!_ready) return;
    struct tm t = {};
    gmtime_r(&epoch_utc, &t);

    I2CScopeGuard guard(30);
    if (!guard.locked) return;

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(PCF_REG_SECONDS);
    Wire.write(dec_to_bcd((uint8_t)t.tm_sec));
    Wire.write(dec_to_bcd((uint8_t)t.tm_min));
    Wire.write(dec_to_bcd((uint8_t)t.tm_hour));
    Wire.write(dec_to_bcd((uint8_t)t.tm_mday));
    Wire.write(dec_to_bcd((uint8_t)t.tm_wday));
    Wire.write(dec_to_bcd((uint8_t)(t.tm_mon + 1)));
    Wire.write(dec_to_bcd((uint8_t)(t.tm_year >= 100 ? t.tm_year - 100 : 26)));
    Wire.endTransmission();
}

void rtc_driver_sync_from_ntp(time_t epoch_utc) {
    rtc_set_epoch(epoch_utc);
    Serial.printf("[rtc] Synced epoch=%lld\n", (long long)epoch_utc);
}

uint8_t rtc_get_hour(int16_t tz_offset_min) {
    time_t local = rtc_get_epoch() + (time_t)tz_offset_min * 60L;
    struct tm t = {};
    gmtime_r(&local, &t);
    return (uint8_t)t.tm_hour;
}

uint8_t rtc_get_minute(int16_t tz_offset_min) {
    time_t local = rtc_get_epoch() + (time_t)tz_offset_min * 60L;
    struct tm t = {};
    gmtime_r(&local, &t);
    return (uint8_t)t.tm_min;
}

void rtc_set_wakeup_minutes(uint16_t minutes) {
    if (!_ready) return;
    if (minutes < 1) minutes = 1;
    if (minutes > 255) minutes = 255;

    I2CScopeGuard guard(30);
    if (!guard.locked) return;

    // Timer_mode: TCF=11 (1/60 Hz = 1 min tick), TE=1 (bit2), TIE=1 (bit1), TI_TP=0
    pcf_write_reg(PCF_REG_CTRL2, 0x00);               // clear TF
    pcf_write_reg(PCF_REG_TIMER_VAL, (uint8_t)minutes);
    pcf_write_reg(PCF_REG_TIMER_MODE, (0x03 << 3) | 0x06);
}

void rtc_clear_alarm() {
    if (!_ready) return;
    I2CScopeGuard guard(30);
    if (!guard.locked) return;
    pcf_write_reg(PCF_REG_TIMER_MODE, 0x00);
    pcf_write_reg(PCF_REG_CTRL2, 0x00);
}
