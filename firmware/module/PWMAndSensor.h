// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

/**
 * @brief Reads and decodes analog or frequency sensor data based on entity parameters.
 * Note: Must be called outside critical sections to prevent blocking/watchdog resets.
 */

float readAnalogPin(uint8_t pin, const std::vector<float>& params_float) {
    if (pin == -1) return 0;

    int sensorType       = (params_float.size() >= 9)  ? (int)params_float[8] : 1;
    uint32_t delayUs     = (params_float.size() >= 10) ? (uint32_t)params_float[9] : 5;
    uint32_t sampleCount = (params_float.size() >= 11) ? (uint32_t)params_float[10] : 5;

    // 6. Frequency (Hz) - Direct pulse measurement
    if (sensorType == 6) {
        unsigned long timeout = (params_float.size() >= 1 && params_float[0] > 0) ? (unsigned long)params_float[0] : 100000UL;
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
        return rawADC * (VREF / 4095.0f);
    }

    // 2. Temperature (°C - NTC Thermistor)
    else if (sensorType == 2) {
        float seriesResistor    = (params_float.size() >= 6 && params_float[5] > 0) ? params_float[5] : 4700.0f;
        float nominalResistance = (params_float.size() >= 7 && params_float[6] > 0) ? params_float[6] : 100000.0f;
        float betaCoefficient   = (params_float.size() >= 8 && params_float[7] > 0) ? params_float[7] : 3950.0f;

        float r_ntc = seriesResistor * rawADC / (4095.0f - rawADC);
        float invT  = (log(r_ntc / nominalResistance) / betaCoefficient) + 0.003354016f;
        return (1.0f / invT) - 273.15f;
    }

    // 3. RPM (Rotations Per Minute)
    else if (sensorType == 3) {
        float maxRpm = (params_float.size() >= 1 && params_float[0] > 0) ? params_float[0] : 3000.0f;
        return (rawADC / 4095.0f) * maxRpm;
    }

    // 4. Velocity (v - m/s)
    else if (sensorType == 4) {
        float scale  = (params_float.size() >= 1 && params_float[0] > 0) ? params_float[0] : 1.0f;
        float offset = (params_float.size() >= 2) ? params_float[1] : 0.0f;
        float voltage = rawADC * (VREF / 4095.0f);
        return (voltage * scale) + offset;
    }

    // 5. Acceleration (a - g)
    else if (sensorType == 5) {
        float sensitivity  = (params_float.size() >= 1 && params_float[0] > 0) ? params_float[0] : 0.330f;
        float zeroGVoltage = (params_float.size() >= 2) ? params_float[1] : 1.65f;
        float voltage = rawADC * (VREF / 4095.0f);
        return (voltage - zeroGVoltage) / sensitivity;
    }

    return 0.0f;
}

/**
 * @brief Background FreeRTOS task handling sensor reading, PID computation, and PWM control.
 */
void SensorAndPWMTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(200);
    uint64_t lastMicros = SYS_GET_TIME_US();

    for (;;) {
        uint64_t currentMicros = SYS_GET_TIME_US();
        float dt = (float)(currentMicros - lastMicros) / 1000000.0f;
        lastMicros = currentMicros;

        if (dt <= 0.001f || dt > 1.0f) {
            dt = 0.2f;
        }

        // Read analog sensors OUTSIDE the critical section to prevent blocking / watchdog reset
        for (auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM_SENSOR && ent.params_int.size() >= 2 && ent.params_int[1] != -1 && ent.params_float.size() >= 5) {
                float current = readAnalogPin(ent.params_int[1], ent.params_float);

                SYS_ENTER_CRITICAL();
                ent.params_float[1] = current; // Safe update to params_float cache
                SYS_EXIT_CRITICAL();
            }
        }

        // PID calculation and PWM hardware update inside critical section
        SYS_ENTER_CRITICAL();
        for (auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM_SENSOR && !ent.params_int.empty() && ent.params_int[0] != -1 && ent.params_float.size() >= 5) {
                uint8_t pin = ent.params_int[0];
                float target = ent.params_float[0];
                float current = ent.params_float[1];

                if (current >= (target + 15.0f)) {
                    ledcWrite(pin, 0);
                    ent.runtimeState1 = 0;
                    ent.runtimeState2 = 0;
                    ent.runtimeState3 = 0;
                } else {
                    float error = target - current;
                    float Kp = ent.params_float[2], Ki = ent.params_float[3], Kd = ent.params_float[4];

                    ent.runtimeState2 = constrain(ent.runtimeState2 + (error * dt), -100.0f, 100.0f);
                    float derivative = (error - ent.runtimeState3) / dt;
                    ent.runtimeState3 = error;

                    int pwmValue = constrain((int)((Kp * error) + (Ki * ent.runtimeState2) + (Kd * derivative)), 0, 255);

                    ledcWrite(pin, pwmValue);
                    ent.runtimeState1 = pwmValue;
                }
            }
        }
        SYS_EXIT_CRITICAL();

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief Handles WebSocket command to update target setpoint for PWM/Sensor devices.
 */
void handleSetPWM(AsyncWebSocketClient *client, char* payload) {
    char deviceName[20];
    float targetT = 0;
    if (sscanf(payload, "%19[^,],%f", deviceName, &targetT) == 2) {
        SYS_ENTER_CRITICAL();
        for (auto& ent : hardwareEntities) {
            if (ent.name == deviceName && ent.type == DEVICE_PWM_SENSOR && !ent.params_float.empty()) {
                ent.params_float[0] = targetT;
                SYS_EXIT_CRITICAL();
                client->text("ACK");
                return;
            }
        }
        SYS_EXIT_CRITICAL();
    }
    client->text("ERR_DEV");
}

/**
 * @brief Immediately halts PWM operations and disables active outputs for emergency safety.
 */

void handleEmergencyPWMAndSensorStop() {
    SYS_ENTER_CRITICAL();
    for (auto& ent : hardwareEntities) {
        if (ent.type == DEVICE_PWM_SENSOR && !ent.params_float.empty()) {
            ent.params_float[0] = 0.0f;
            if (!ent.params_int.empty()) ledcWrite(ent.params_int[0], 0);
        }
    }
    SYS_EXIT_CRITICAL();
}

/**
 * @brief Scans hardware entities and initializes PWM output channels and analog sensor feedback params_int.
 */
inline void initPWMAndSensorSubsystem() {
    for (auto& ent : hardwareEntities) {
        if (ent.type == DEVICE_PWM_SENSOR) {
            // Initialize PWM output pin if valid
            if (!ent.params_int.empty() && ent.params_int[0] != -1) {
                ledcAttach(ent.params_int[0], 5000, 8);
            }
            // Initialize Analog sensor feedback pin if valid
            if (ent.params_int.size() >= 2 && ent.params_int[1] != -1) {
                pinMode(ent.params_int[1], INPUT);
            }
        }
    }
}
