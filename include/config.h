#pragma once

// --- Плата: Waveshare ESP32-S3-RLCD-4.2 (ESP32-S3-WROOM-1-N16R8) ---

// Экран ST7305, 4-проводной SPI.
// Внимание: DC = GPIO5, CS = GPIO40 (в BSP Waveshare порядок аргументов
// конструктора DisplayPort — mosi, scl, dc, cs, rst).
#define PIN_LCD_MOSI 12
#define PIN_LCD_SCLK 11
#define PIN_LCD_DC    5
#define PIN_LCD_CS   40
#define PIN_LCD_RST  41

// Общая шина I2C: PCF85063 (RTC, 0x51), SHTC3 (0x70)
// Внимание: на этой плате SCL = GPIO14, SDA = GPIO13
#define PIN_I2C_SCL 14
#define PIN_I2C_SDA 13

// Кнопки (активный уровень — низкий)
#define PIN_BTN_BOOT 0
#define PIN_BTN_KEY  18

// Батарея: делитель 1:3 на ADC1_CH3 (GPIO4)
#define PIN_BAT_ADC  4
#define BAT_DIVIDER  3.0f
#define BAT_EMPTY_V  3.00f
#define BAT_FULL_V   4.12f

// --- Город ---
// Город, координаты и часовой пояс — личные данные, поэтому они лежат
// в include/location.h, который не попадает в git.
// Шаблон: include/location.example.h
#if __has_include("location.h")
#include "location.h"
#else
#error "Нет include/location.h — скопируй include/location.example.h и заполни"
#endif

// --- Расписание ---
#define WEATHER_PERIOD_MS  (20UL * 60UL * 1000UL)   // погода раз в 20 минут (когда бодрствуем)
#define SENSOR_PERIOD_MS   (60UL * 1000UL)          // внутренние датчики раз в минуту
#define NTP_RETRY_MS       (60UL * 1000UL)
#define WIFI_RETRY_MS      (20UL * 1000UL)          // повтор подключения к Wi-Fi

// --- Энергосбережение ---
#define SLEEP_INTERVAL_S   (3 * 60 * 60)            // просыпаться раз в 3 часа
#define SLEEP_ANCHOR_MIN   5                        // привязка к 00:05, 03:05, 06:05…
#define AWAKE_TIMER_MS     180000UL                 // окно после пробуждения по таймеру (3 мин)
#define AWAKE_BUTTON_MS    180000UL                 // окно после пробуждения/нажатия кнопки (3 мин)
#define AWAKE_COLD_MS      180000UL                 // окно после включения/прошивки (3 мин)

// --- Сетевые сервисы (задел под внешний сервер / ESPHome) ---
#define ENABLE_SERVICES    1
#define MDNS_HOSTNAME      "rlcd-clock"
#define OTA_PASSWORD       ""                       // пусто — OTA без пароля