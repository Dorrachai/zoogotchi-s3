#pragma once

// =============================================================================
// ZooGotchi S3 — Central Hardware & Tuning Constants
// Target: Waveshare ESP32-S3-Touch-LCD-1.69 (16MB Flash, 8MB OPI PSRAM)
// =============================================================================

// --- Display SPI (ST7789V2, 240x280) -----------------------------------------
#define PIN_LCD_SCLK        6
#define PIN_LCD_MOSI        7
#define PIN_LCD_CS          5
#define PIN_LCD_DC          4
#define PIN_LCD_RST         8
#define PIN_LCD_BL          15
#define LCD_WIDTH           240
#define LCD_HEIGHT          280
#define LCD_CORNER_INSET    20
#define LCD_SPI_FREQ        80000000UL
#define LCD_SPI_FREQ_READ   16000000UL

// --- Shared I2C Bus (CST816T Touch, QMI8658C IMU, PCF85063A RTC) -------------
#define PIN_I2C_SDA         11
#define PIN_I2C_SCL         10
#define I2C_FREQ            400000

#define TOUCH_I2C_ADDR      0x15
#define IMU_I2C_ADDR        0x6B
#define RTC_I2C_ADDR        0x51

#define PIN_TOUCH_RST       13
#define PIN_TOUCH_INT       14
#define PIN_IMU_INT1        38
#define PIN_RTC_INT         39

// --- Onboard Buttons & Power Latch -------------------------------------------
// CRITICAL: PIN_SYS_OUT (GPIO40) is a sense INPUT only — NEVER drive as OUTPUT!
#define PIN_BOOT_BTN        0
#define PIN_SYS_OUT         40
#define PIN_SYS_EN          41

#define BTN_DEBOUNCE_MS     30
#define BTN_DOUBLE_MS       380
#define BTN_LONG_PRESS_MS   2500

// --- Passive Buzzer (LEDC Channel 0 — keep BUZZER_DUTY == 512) ---------------
#define PIN_BUZZER          42
#define BUZZER_LEDC_CH      0
#define BUZZER_LEDC_RES     10
#define BUZZER_DUTY         512

// --- Battery ADC (200k / 100k divider) ---------------------------------------
#define PIN_BAT_ADC         1
#define BAT_DIVIDER_RATIO   3.0f
#define BAT_V_FULL          4.2f
#define BAT_V_EMPTY         3.0f
#define BAT_BROWNOUT_V      3.35f
#define BAT_ADC_SAMPLES     16

// --- LVGL & Backlight (LEDC Channel 1) ---------------------------------------
#define LV_BUF_LINES        40
#define LCD_BL_NORMAL       180
#define LCD_BL_DIM          45

// --- Power / Sleep Timings ---------------------------------------------------
#define SLEEP_DIM_MS        15000UL
#define SLEEP_LIGHT_MS      45000UL
#define SLEEP_DEEP_MS       180000UL
#define SLEEP_RTC_WAKE_MIN  30

// --- IMU Thresholds ----------------------------------------------------------
#define SHAKE_GENTLE_G      1.55f
#define SHAKE_HARD_G        2.45f
#define SHAKE_GENTLE_MS     180UL
#define SHAKE_HARD_MS       240UL
#define DIZZY_DURATION_MS   2500UL

// --- Game & Simulation Timing ------------------------------------------------
#define GAME_TICK_MS        1000UL
#define AUTOSAVE_INTERVAL_S 60UL

// --- Wi-Fi Companion Mode (On-Demand Only) -----------------------------------
#define WIFI_SOFTAP_SSID    "ZooGotchi-S3"
#define WIFI_SOFTAP_PASS    ""
#define WIFI_MDNS_HOST      "zoogotchi"
#define WIFI_CONNECT_TIMEOUT_MS 8000UL

// --- NTP ---------------------------------------------------------------------
#define NTP_SERVER1         "pool.ntp.org"
#define NTP_SERVER2         "time.google.com"
#define NTP_GMT_OFFSET      0
#define NTP_DST_OFFSET      0

// --- Micro-LLM Safety & Limits -----------------------------------------------
#define LLM_PARTITION_LABEL "llm_model"
#define LLM_MAX_INPUT_CHARS 120
#define LLM_MAX_TOK_SCREEN  28
#define LLM_MAX_TOK_WEB     64
#define LLM_MIN_BATTERY_PCT 10
