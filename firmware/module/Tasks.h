// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

volatile uint32_t systemLoopTimeUs = 0;
volatile uint32_t freeHeapBytes = 0;
volatile uint32_t totalHeapBytes = 0;

void SensorAndPWMTask(void *pvParameters) {
    SYS_ENTER_CRITICAL();
    for (auto& ent : hardwareEntities) {
        if (ent.type == DEVICE_PWM_SENSOR && !ent.pins.empty() && ent.pins[0] != -1) {
            ledcAttach(ent.pins[0], 5000, 8);
        }
    }
    SYS_EXIT_CRITICAL();

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

                SYS_ENTER_CRITICAL();
                ent.params[1] = current;
                SYS_EXIT_CRITICAL();
            }
        }

        SYS_ENTER_CRITICAL();
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
        SYS_EXIT_CRITICAL();

        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        SYS_ENTER_CRITICAL();
        for (auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM_SENSOR && !ent.pins.empty() && ent.pins[0] != -1) {
                ledcWrite(ent.pins[0], 0);
            }
        }
        SYS_EXIT_CRITICAL();
    }
}

void telemetryTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);
    char statusMsg[1024];

    for (;;) {
        uint32_t loopTime, freeH, totalH;

        struct EntitySnap {
            decltype(hardwareEntities[0].type) type;
            float p0;
            float p1;
            int rState1;
        };
        std::vector<EntitySnap> snapList;

        SYS_ENTER_CRITICAL();
        loopTime = systemLoopTimeUs;
        freeH = freeHeapBytes;
        totalH = totalHeapBytes;

        snapList.reserve(hardwareEntities.size());
        for (const auto& ent : hardwareEntities) {
            EntitySnap s;
            s.type = ent.type;
            s.p0 = ent.params.empty() ? 0.0f : ent.params[0];
            s.p1 = ent.params.size() >= 2 ? ent.params[1] : 0.0f;
            s.rState1 = ent.runtimeState1;
            snapList.push_back(s);
        }
        SYS_EXIT_CRITICAL();

        int offset = snprintf(statusMsg, sizeof(statusMsg), "STATUS,%u,%u,%u", loopTime, freeH, totalH);

        for (const auto& s : snapList) {
            if (s.type == DEVICE_PWM_SENSOR) {
                offset += snprintf(statusMsg + offset, sizeof(statusMsg) - offset, ",%.1f", s.p1);
            }
        }

        for (const auto& s : snapList) {
            if (isAxisDevice(s.type)) {
                float stepsPerUnit = (s.p0 > 0) ? s.p0 : 80.0f;
                float currentPos = (float)s.rState1 / stepsPerUnit;
                offset += snprintf(statusMsg + offset, sizeof(statusMsg) - offset, ",%.2f", currentPos);
            }
        }

        for (const auto& s : snapList) {
            if (s.type == DEVICE_VAR) {
                offset += snprintf(statusMsg + offset, sizeof(statusMsg) - offset, ",%.2f", s.p0);
            }
        }

        ws.textAll(statusMsg);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void PipelineTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(200);

    for (;;) {
        SYS_ENTER_CRITICAL();
        for (auto& varEnt : hardwareEntities) {
            if (varEnt.type != DEVICE_VAR) continue;
            int varMode = varEnt.pins.empty() ? 0 : varEnt.pins[0];

            switch (varMode) {
                case 1:
                    if (varEnt.params.empty()) varEnt.params.push_back(0.0f);
                    varEnt.params[0] = (float)millis();
                break;
                case 2: {
                    if (varEnt.params.empty()) varEnt.params.push_back(0.0f);
                    float minVal = (varEnt.params.size() >= 2) ? varEnt.params[1] : 0.0f;
                    float maxVal = (varEnt.params.size() >= 3) ? varEnt.params[2] : 100.0f;
                    varEnt.params[0] = minVal + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (maxVal - minVal)));
                    break;
                }
                default:
                    break;
            }
        }
        SYS_EXIT_CRITICAL();

        struct EntityCopy {
            decltype(hardwareEntities[0].type) type;
            std::vector<float> params;
            std::vector<int> pins;
            int runtimeState1;
        };
        std::vector<EntityCopy> entitiesSnap;

        SYS_ENTER_CRITICAL();
        entitiesSnap.reserve(hardwareEntities.size());
        for (const auto& ent : hardwareEntities) {
            EntityCopy ec;
            ec.type = ent.type;
            ec.params = ent.params;
            ec.runtimeState1 = ent.runtimeState1;

            ec.pins.clear();
            for (auto p : ent.pins) {
                ec.pins.push_back(static_cast<int>(p));
            }

            entitiesSnap.push_back(ec);
        }
        SYS_EXIT_CRITICAL();

        auto getSnapValue = [](const EntityCopy& ent) -> float {
            if (ent.type == DEVICE_PWM_SENSOR) {
                return (ent.params.size() >= 2) ? ent.params[1] : (ent.params.empty() ? 0.0f : ent.params[0]);
            } else if (ent.type == DEVICE_GPIO) {
                return (float)ent.runtimeState1;
            } else if (ent.type == DEVICE_VAR) {
                return ent.params.empty() ? 0.0f : ent.params[0];
            } else {
                return 0.0f;
            }
        };

        struct PipelineUpdate {
            int targetIdx;
            float finalVal;
        };
        std::vector<PipelineUpdate> pendingUpdates;

        for (size_t pIdx = 0; pIdx < entitiesSnap.size(); ++pIdx) {
            const auto& pipelineEnt = entitiesSnap[pIdx];
            if (pipelineEnt.type != DEVICE_PIPELINE) continue;
            if (pipelineEnt.pins.size() < 2) continue;

            float finalValToTarget = 0.0f;
            int targetIdx = -1;

            if (pipelineEnt.pins.size() >= 4) {
                int idxA = pipelineEnt.pins[0];
                float valA = (idxA >= 0 && (size_t)idxA < entitiesSnap.size()) ? getSnapValue(entitiesSnap[idxA]) : ((pipelineEnt.params.size() > 0) ? pipelineEnt.params[0] : 0.0f);

                int idxB = pipelineEnt.pins[1];
                float valB = (idxB >= 0 && (size_t)idxB < entitiesSnap.size()) ? getSnapValue(entitiesSnap[idxB]) : ((pipelineEnt.params.size() > 1) ? pipelineEnt.params[1] : 1.0f);

                int idxC = pipelineEnt.pins[2];
                float valC = (idxC >= 0 && (size_t)idxC < entitiesSnap.size()) ? getSnapValue(entitiesSnap[idxC]) : ((pipelineEnt.params.size() > 2) ? pipelineEnt.params[2] : 0.0f);

                finalValToTarget = (valA * valB) + valC;
                targetIdx = pipelineEnt.pins[3];
            } else {
                int sourceIdx = pipelineEnt.pins[0];
                targetIdx = pipelineEnt.pins[1];

                if (sourceIdx < 0 || (size_t)sourceIdx >= entitiesSnap.size()) continue;

                float sourceVal = getSnapValue(entitiesSnap[sourceIdx]);

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
                        finalValToTarget = sourceVal;
                    }
                }
            }

            if (targetIdx >= 0 && (size_t)targetIdx < entitiesSnap.size()) {
                pendingUpdates.push_back({targetIdx, finalValToTarget});
            }
        }

        SYS_ENTER_CRITICAL();
        for (const auto& update : pendingUpdates) {
            if (update.targetIdx < 0 || (size_t)update.targetIdx >= hardwareEntities.size()) continue;
            auto& tgt = hardwareEntities[update.targetIdx];
            if (tgt.type == DEVICE_PWM_SENSOR && !tgt.params.empty()) {
                tgt.params[0] = update.finalVal;
            } else if (tgt.type == DEVICE_GPIO) {
                tgt.runtimeState1 = (int)update.finalVal;
            } else if (tgt.type == DEVICE_VAR) {
                if (tgt.params.empty()) {
                    tgt.params.push_back(0.0f);
                }
                tgt.params[0] = update.finalVal;
            }
        }
        SYS_EXIT_CRITICAL();

        for (const auto& update : pendingUpdates) {
            if (update.targetIdx < 0 || (size_t)update.targetIdx >= hardwareEntities.size()) continue;
            const auto& tgt = hardwareEntities[update.targetIdx];
            if (tgt.type == DEVICE_GPIO && !tgt.pins.empty()) {
                int pinNum = tgt.pins[0];
                int valInt = (int)update.finalVal;
                if (valInt > 1) {
                    ledcWrite(pinNum, constrain(valInt, 0, 255));
                } else {
                    digitalWrite(pinNum, valInt ? HIGH : LOW);
                }
            }
        }

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

        SYS_ENTER_CRITICAL();
        size_t count = hardwareEntities.size();
        for(size_t i = 0; i < count; i++) {
            volatile auto& ent = hardwareEntities[i];
            (void)ent.type;
        }
        SYS_EXIT_CRITICAL();

        uint64_t elapsedTime = SYS_GET_TIME_US() - startTime;

        SYS_ENTER_CRITICAL();
        systemLoopTimeUs = (uint32_t)elapsedTime;
        freeHeapBytes = freeHeap;
        totalHeapBytes = totalHeap;
        SYS_EXIT_CRITICAL();

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

inline void initSystemTasks() {
    SYS_TASK_CREATE(SensorAndPWMTask, "PIDTask", 4096, NULL, 1, NULL, 0);
    SYS_TASK_CREATE(telemetryTask, "TelemetryTask", 4096, NULL, 1, NULL, 0);
    SYS_TASK_CREATE(motionControlTask, "MotionTask", 4096, NULL, 2, NULL, 1);
    SYS_TASK_CREATE(PipelineTask, "PipelineTask", 4096, NULL, 1, NULL, 0);
    SYS_TASK_CREATE(systemMonitorTask, "SysMonitorTask", 2048, NULL, 1, NULL, 0);
}
