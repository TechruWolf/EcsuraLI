// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

void PWMControlTask(void *pvParameters) {
    for (auto& ent : hardwareEntities) {
        if (ent.type == DEVICE_PWM && !ent.pins.empty()) {
            ledcAttach(ent.pins[0], 5000, 8);
        }
    }

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

        for (auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM && ent.pins.size() >= 2 && ent.params.size() >= 5) {
                uint8_t sensorPin = ent.pins[1];
                float current = readAnalogPin(sensorPin, ent.params);

                portENTER_CRITICAL(&hardwareMux);
                ent.params[1] = current;
                portEXIT_CRITICAL(&hardwareMux);
            }
        }

        portENTER_CRITICAL(&hardwareMux);
        for (auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM && ent.pins.size() >= 2 && ent.params.size() >= 5) {
                uint8_t Pin = ent.pins[0];
                float target = ent.params[0];
                float current = ent.params[1];

                if (current >= (target + 15.0f)) {
                    ledcWrite(Pin, 0);
                    ent.runtimeState1 = 0;
                    ent.runtimeState2 = 0;
                    ent.runtimeState3 = 0;
                }
                else {
                    float error = target - current;
                    float Kp = ent.params[2], Ki = ent.params[3], Kd = ent.params[4];

                    ent.runtimeState2 = constrain(ent.runtimeState2 + (error * dt), -100.0f, 100.0f);
                    float derivative = (error - ent.runtimeState3) / dt;
                    ent.runtimeState3 = error;

                    int pwmValue = constrain((int)((Kp * error) + (Ki * ent.runtimeState2) + (Kd * derivative)), 0, 255);

                    ledcWrite(Pin, pwmValue);
                    ent.runtimeState1 = pwmValue;
                }
            }
        }
        portEXIT_CRITICAL(&hardwareMux);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        portENTER_CRITICAL(&hardwareMux);
        for (auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM && !ent.pins.empty()) {
                ledcWrite(ent.pins[0], 0);
            }
        }
        portEXIT_CRITICAL(&hardwareMux);
    }
}

void PipelineTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(200);

    for (;;) {
        portENTER_CRITICAL(&hardwareMux);

        for (auto& pipelineEnt : hardwareEntities) {
            if (pipelineEnt.type != DEVICE_PIPELINE) continue;
            if (pipelineEnt.pins.size() < 2) continue;

            int sourceIdx = pipelineEnt.pins[0];
            int targetIdx = pipelineEnt.pins[1];

            if (sourceIdx < 0 || sourceIdx >= hardwareEntities.size() ||
                targetIdx < 0 || targetIdx >= hardwareEntities.size()) {
                continue;
            }

            float sourceVal = 0.0f;
            auto& src = hardwareEntities[sourceIdx];
            if (src.type == DEVICE_PWM) {
                sourceVal = (src.params.size() >= 2) ? src.params[1] : src.params[0];
            } else if (src.type == DEVICE_GPIO) {
                sourceVal = (float)src.runtimeState1;
            } else {
                sourceVal = src.runtimeState2;
            }

            float minTrigger = (pipelineEnt.params.size() >= 1) ? pipelineEnt.params[0] : -999999.0f;
            float maxTrigger = (pipelineEnt.params.size() >= 2) ? pipelineEnt.params[1] : 999999.0f;
            bool hasMin = (pipelineEnt.params.size() >= 1);
            bool hasMax = (pipelineEnt.params.size() >= 2);

            bool conditionMet = false;
            if (hasMin && sourceVal < minTrigger) conditionMet = true;
            if (hasMax && sourceVal > maxTrigger) conditionMet = true;

            float finalValToTarget = sourceVal;

            if (conditionMet) {
                if (pipelineEnt.params.size() >= 3) {
                    finalValToTarget = pipelineEnt.params[2];
                }
            } else {
                if (pipelineEnt.params.size() >= 4) {
                    finalValToTarget = pipelineEnt.params[3];
                } else {
                    continue;
                }
            }

            auto& tgt = hardwareEntities[targetIdx];
            if (tgt.type == DEVICE_PWM && !tgt.params.empty()) {
                tgt.params[0] = finalValToTarget;
            } else if (tgt.type == DEVICE_GPIO && !tgt.pins.empty()) {
                int pin = tgt.pins[0];
                int valInt = (int)finalValToTarget;
                if (valInt > 1) {
                    ledcWrite(pin, constrain(valInt, 0, 255));
                } else {
                    digitalWrite(pin, valInt ? HIGH : LOW);
                }
                tgt.runtimeState1 = valInt;
            }
        }

        portEXIT_CRITICAL(&hardwareMux);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void telemetryTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);
    char statusMsg[256];

    for (;;) {
        int offset = snprintf(statusMsg, sizeof(statusMsg), "STATUS");

        portENTER_CRITICAL(&hardwareMux);

        for (const auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM) {
                float currentTemp = (ent.params.size() >= 2) ? ent.params[1] : 0.0f;
                offset += snprintf(statusMsg + offset, sizeof(statusMsg) - offset, ",%.1f", currentTemp);
            }
        }

        for (const auto& ent : hardwareEntities) {
            if (isAxisDevice(ent.type)) {
                float stepsPerUnit = (!ent.params.empty() && ent.params[0] > 0) ? ent.params[0] : 80.0f;
                float currentPos = (float)ent.runtimeState1 / stepsPerUnit;
                offset += snprintf(statusMsg + offset, sizeof(statusMsg) - offset, ",%.2f", currentPos);
            }
        }

        portEXIT_CRITICAL(&hardwareMux);

        ws.textAll(statusMsg);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void motionControlTask(void *pvParameters) {
    for (;;) {
        executeMotion();
        if (motionPipeline.isEmpty()) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

inline void initSystemTasks() {
    xTaskCreatePinnedToCore(PWMControlTask, "PIDTask", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(telemetryTask, "TelemetryTask", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(motionControlTask, "MotionTask", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(PipelineTask, "PipelineTask", 4096, NULL, 1, NULL, 1);
}
