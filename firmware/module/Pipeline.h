// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

// --- Auxiliary Data Structures ---
struct EntityCopy {
    decltype(hardwareEntities[0].type) type;
    std::vector<float> params_float;
    std::vector<int> params_int;
    int runtimeState1;
};

struct PipelineUpdate {
    int targetIdx;
    float finalVal;
};

// --- 1. Update System Variables (DEVICE_VAR) ---
inline void updateDeviceVariables() {
    SYS_ENTER_CRITICAL();
    for (auto& varEnt : hardwareEntities) {
        if (varEnt.type != DEVICE_VAR) continue;
        int varMode = varEnt.params_int.empty() ? 0 : varEnt.params_int[0];

        switch (varMode) {
            case 1:
                if (varEnt.params_float.empty()) varEnt.params_float.push_back(0.0f);
                varEnt.params_float[0] = (float)millis();
            break;
            case 2: {
                if (varEnt.params_float.empty()) varEnt.params_float.push_back(0.0f);
                float minVal = (varEnt.params_float.size() >= 2) ? varEnt.params_float[1] : 0.0f;
                float maxVal = (varEnt.params_float.size() >= 3) ? varEnt.params_float[2] : 100.0f;
                varEnt.params_float[0] = minVal + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (maxVal - minVal)));
                break;
            }
            default:
                break;
        }
    }
    SYS_EXIT_CRITICAL();
}

// --- 2. Take a Safe Snapshot of hardwareEntities ---
inline void takeEntitiesSnapshot(std::vector<EntityCopy>& entitiesSnap) {
    SYS_ENTER_CRITICAL();
    entitiesSnap.clear();
    entitiesSnap.reserve(hardwareEntities.size());
    for (const auto& ent : hardwareEntities) {
        EntityCopy ec;
        ec.type = ent.type;
        ec.params_float = ent.params_float;
        ec.runtimeState1 = ent.runtimeState1;

        ec.params_int.clear();
        for (auto p : ent.params_int) {
            ec.params_int.push_back(static_cast<int>(p));
        }

        entitiesSnap.push_back(ec);
    }
    SYS_EXIT_CRITICAL();
}

// --- Helper Function to Get Value from Snapshot ---
inline float getSnapValue(const EntityCopy& ent) {
    if (ent.type == DEVICE_PWM_SENSOR) {
        return (ent.params_float.size() >= 2) ? ent.params_float[1] : (ent.params_float.empty() ? 0.0f : ent.params_float[0]);
    } else if (ent.type == DEVICE_GPIO) {
        return (float)ent.runtimeState1;
    } else if (ent.type == DEVICE_VAR) {
        return ent.params_float.empty() ? 0.0f : ent.params_float[0];
    }
    return 0.0f;
}

// --- 3. Evaluate Pipeline Logic and Compute Output Values ---
inline void evaluatePipelines(const std::vector<EntityCopy>& entitiesSnap, std::vector<PipelineUpdate>& pendingUpdates) {
    for (size_t pIdx = 0; pIdx < entitiesSnap.size(); ++pIdx) {
        const auto& pipelineEnt = entitiesSnap[pIdx];
        if (pipelineEnt.type != DEVICE_PIPELINE) continue;
        if (pipelineEnt.params_int.size() < 2) continue;

        float finalValToTarget = 0.0f;
        int targetIdx = -1;

        // Formula calculation case: (A * B) + C
        if (pipelineEnt.params_int.size() >= 4) {
            int idxA = pipelineEnt.params_int[0];
            float valA = (idxA >= 0 && (size_t)idxA < entitiesSnap.size()) ? getSnapValue(entitiesSnap[idxA]) : ((pipelineEnt.params_float.size() > 0) ? pipelineEnt.params_float[0] : 0.0f);

            int idxB = pipelineEnt.params_int[1];
            float valB = (idxB >= 0 && (size_t)idxB < entitiesSnap.size()) ? getSnapValue(entitiesSnap[idxB]) : ((pipelineEnt.params_float.size() > 1) ? pipelineEnt.params_float[1] : 1.0f);

            int idxC = pipelineEnt.params_int[2];
            float valC = (idxC >= 0 && (size_t)idxC < entitiesSnap.size()) ? getSnapValue(entitiesSnap[idxC]) : ((pipelineEnt.params_float.size() > 2) ? pipelineEnt.params_float[2] : 0.0f);

            finalValToTarget = (valA * valB) + valC;
            targetIdx = pipelineEnt.params_int[3];
        }
        // Min/Max trigger condition case
        else {
            int sourceIdx = pipelineEnt.params_int[0];
            targetIdx = pipelineEnt.params_int[1];

            if (sourceIdx < 0 || (size_t)sourceIdx >= entitiesSnap.size()) continue;

            float sourceVal = getSnapValue(entitiesSnap[sourceIdx]);

            float minTrigger = (pipelineEnt.params_float.size() >= 1) ? pipelineEnt.params_float[0] : -999999.0f;
            float maxTrigger = (pipelineEnt.params_float.size() >= 2) ? pipelineEnt.params_float[1] : 999999.0f;
            bool hasMin = (pipelineEnt.params_float.size() >= 1);
            bool hasMax = (pipelineEnt.params_float.size() >= 2);

            bool conditionMet = false;
            if (hasMin && sourceVal < minTrigger) conditionMet = true;
            if (hasMax && sourceVal > maxTrigger) conditionMet = true;

            finalValToTarget = sourceVal;
            if (conditionMet) {
                if (pipelineEnt.params_float.size() >= 3) finalValToTarget = pipelineEnt.params_float[2];
            } else {
                if (pipelineEnt.params_float.size() >= 4) {
                    finalValToTarget = pipelineEnt.params_float[3];
                } else {
                    finalValToTarget = sourceVal;
                }
            }
        }

        if (targetIdx >= 0 && (size_t)targetIdx < entitiesSnap.size()) {
            pendingUpdates.push_back({targetIdx, finalValToTarget});
        }
    }
}

// --- 4. Commit Update Results to hardwareEntities (Critical Section) ---
inline void commitPipelineUpdates(const std::vector<PipelineUpdate>& pendingUpdates) {
    SYS_ENTER_CRITICAL();
    for (const auto& update : pendingUpdates) {
        if (update.targetIdx < 0 || (size_t)update.targetIdx >= hardwareEntities.size()) continue;
        auto& tgt = hardwareEntities[update.targetIdx];
        if (tgt.type == DEVICE_PWM_SENSOR && !tgt.params_float.empty()) {
            tgt.params_float[0] = update.finalVal;
        } else if (tgt.type == DEVICE_GPIO) {
            tgt.runtimeState1 = (int)update.finalVal;
        } else if (tgt.type == DEVICE_VAR) {
            if (tgt.params_float.empty()) {
                tgt.params_float.push_back(0.0f);
            }
            tgt.params_float[0] = update.finalVal;
        }
    }
    SYS_EXIT_CRITICAL();
}

// --- 5. Execute Actual Hardware Control Actions ---
inline void executeHardwareActions(const std::vector<PipelineUpdate>& pendingUpdates) {
    for (const auto& update : pendingUpdates) {
        if (update.targetIdx < 0 || (size_t)update.targetIdx >= hardwareEntities.size()) continue;
        const auto& tgt = hardwareEntities[update.targetIdx];
        if (tgt.type == DEVICE_GPIO && !tgt.params_int.empty()) {
            int pinNum = tgt.params_int[0];
            int valInt = (int)update.finalVal;
            if (valInt > 1) {
                ledcWrite(pinNum, constrain(valInt, 0, 255));
            } else {
                digitalWrite(pinNum, valInt ? HIGH : LOW);
            }
        }
    }
}

// --- Main Coordination Task (Clean and Clear) ---
void PipelineTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(200);

    std::vector<EntityCopy> entitiesSnap;
    std::vector<PipelineUpdate> pendingUpdates;

    for (;;) {
        // 1. Update virtual variables
        updateDeviceVariables();

        // 2. Take a safe state snapshot
        takeEntitiesSnapshot(entitiesSnap);

        // 3. Compute pipeline logic
        pendingUpdates.clear();
        evaluatePipelines(entitiesSnap, pendingUpdates);

        // 4. Synchronize results to shared memory
        commitPipelineUpdates(pendingUpdates);

        // 5. Trigger physical hardware
        executeHardwareActions(pendingUpdates);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
