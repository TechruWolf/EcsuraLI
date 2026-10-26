// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

#if defined(ESP32)
#define SYS_GET_TIME_US()     esp_timer_get_time()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define SYS_ENTER_CRITICAL()  portENTER_CRITICAL(&hardwareMux)
#define SYS_EXIT_CRITICAL()   portEXIT_CRITICAL(&hardwareMux)
#define MOTION_BUFFER_SIZE    1000
#define LUT_SIZE              64
#define LOOK_AHEAD_COUNT      16
#define SYS_TASK_CREATE(task, name, stack, params, priority, handle, core) \
xTaskCreatePinnedToCore(task, name, stack, params, priority, handle, core)

#elif defined(ARDUINO_ARCH_RP2040)
#define SYS_GET_TIME_US()     micros()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define SYS_ENTER_CRITICAL()  portENTER_CRITICAL()
#define SYS_EXIT_CRITICAL()   portEXIT_CRITICAL()
#define MOTION_BUFFER_SIZE    1000
#define LUT_SIZE              64
#define LOOK_AHEAD_COUNT      16
#define SYS_TASK_CREATE(task, name, stack, params, priority, handle, core) \
xTaskCreate(task, name, stack, params, priority, handle)

#elif defined(ESP8266)
#define SYS_GET_TIME_US()     micros()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define SYS_ENTER_CRITICAL()  noInterrupts()
#define SYS_EXIT_CRITICAL()   interrupts()
#define MOTION_BUFFER_SIZE    256
#define LUT_SIZE              48
#define LOOK_AHEAD_COUNT      8
#define SYS_TASK_CREATE(task, name, stack, params, priority, handle, core) \
((void)0)

#elif defined(ARDUINO_ARCH_STM32)
#define SYS_GET_TIME_US()     micros()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define SYS_ENTER_CRITICAL()  portENTER_CRITICAL()
#define SYS_EXIT_CRITICAL()   portEXIT_CRITICAL()
#define MOTION_BUFFER_SIZE    500
#define LUT_SIZE              16
#define LOOK_AHEAD_COUNT      2
#define SYS_TASK_CREATE(task, name, stack, params, priority, handle, core) \
xTaskCreate(task, name, stack, params, priority, handle)

#else
#define SYS_GET_TIME_US()     micros()
#define SYS_ADC_READ(pin)     analogRead(pin)
#define SYS_PULSE_IN(p, v, t) pulseIn(p, v, t)
#define SYS_DELAY_US(us)      delayMicroseconds(us)
#define SYS_ENTER_CRITICAL()  noInterrupts()
#define SYS_EXIT_CRITICAL()   interrupts()
#define MOTION_BUFFER_SIZE    64
#define LUT_SIZE              8
#define LOOK_AHEAD_COUNT      1
#define SYS_TASK_CREATE(task, name, stack, params, priority, handle, core) \
((void)0)
#endif
