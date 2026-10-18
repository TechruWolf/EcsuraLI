// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

volatile uint32_t systemLoopTimeUs = 0;
volatile uint32_t freeHeapBytes = 0;
volatile uint32_t totalHeapBytes = 0;

void SensorAndPWMTask(void *pvParameters) {
    portENTER_CRITICAL(&hardwareMux);
    for (auto& ent : hardwareEntities) {
        if (ent.type == DEVICE_PWM_SENSOR && !ent.pins.empty() && ent.pins[0] != -1) {
            ledcAttach(ent.pins[0], 5000, 8);
        }
    }
    portEXIT_CRITICAL(&hardwareMux);

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
            if (ent.type == DEVICE_PWM_SENSOR && ent.pins.size() >= 2 && ent.pins[1] != -1 && ent.params.size() >= 5) {
                float current = readAnalogPin(ent.pins[1], ent.params);

                portENTER_CRITICAL(&hardwareMux);
                ent.params[1] = current;
                portEXIT_CRITICAL(&hardwareMux);
            }
        }

        portENTER_CRITICAL(&hardwareMux);
        for (auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM_SENSOR && !ent.pins.empty() && ent.pins[0] != -1 && ent.params.size() >= 5) {
                uint8_t pin = ent.pins[0];
                float target = ent.params[0];
                float current = ent.params[1];

                if (current >= (target + 15.0f)) {
                    ledcWrite(pin, 0);
                    ent.runtimeState1 = 0;
                    ent.runtimeState2 = 0;
                    ent.runtimeState3 = 0;
                } else {
                    float error = target - current;
                    float Kp = ent.params[2], Ki = ent.params[3], Kd = ent.params[4];

                    ent.runtimeState2 = constrain(ent.runtimeState2 + (error * dt), -100.0f, 100.0f);
                    float derivative = (error - ent.runtimeState3) / dt;
                    ent.runtimeState3 = error;

                    int pwmValue = constrain((int)((Kp * error) + (Ki * ent.runtimeState2) + (Kd * derivative)), 0, 255);

                    ledcWrite(pin, pwmValue);
                    ent.runtimeState1 = pwmValue;
                }
            }
        }
        portEXIT_CRITICAL(&hardwareMux);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        portENTER_CRITICAL(&hardwareMux);
        for (auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM_SENSOR && !ent.pins.empty() && ent.pins[0] != -1) {
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

            float finalValToTarget = 0.0f;
            int targetIdx = -1;

            if (pipelineEnt.pins.size() >= 4) {
                int idxA = pipelineEnt.pins[0];
                float valA = 0.0f;
                if (idxA >= 0 && idxA < hardwareEntities.size()) {
                    auto& srcA = hardwareEntities[idxA];
                    valA = (srcA.type == DEVICE_PWM_SENSOR) ? ((srcA.params.size() >= 2) ? srcA.params[1] : (srcA.params.empty() ? 0.0f : srcA.params[0]))
                    : (srcA.type == DEVICE_GPIO) ? (float)srcA.runtimeState1 : srcA.runtimeState2;
                } else {
                    valA = (pipelineEnt.params.size() > 0) ? pipelineEnt.params[0] : 0.0f;
                }

                int idxB = pipelineEnt.pins[1];
                float valB = 0.0f;
                if (idxB >= 0 && idxB < hardwareEntities.size()) {
                    auto& srcB = hardwareEntities[idxB];
                    valB = (srcB.type == DEVICE_PWM_SENSOR) ? ((srcB.params.size() >= 2) ? srcB.params[1] : (srcB.params.empty() ? 0.0f : srcB.params[0]))
                    : (srcB.type == DEVICE_GPIO) ? (float)srcB.runtimeState1 : srcB.runtimeState2;
                } else {
                    valB = (pipelineEnt.params.size() > 1) ? pipelineEnt.params[1] : 1.0f;
                }

                int idxC = pipelineEnt.pins[2];
                float valC = 0.0f;
                if (idxC >= 0 && idxC < hardwareEntities.size()) {
                    auto& srcC = hardwareEntities[idxC];
                    valC = (srcC.type == DEVICE_PWM_SENSOR) ? ((srcC.params.size() >= 2) ? srcC.params[1] : (srcC.params.empty() ? 0.0f : srcC.params[0]))
                    : (srcC.type == DEVICE_GPIO) ? (float)srcC.runtimeState1 : srcC.runtimeState2;
                } else {
                    valC = (pipelineEnt.params.size() > 2) ? pipelineEnt.params[2] : 0.0f;
                }

                finalValToTarget = (valA * valB) + valC;
                targetIdx = pipelineEnt.pins[3];

            }
            else {
                int sourceIdx = pipelineEnt.pins[0];
                targetIdx = pipelineEnt.pins[1];

                if (sourceIdx < 0 || sourceIdx >= hardwareEntities.size()) continue;

                auto& src = hardwareEntities[sourceIdx];
                float sourceVal = (src.type == DEVICE_PWM_SENSOR) ? ((src.params.size() >= 2) ? src.params[1] : (src.params.empty() ? 0.0f : src.params[0]))
                : (src.type == DEVICE_GPIO) ? (float)src.runtimeState1 : src.runtimeState2;

                float minTrigger = (pipelineEnt.params.size() >= 1) ? pipelineEnt.params[0] : -999999.0f;
                float maxTrigger = (pipelineEnt.params.size() >= 2) ? pipelineEnt.params[1] : 999999.0f;
                bool hasMin = (pipelineEnt.params.size() >= 1);
                bool hasMax = (pipelineEnt.params.size() >= 2);

                bool conditionMet = false;
                if (hasMin && sourceVal < minTrigger) conditionMet = true;
                if (hasMax && sourceVal > maxTrigger) conditionMet = true;

                finalValToTarget = sourceVal;
                if (conditionMet) {
                    if (pipelineEnt.params.size() >= 3) finalValToTarget = pipelineEnt.params[2];
                } else {
                    if (pipelineEnt.params.size() >= 4) {
                        finalValToTarget = pipelineEnt.params[3];
                    } else {
                        continue;
                    }
                }
            }

            if (targetIdx < 0 || targetIdx >= hardwareEntities.size()) continue;

            auto& tgt = hardwareEntities[targetIdx];
            if (tgt.type == DEVICE_PWM_SENSOR && !tgt.params.empty()) {
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
        portENTER_CRITICAL(&hardwareMux);
        uint32_t loopTime = systemLoopTimeUs;
        uint32_t freeH = freeHeapBytes;
        uint32_t totalH = totalHeapBytes;
        portEXIT_CRITICAL(&hardwareMux);

        int offset = snprintf(statusMsg, sizeof(statusMsg), "STATUS,%u,%u,%u", loopTime, freeH, totalH);

        portENTER_CRITICAL(&hardwareMux);

        for (const auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM_SENSOR) {
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

void systemMonitorTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);

    for (;;) {
        uint64_t startTime = SYS_GET_TIME_US();

        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();

        portENTER_CRITICAL(&hardwareMux);
        size_t count = hardwareEntities.size();
        for(size_t i = 0; i < count; i++) {
            volatile auto& ent = hardwareEntities[i];
            (void)ent.type;
        }
        portEXIT_CRITICAL(&hardwareMux);

        uint64_t elapsedTime = SYS_GET_TIME_US() - startTime;

        portENTER_CRITICAL(&hardwareMux);
        systemLoopTimeUs = (uint32_t)elapsedTime;
        freeHeapBytes = freeHeap;
        totalHeapBytes = totalHeap;
        portEXIT_CRITICAL(&hardwareMux);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

inline void initSystemTasks() {
    xTaskCreatePinnedToCore(SensorAndPWMTask, "PIDTask", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(telemetryTask, "TelemetryTask", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(motionControlTask, "MotionTask", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(PipelineTask, "PipelineTask", 4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(systemMonitorTask, "SysMonitorTask", 2048, NULL, 1, NULL, 0);
}
