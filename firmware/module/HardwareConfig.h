// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

// ==========================================
// 1. DEVICE_AXIS (Motion Motor & Endstop Control)
// ==========================================
// PIN MAPPING:
// - Pin[0] : STEP Pin
// - Pin[1] : DIR Pin
// - Pin[2] : EN (Enable) Pin
// - Pin[3] : ENDSTOP MIN (Physical Pin, or -1 for Virtual/Disabled)
// - Pin[4] : ENDSTOP MAX (Physical Pin, or -1 for Virtual/Disabled)
//
// PARAMS MAPPING:
// - params[0] : stepsPerUnit (Steps required per mm or degree)
// - params[1] : maxLimit (Virtual software limit or boundary)
// - params[2] : minStepDelay (Minimum delay in microseconds per step)
// - params[3] : homingBackoffSteps (Steps to back off after hitting endstop)
// - params[4] : homingPostOffset (Offset position after homing completion)
// ==========================================

// ==========================================
// 2. DEVICE_PWM_SENSOR (Thermal, PID Control & Sensor Monitoring)
// ==========================================
// PIN MAPPING:
// - Pin[0] : OUTPUT PWM Pin (Set to -1 if device acts as a pure Sensor / Virtual device)
// - Pin[1] : FEEDBACK ANALOG / SENSOR Pin (Set to -1 if not used)
//
// PARAMS MAPPING:
// - params[0] : Target Value / Setpoint (e.g., Target Temperature or PWM value)
// - params[1] : Current Value (Read from sensor, updated dynamically by task)
// - params[2] : Kp (PID Proportional gain)
// - params[3] : Ki (PID Integral gain)
// - params[4] : Kd (PID Derivative gain)
// - params[5] : Series Resistor (for NTC thermistor calculation)
// - params[6] : Nominal Resistance (for NTC thermistor calculation)
// - params[7] : Beta Coefficient (for NTC thermistor calculation)
// - params[8] : Sensor Type (1: RAW, 2: Temp, 3: RPM, 4: Velocity, 5: Accel, 6: Frequency)
// - params[9] : Delay in microseconds per read (usPerRead)
// - params[10]: Number of samples for averaging (sampleCount)
// ==========================================

// ==========================================
// 3. DEVICE_GPIO (Universal GPIO Configuration)
// ==========================================
// PIN MAPPING:
// - Pin[0] : Target MCU Pin Number
//
// PARAMS MAPPING:
// - params[0] : Mode Select (1.0f = Output Mode, 0.0f or Default = Input Pullup Mode)
// - params[1] : Initial State / Default Value (0 = LOW/Off, 1 = HIGH/On) - Output only
// - params[2] : Pull Mode (0: Pull-Up, 1: Pull-Down, 2: Floating) - Input extension
// - params[3] : Active Level (0: Active Low, 1: Active High) - Input logic
// - params[4] : Debounce Delay in ms (Anti-jitter filter)
// ==========================================

// ==========================================
// 4. DEVICE_PIPELINE (Logic Interlock & Automation Pipeline)
// ==========================================
// PIN MAPPING & MODES:
//
// A. IF-ELSE MODE (2 Pins: Source -> Condition -> Target):
// - Pin[0] : Source Device Index (Index in hardwareEntities to read from)
// - Pin[1] : Target Device Index (Index in hardwareEntities to write to)
// PARAMS MAPPING (Standard):
// - params[0] : Min Trigger Threshold (Triggers if source < this value)
// - params[1] : Max Trigger Threshold (Triggers if source > this value)
// - params[2] : Value on True (Injected into target if condition is met)
// - params[3] : Value on False (Injected into target if no condition is met)
//
// B. FMA CALCULATION MODE (4 Pins: Formula (A * B) + C -> Direct Target):
// - Pin[0] : Index A (Source device index, or -1 to use constant from params[0])
// - Pin[1] : Index B (Source device index, or -1 to use constant from params[1])
// - Pin[2] : Index C (Source device index, or -1 to use constant from params[2])
// - Pin[3] : Target Device Index (Index in hardwareEntities to write final result)
// PARAMS MAPPING (FMA):
// - params[0] : Constant A value (Used if Pin[0] == -1)
// - params[1] : Constant B value (Used if Pin[1] == -1)
// - params[2] : Constant C value (Used if Pin[2] == -1)
// - params[3] : (Reserved)
// ==========================================

inline std::vector<HardwareEntity> hardwareEntities = {
    //Name                   Type                          Pins                             Params                                                          Runtime States
    { "X",          DEVICE_AXIS,    {13, 12,  6, 48, 38},    {80.0f, 350.0f, 25.0f,  0.0f,  0.0f},                                           0,    0.0f,   0.0f },
    { "Y",          DEVICE_AXIS,    {10,  9, 11, 39, 40},    {80.0f, 170.0f, 25.0f,  0.0f,  0.0f},                                           0,    0.0f,   0.0f },
    { "Z",          DEVICE_AXIS,    { 5,  7,  8, 41, -1},    {400.0f, 320.0f, 50.0f,  0.0f,  0.0f},                                          0,    0.0f,   0.0f },
    { "E0",         DEVICE_AXIS,  {16, 17, 18, -1, -1},    {100.0f,   0.0f, 50.0f,  0.0f,  0.0f},                                          0,    0.0f,   0.0f },
    { "HEATER_0",   DEVICE_PWM_SENSOR,                {21, 1},                 {0.0f,   0.0f, 22.2f, 1.08f, 114.0f,  4700.0f, 100000.0f, 3950.0f, 2.0f, 10, 10},       0,    0.0f,   0.0f },
    { "FAN",        DEVICE_PWM_SENSOR,                {14, 1},                 {0.0f,   0.0f, 22.2f, 1.08f, 114.0f, 0, 0, 0, 1, 10, 10},                                           0,    0.0f,   0.0f },
    { "DOOR_SW",    DEVICE_GPIO,                {2},                     {0.0f,   0.0f,  0.0f,  0.0f,   0.0f},                                           0,    0.0f,   0.0f }
};

inline const char* WIFI_SSID = "YOUR_SSID";
inline const char* WIFI_PASS = "I_DONT_KNOW";

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
        else if (ent.type == DEVICE_PWM_SENSOR) {
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
