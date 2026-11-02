// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

enum DeviceType {
    DEVICE_AXIS = 0,
    DEVICE_PWM_SENSOR = 1,
    DEVICE_GPIO = 2,
    DEVICE_PIPELINE = 3,
    DEVICE_VAR = 4
};

struct HardwareEntity {
    String name;
    DeviceType type;
    std::vector<int8_t> params_int;
    std::vector<float> params_float;

    volatile int32_t runtimeState1;
    float runtimeState2;
    float runtimeState3;

    HardwareEntity(String n, DeviceType t, std::vector<int8_t> p, std::vector<float> pr, int32_t r1, float r2, float r3)
    : name(n), type(t), params_int(p), params_float(pr), runtimeState1(r1), runtimeState2(r2), runtimeState3(r3) {}
};

Preferences preferences;
portMUX_TYPE hardwareMux = portMUX_INITIALIZER_UNLOCKED;
