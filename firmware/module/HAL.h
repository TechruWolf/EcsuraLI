// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

#if defined(ESP32)
#define SYS_GET_TIME_US()     esp_timer_get_time()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define MOTION_BUFFER_SIZE    1000

#elif defined(ARDUINO_ARCH_RP2040)
#define SYS_GET_TIME_US()     micros()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define MOTION_BUFFER_SIZE    1000

#elif defined(ESP8266)
#define SYS_GET_TIME_US()     micros()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define MOTION_BUFFER_SIZE    256

#elif defined(ARDUINO_ARCH_STM32)
#define SYS_GET_TIME_US()     micros()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define MOTION_BUFFER_SIZE    500

#else
#define SYS_GET_TIME_US()     micros()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define MOTION_BUFFER_SIZE    64
#endif
