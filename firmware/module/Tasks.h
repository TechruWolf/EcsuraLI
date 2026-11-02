// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

volatile uint32_t systemLoopTimeUs = 0;
volatile uint32_t freeHeapBytes = 0;
volatile uint32_t totalHeapBytes = 0;

/**
 * @brief Telemetry task responsible for gathering entity states and broadcasting telemetry data via WebSockets.
 */
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

        // 1. Safely copy global telemetry data and entity snapshot under critical section
        SYS_ENTER_CRITICAL();
        loopTime = systemLoopTimeUs;
        freeH = freeHeapBytes;
        totalH = totalHeapBytes;

        snapList.reserve(hardwareEntities.size());
        for (const auto& ent : hardwareEntities) {
            EntitySnap s;
            s.type = ent.type;
            s.p0 = ent.params_float.empty() ? 0.0f : ent.params_float[0];
            s.p1 = ent.params_float.size() >= 2 ? ent.params_float[1] : 0.0f;
            s.rState1 = ent.runtimeState1;
            snapList.push_back(s);
        }
        SYS_EXIT_CRITICAL();

        // 2. Format base status message header
        int offset = snprintf(statusMsg, sizeof(statusMsg), "STATUS,%u,%u,%u", loopTime, freeH, totalH);

        // 3. Append PWM sensor telemetry data
        for (const auto& s : snapList) {
            if (s.type == DEVICE_PWM_SENSOR) {
                offset += snprintf(statusMsg + offset, sizeof(statusMsg) - offset, ",%.1f", s.p1);
            }
        }

        // 4. Append axis position telemetry data
        for (const auto& s : snapList) {
            if (s.type == DEVICE_AXIS) {
                float stepsPerUnit = (s.p0 > 0) ? s.p0 : 80.0f;
                float currentPos = (float)s.rState1 / stepsPerUnit;
                offset += snprintf(statusMsg + offset, sizeof(statusMsg) - offset, ",%.2f", currentPos);
            }
        }

        // 5. Append system variable telemetry data
        for (const auto& s : snapList) {
            if (s.type == DEVICE_VAR) {
                offset += snprintf(statusMsg + offset, sizeof(statusMsg) - offset, ",%.2f", s.p0);
            }
        }

        // 6. Broadcast telemetry message to all connected clients
        ws.textAll(statusMsg);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief System monitor task that evaluates heap memory usage and system execution latency.
 */
void systemMonitorTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);

    for (;;) {
        uint64_t startTime = SYS_GET_TIME_US();

        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();

        // Safely touch/verify hardware entities status in critical section
        SYS_ENTER_CRITICAL();
        size_t count = hardwareEntities.size();
        for(size_t i = 0; i < count; i++) {
            volatile auto& ent = hardwareEntities[i];
            (void)ent.type;
        }
        SYS_EXIT_CRITICAL();

        uint64_t elapsedTime = SYS_GET_TIME_US() - startTime;

        // Update shared performance metrics
        SYS_ENTER_CRITICAL();
        systemLoopTimeUs = (uint32_t)elapsedTime;
        freeHeapBytes = freeHeap;
        totalHeapBytes = totalHeap;
        SYS_EXIT_CRITICAL();

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
