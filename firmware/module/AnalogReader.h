// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

float readAnalogPin(uint8_t pin, const std::vector<float>& params) {
    if (pin == -1) return 0;

    int sensorType       = (params.size() >= 9)  ? (int)params[8] : 1;
    uint32_t delayUs     = (params.size() >= 10) ? (uint32_t)params[9] : 5;
    uint32_t sampleCount = (params.size() >= 11) ? (uint32_t)params[10] : 5;

    // 6. Frequency (Hz) - Direct pulse measurement
    if (sensorType == 6) {
        unsigned long timeout = (params.size() >= 1 && params[0] > 0) ? (unsigned long)params[0] : 100000UL;
        unsigned long highPulse = SYS_PULSE_IN(pin, HIGH, timeout);
        unsigned long lowPulse  = SYS_PULSE_IN(pin, LOW, timeout);

        if (highPulse == 0 || lowPulse == 0) return 0.0f;
        return 1000000.0f / (float)(highPulse + lowPulse);
    }

    // ADC-based sensors (RAW, Temp, RPM, Velocity, Acceleration)
    uint32_t adcSum = 0;
    for (uint32_t i = 0; i < sampleCount; i++) {
        adcSum += SYS_ADC_READ(pin);
        SYS_DELAY_US(delayUs);
    }
    float rawADC = (float)adcSum / (float)sampleCount;

    // 1. RAW Voltage (V)
    if (sensorType == 1) {
        return rawADC * (3.3f / 4095.0f);
    }

    // 2. Temperature (°C - NTC Thermistor)
    else if (sensorType == 2) {
        float seriesResistor    = (params.size() >= 6 && params[5] > 0) ? params[5] : 4700.0f;
        float nominalResistance = (params.size() >= 7 && params[6] > 0) ? params[6] : 100000.0f;
        float betaCoefficient   = (params.size() >= 8 && params[7] > 0) ? params[7] : 3950.0f;

        float r_ntc = seriesResistor * rawADC / (4095.0f - rawADC);
        float invT  = (log(r_ntc / nominalResistance) / betaCoefficient) + 0.003354016f;
        return (1.0f / invT) - 273.15f;
    }

    // 3. RPM (Rotations Per Minute)
    else if (sensorType == 3) {
        float maxRpm = (params.size() >= 1 && params[0] > 0) ? params[0] : 3000.0f;
        return (rawADC / 4095.0f) * maxRpm;
    }

    // 4. Velocity (v - m/s)
    else if (sensorType == 4) {
        float scale  = (params.size() >= 1 && params[0] > 0) ? params[0] : 1.0f;
        float offset = (params.size() >= 2) ? params[1] : 0.0f;
        float voltage = rawADC * (3.3f / 4095.0f);
        return (voltage * scale) + offset;
    }

    // 5. Acceleration (a - g)
    else if (sensorType == 5) {
        float sensitivity  = (params.size() >= 1 && params[0] > 0) ? params[0] : 0.330f;
        float zeroGVoltage = (params.size() >= 2) ? params[1] : 1.65f;
        float voltage = rawADC * (3.3f / 4095.0f);
        return (voltage - zeroGVoltage) / sensitivity;
    }

    return 0.0f;
}
