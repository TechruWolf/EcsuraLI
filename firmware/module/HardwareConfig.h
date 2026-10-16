// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

// ==========================================
//  DEVICE_AXIS_ ...
// ==========================================
// PIN MAPPING:
// Pin[0] : STEP
// Pin[1] : DIR
// Pin[2] : EN
// Pin[3] : ENDSTOP MIN (Physical)
// Pin[4] : ENDSTOP MAX (Physical)
//
// PARAMS MAPPING:
// - params[0] : stepsPerUnit
// - params[1] : maxLimit (Virtual)
// - params[2] : minStepDelay (us)
// - params[3] : homingBackoffSteps
// - params[4] : homingPostOffset
// ==========================================

// ==========================================
// DEVICE_PWM
// PIN MAPPING:
// - Pin[0] : OUTPUT PWM
// - Pin[1] : FEEDBACK PWM
//
// PARAMS MAPPING:
// - params[0] : Scale / Max Value / Sensitivity (Depends on sensorType)
// - params[1] : Offset / Zero-G / Timeout      (Depends on sensorType)
// - params[2] : Kp (PID Proportional gain)
// - params[3] : Ki (PID Integral gain)
// - params[4] : Kd (PID Derivative gain)
// - params[5] : Series Resistor (for NTC thermistor)
// - params[6] : Nominal Resistance (for NTC thermistor)
// - params[7] : Beta Coefficient (for NTC thermistor)
// - params[8] : Sensor Type (1: RAW, 2: Temp, 3: RPM, 4: Velocity, 5: Accel, 6: Frequency)
// - params[9] : Delay in microseconds per read (usPerRead)
// - params[10]: Number of samples for averaging (sampleCount)
// ==========================================

// ==========================================
// DEVICE_GPIO (Universal GPIO Configuration)
// PIN MAPPING:
// - Pin[0] : Target Pin Number
//
// PARAMS MAPPING:
// - params[0] : Mode Select (1.0f = Output Mode, 0.0f or Default = Input Pullup Mode)
// - params[1] : Initial State / Default Value (0 = LOW/Off, 1 = HIGH/On) - Used for Output only
// - params[2] : Pull Mode (0: Pull-Up, 1: Pull-Down, 2: Floating) - Used for Input extension
// - params[3] : Active Level (0: Active Low, 1: Active High) - Used for Input logic
// - params[4] : Debounce Delay in ms (Anti-jitter filter)
// ==========================================

// ==========================================
// DEVICE_PIPELINE (Logic Interlock & Automation Pipeline)
// PIN MAPPING:
// - Pin[0] : Source Device Index (Index of the source entity in hardwareEntities to read from)
// - Pin[1] : Target Device Index (Index of the target entity in hardwareEntities to write to)
//
// PARAMS MAPPING:
// - params[0] : Min Trigger Threshold (Triggers if source value is less than this value)
// - params[1] : Max Trigger Threshold (Triggers if source value is greater than this value)
// - params[2] : Value on True (Value injected into target device if either condition above is met)
// - params[3] : Value on False (Value injected into target device if no conditions are met.
//                             If omitted or unconfigured, the direct source value will be passed through)
// ==========================================

inline std::vector<HardwareEntity> hardwareEntities = {
    //Name                   Type                          Pins                             Params                                                          Runtime States
    { "X",          DEVICE_AXIS_LINEAR_LIMITED,    {13, 12,  6, 48, 38},    {80.0f, 350.0f, 25.0f,  0.0f,  0.0f},                                           0,    0.0f,   0.0f },
    { "Y",          DEVICE_AXIS_LINEAR_LIMITED,    {10,  9, 11, 39, 40},    {80.0f, 170.0f, 25.0f,  0.0f,  0.0f},                                           0,    0.0f,   0.0f },
    { "Z",          DEVICE_AXIS_LINEAR_LIMITED,    { 5,  7,  8, 41, -1},    {400.0f, 320.0f, 50.0f,  0.0f,  0.0f},                                          0,    0.0f,   0.0f },
    { "E0",         DEVICE_AXIS_ROTARY_UNLIMITED,  {16, 17, 18, -1, -1},    {100.0f,   0.0f, 50.0f,  0.0f,  0.0f},                                          0,    0.0f,   0.0f },
    { "HEATER_0",   DEVICE_PWM,                {21, 1},                 {0.0f,   0.0f, 22.2f, 1.08f, 114.0f,  4700.0f, 100000.0f, 3950.0f, 2.0f, 10, 10},       0,    0.0f,   0.0f },
    { "FAN",        DEVICE_PWM,                {14, 1},                 {0.0f,   0.0f, 22.2f, 1.08f, 114.0f, 0, 0, 0, 1, 10, 10},                                           0,    0.0f,   0.0f },
    { "DOOR_SW",    DEVICE_GPIO,                {2},                     {0.0f,   0.0f,  0.0f,  0.0f,   0.0f},                                           0,    0.0f,   0.0f }
};

inline const char* WIFI_SSID = "YOUR_NEIGHBOR_SSID";
inline const char* WIFI_PASS = "PASSWORD_YOU_KNEW";

inline void initHardwareECS() {
    for (auto& ent : hardwareEntities) {
        if (isAxisDevice(ent.type)) {
            if (ent.pins.size() >= 3) {
                pinMode(ent.pins[0], OUTPUT);
                pinMode(ent.pins[1], OUTPUT);
                pinMode(ent.pins[2], OUTPUT);
                digitalWrite(ent.pins[2], LOW);
            }
            if (ent.pins.size() >= 4 && ent.pins[3] != -1) pinMode(ent.pins[3], INPUT_PULLUP);
            if (ent.pins.size() >= 5 && ent.pins[4] != -1) pinMode(ent.pins[4], INPUT_PULLUP);
        }
        else if (ent.type == DEVICE_PWM) {
            if (ent.pins.size() >= 2) pinMode(ent.pins[1], INPUT);
        }
        else if (ent.type == DEVICE_GPIO) {
            if (ent.pins.size() >= 1) {
                int isOutput = (ent.params.size() >= 1 && ent.params[0] == 1.0f) ? 1 : 0;
                if (isOutput) {
                    pinMode(ent.pins[0], OUTPUT);
                    int initState = (ent.params.size() >= 2) ? (int)ent.params[1] : 0;
                    digitalWrite(ent.pins[0], initState ? HIGH : LOW);
                    ent.runtimeState1 = initState;
                } else {
                    pinMode(ent.pins[0], INPUT_PULLUP);
                }
            }
        }
    }

    size_t detectedAxes = 0;
    for (const auto& ent : hardwareEntities) {
        if (isAxisDevice(ent.type)) detectedAxes++;
    }
    motionPipeline.init(detectedAxes);
}
