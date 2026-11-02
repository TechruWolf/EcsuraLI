// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

/**
 * @brief Dynamic Ring Buffer for Multi-Axis Motion Control (ECS Architecture).
 * Manages timed step commands and axis matrices in real-time.
 */

struct MotionBufferECS {
    uint32_t* time_units = nullptr;
    int32_t** steps_matrix = nullptr;
    size_t numAxes = 0;

    volatile size_t head = 0;
    volatile size_t tail = 0;

    /**
     * @brief Initializes or re-initializes the buffer capacity based on the number of axes.
     * @param axesCount Total number of valid active axes in the system.
     */
    void init(size_t axesCount) {
        // Free previously allocated memory if it exists
        if (steps_matrix != nullptr) {
            for (size_t i = 0; i < MOTION_BUFFER_SIZE; i++) {
                delete[] steps_matrix[i];
            }
            delete[] steps_matrix;
            steps_matrix = nullptr;
        }
        if (time_units != nullptr) {
            delete[] time_units;
            time_units = nullptr;
        }

        // Allocate new resources for the specified axes count
        numAxes = axesCount;
        time_units = new uint32_t[MOTION_BUFFER_SIZE];
        steps_matrix = new int32_t*[MOTION_BUFFER_SIZE];
        for (size_t i = 0; i < MOTION_BUFFER_SIZE; i++) {
            steps_matrix[i] = new int32_t[numAxes]();
        }
        head = 0;
        tail = 0;
    }

    void clear() { head = 0; tail = 0; }
    bool isEmpty() const { return head == tail; }
    bool isFull() const { return ((tail + 1) % MOTION_BUFFER_SIZE) == head; }

    /**
     * @brief Pushes a new motion block (timing + step values) into the buffer tail.
     * @param t Execution time duration in milliseconds.
     * @param stepValues Vector of target step increments per axis.
     * @return true if successfully pushed, false if buffer is full.
     */
    bool push(uint32_t t, const std::vector<int32_t>& stepValues) {
        if (isFull()) return false;
        time_units[tail] = t;
        for (size_t i = 0; i < numAxes && i < stepValues.size(); i++) {
            steps_matrix[tail][i] = stepValues[i];
        }
        tail = (tail + 1) % MOTION_BUFFER_SIZE;
        return true;
    }

    /**
     * @brief Pops a motion block from the buffer head for execution.
     * @param t Reference to store the popped execution time duration.
     * @param outSteps Reference to populate with step increments for all axes.
     * @return true if successfully popped, false if buffer is empty.
     */
    bool pop(uint32_t &t, std::vector<int32_t>& outSteps) {
        if (isEmpty()) return false;
        t = time_units[head];
        outSteps.resize(numAxes);
        for (size_t i = 0; i < numAxes; i++) {
            outSteps[i] = steps_matrix[head][i];
        }
        head = (head + 1) % MOTION_BUFFER_SIZE;
        return true;
    }

    size_t count() const {
        return (tail >= head) ? (tail - head) : (MOTION_BUFFER_SIZE - head + tail);
    }

    /**
     * @brief Peeks at an upcoming motion block in the queue without removing it.
     * @param index Queue offset index relative to head.
     * @param t Reference to store the peeked time duration.
     * @param outSteps Reference to populate with peeked step increments.
     * @return true if index is valid, false otherwise.
     */
    bool peek(size_t index, uint32_t &t, std::vector<int32_t>& outSteps) const {
        if (index >= count()) return false;

        size_t actual_idx = (head + index) % MOTION_BUFFER_SIZE;
        t = time_units[actual_idx];
        outSteps.resize(numAxes);
        for (size_t i = 0; i < numAxes; i++) {
            outSteps[i] = steps_matrix[actual_idx][i];
        }
        return true;
    }
};

/**
 * @brief Represents a single motion instruction block with look-ahead speed factors.
 */
struct MotionBlock {
    uint32_t t;
    std::vector<int32_t> steps;
    float entry_speed_factor;
    float exit_speed_factor;
};

MotionBufferECS motionPipeline;
portMUX_TYPE ecsMux = portMUX_INITIALIZER_UNLOCKED;
size_t globalNumAxes = 0;

/**
 * @brief Computes an S-Curve smoothing factor using a precalculated Sine Look-Up Table (LUT).
 * @param index Current step index within the acceleration/deceleration ramp.
 * @param total_steps Total number of steps in the smoothing ramp zone.
 * @return Normalized scaling factor between 0.0f and 1.0f.
 */
inline float getSCurveFactor(int32_t index, int32_t total_steps) {
    if (total_steps <= 0) return 0.0f;

    static float lut[LUT_SIZE];
    static bool initialized = false;
    if (!initialized) {
        for (int i = 0; i < LUT_SIZE; ++i) {
            float angle = ((float)i / (float)(LUT_SIZE - 1)) * (M_PI_2);
            lut[i] = sinf(angle);
        }
        initialized = true;
    }

    float exact_idx = ((float)index / (float)total_steps) * (float)(LUT_SIZE - 1);
    int idx1 = (int)exact_idx;

    if (idx1 >= LUT_SIZE - 1) return lut[LUT_SIZE - 1];
    if (idx1 < 0) return lut[0];

    int idx2 = idx1 + 1;
    float frac = exact_idx - (float)idx1;

    return lut[idx1] * (1.0f - frac) + lut[idx2] * frac;
}

/**
 * @brief Main execution function for processing real-time motion control,
 * velocity blending, S-curve profiling, and endstop monitoring.
 */
void executeMotion() {
    // Phase 1: Collect active axis indices from hardware entities safely
    std::vector<int> axisIndices;
    SYS_ENTER_CRITICAL();
    size_t totalEntities = hardwareEntities.size();
    for (size_t i = 0; i < totalEntities; i++) {
        const auto& ent = hardwareEntities[i];
        if (ent.type == DEVICE_AXIS && !ent.params_int.empty() && ent.params_int[0] != -1) {
            axisIndices.push_back(i);
        }
    }
    SYS_EXIT_CRITICAL();

    if (axisIndices.empty()) return;
    size_t numAxes = axisIndices.size();

    std::vector<MotionBlock> lookAheadQueue;

    // Phase 2: Fetch motion blocks from the pipeline for look-ahead processing
    portENTER_CRITICAL(&ecsMux);
    if (motionPipeline.isEmpty()) {
        portEXIT_CRITICAL(&ecsMux);
        return;
    }

    size_t available_blocks = motionPipeline.count();
    size_t fetch_count = std::min(available_blocks, (size_t)LOOK_AHEAD_COUNT);

    for (size_t i = 0; i < fetch_count; i++) {
        uint32_t bt;
        std::vector<int32_t> bs;
        if (motionPipeline.peek(i, bt, bs)) {
            lookAheadQueue.push_back({bt, bs, 1.0f, 1.0f});
        }
    }
    portEXIT_CRITICAL(&ecsMux);

    if (lookAheadQueue.empty()) return;

    // Phase 3: Calculate junction speed factors between consecutive motion blocks (Corner blending)
    for (size_t i = 0; i < lookAheadQueue.size() - 1; i++) {
        const auto& curr_steps = lookAheadQueue[i].steps;
        const auto& next_steps = lookAheadQueue[i+1].steps;

        float dot_product = 0.0f;
        float mag_curr = 0.0f;
        float mag_next = 0.0f;

        for (size_t a = 0; a < numAxes && a < curr_steps.size() && a < next_steps.size(); a++) {
            dot_product += (float)(curr_steps[a] * next_steps[a]);
            mag_curr += (float)(curr_steps[a] * curr_steps[a]);
            mag_next += (float)(next_steps[a] * next_steps[a]);
        }

        if (mag_curr > 0.0f && mag_next > 0.0f) {
            float cos_theta = dot_product / (sqrtf(mag_curr) * sqrtf(mag_next));
            float junction_factor = (cos_theta + 1.0f) * 0.5f;
            if (junction_factor < 0.15f) junction_factor = 0.15f;

            lookAheadQueue[i].exit_speed_factor = junction_factor;
            lookAheadQueue[i+1].entry_speed_factor = junction_factor;
        }
    }

    // Phase 4: Pop the active motion block to be executed
    uint32_t t;
    std::vector<int32_t> s(numAxes, 0);
    portENTER_CRITICAL(&ecsMux);
    motionPipeline.pop(t, s);
    portEXIT_CRITICAL(&ecsMux);

    if (t == 0) return;
    float current_entry_factor = lookAheadQueue[0].entry_speed_factor;
    float current_exit_factor = lookAheadQueue[0].exit_speed_factor;

    uint32_t global_min_delay = 5;

    // Pre-scan to determine global minimum step delay directly from hardware configurations
    SYS_ENTER_CRITICAL();
    for (size_t i = 0; i < numAxes; i++) {
        const auto& ent = hardwareEntities[axisIndices[i]];
        float minStepDelay = (ent.params_float.size() > 2) ? ent.params_float[2] : 2.0f;
        if (minStepDelay > (float)global_min_delay) {
            global_min_delay = (uint32_t)minStepDelay;
        }
    }
    SYS_EXIT_CRITICAL();

    std::vector<bool> is_homing(numAxes, false);
    std::vector<int32_t> d(numAxes, 0);
    int32_t total_max_steps = 0;

    // Phase 5 & 6 (Merged): Directly read hardware entities and configure direction, endstops, and homing flags
    for (size_t i = 0; i < numAxes; i++) {
        int entIdx = axisIndices[i];

        SYS_ENTER_CRITICAL();
        const auto& ent = hardwareEntities[entIdx];
        int8_t dirPin = (ent.params_int.size() > 1) ? ent.params_int[1] : -1;
        if (dirPin == -1) {
            SYS_EXIT_CRITICAL();
            continue;
        }
        int8_t minPin = (ent.params_int.size() > 3) ? abs(ent.params_int[3]) : 0;
        int8_t maxPin = (ent.params_int.size() > 4) ? abs(ent.params_int[4]) : 0;
        bool has_min = (minPin != 0);
        bool has_max = (maxPin != 0);

        // Read and reset homing request flag directly from runtimeState2
        float& homeRequest = hardwareEntities[entIdx].runtimeState2;
        bool isHomingRequested = (homeRequest > 0.5f);
        if (isHomingRequested) {
            homeRequest = 0.0f;
        }
        SYS_EXIT_CRITICAL();

        if (isHomingRequested && (has_min || has_max)) {
            is_homing[i] = true;
            int32_t homeDir = has_min ? LOW : HIGH;
            digitalWrite(dirPin, homeDir);
            d[i] = 1000000; // Large step pool for homing sweep
        } else {
            int32_t stepDir = (s[i] >= 0) ? 1 : -1;
            digitalWrite(dirPin, stepDir == 1 ? HIGH : LOW);
            int32_t req_steps = abs(s[i]);

            // Soft limits / Virtual boundary check
            SYS_ENTER_CRITICAL();
            float maxLimit = (hardwareEntities[entIdx].params_float.size() > 1) ? hardwareEntities[entIdx].params_float[1] : -1.0f;
            float stepsPerUnit = (hardwareEntities[entIdx].params_float.size() > 0) ? hardwareEntities[entIdx].params_float[0] : 80.0f;
            int32_t current_pos = hardwareEntities[entIdx].runtimeState1;
            SYS_EXIT_CRITICAL();

            bool has_virtual = (maxLimit > 0.0f);
            if (has_virtual) {
                int32_t max_limit_steps = (int32_t)(maxLimit * stepsPerUnit);
                if (stepDir > 0) {
                    int32_t allowed = max_limit_steps - current_pos;
                    if (allowed < 0) allowed = 0;
                    if (req_steps > allowed) req_steps = allowed;
                } else {
                    int32_t allowed = current_pos;
                    if (req_steps > allowed) req_steps = allowed;
                }
            }
            d[i] = req_steps;
        }
        if (d[i] > total_max_steps) total_max_steps = d[i];
    }

    if (total_max_steps == 0) {
        SYS_DELAY_US((t > 400000) ? 400000 : (t * 10));
        return;
    }

    const int32_t CHUNK_SIZE = 64;
    std::vector<int32_t> remaining_d = d;
    uint64_t total_target_us = (uint64_t)t * 1000ULL;
    uint32_t base_delay_per_step = std::max((uint32_t)(total_target_us / (uint64_t)total_max_steps), global_min_delay);

    // Phase 7: Main step generation and interpolation loop (Chunk-based Bresenham algorithm)
    while (true) {
        int32_t chunk_max_steps = 0;
        for (size_t i = 0; i < numAxes; i++) {
            if (remaining_d[i] > chunk_max_steps) chunk_max_steps = remaining_d[i];
        }
        if (chunk_max_steps == 0) break;
        if (chunk_max_steps > CHUNK_SIZE) chunk_max_steps = CHUNK_SIZE;

        std::vector<int32_t> err(numAxes, chunk_max_steps / 2);
        bool use_scurve = (chunk_max_steps > 15);
        uint32_t accel_steps = use_scurve ? (chunk_max_steps / 3) : 0;

        for (int32_t i = 0; i < chunk_max_steps; i++) {
            uint32_t current_delay = base_delay_per_step;

            // Apply S-curve acceleration/deceleration factor per step pulse
            if (use_scurve && accel_steps > 0) {
                float factor = 0.0f;
                if (i < (int32_t)accel_steps) {
                    factor = (1.0f - current_entry_factor) + (current_entry_factor * (1.0f - getSCurveFactor(i, accel_steps)));
                } else if (i > chunk_max_steps - (int32_t)accel_steps) {
                    float scurve_val = getSCurveFactor(i - (chunk_max_steps - (int32_t)accel_steps), accel_steps);
                    factor = (1.0f - current_exit_factor) * scurve_val;
                }
                current_delay += (uint32_t)(base_delay_per_step * 1.5f * factor);
            }

            bool all_done = true;

            for (size_t a = 0; a < numAxes; a++) {
                int entIdx = axisIndices[a];

                SYS_ENTER_CRITICAL();
                int8_t stepPin = hardwareEntities[entIdx].params_int[0];
                SYS_EXIT_CRITICAL();

                if (stepPin == -1 || remaining_d[a] <= 0) continue;

                all_done = false;
                int32_t direction = (s[a] < 0 || is_homing[a]) ? -1 : 1;

                // Monitor physical endstops (limit switches)
                SYS_ENTER_CRITICAL();
                int8_t minPin = (hardwareEntities[entIdx].params_int.size() > 3) ? hardwareEntities[entIdx].params_int[3] : 0;
                int8_t maxPin = (hardwareEntities[entIdx].params_int.size() > 4) ? hardwareEntities[entIdx].params_int[4] : 0;
                bool mInit = (minPin < 0);
                bool xInit = (maxPin < 0);
                minPin = abs(minPin);
                maxPin = abs(maxPin);
                SYS_EXIT_CRITICAL();

                bool has_physical_endstop = (direction < 0 && minPin != 0) || (direction > 0 && maxPin != 0);
                if (has_physical_endstop) {
                    bool physical_tripped = false;
                    if (direction < 0 && minPin != 0) {
                        physical_tripped = (digitalRead(minPin) != mInit);
                    } else if (direction > 0 && maxPin != 0) {
                        physical_tripped = (digitalRead(maxPin) != xInit);
                    }

                    if (physical_tripped) {
                        remaining_d[a] = 0;

                        // Execute homing sequence backoff and post-offset actions
                        if (is_homing[a]) {
                            SYS_ENTER_CRITICAL();
                            int8_t dirPin = hardwareEntities[entIdx].params_int[1];
                            float minStepDelay = (hardwareEntities[entIdx].params_float.size() > 2) ? hardwareEntities[entIdx].params_float[2] : 2.0f;
                            int32_t homingBack = (hardwareEntities[entIdx].params_float.size() > 3) ? (int32_t)hardwareEntities[entIdx].params_float[3] : 0;
                            int32_t homingPost = (hardwareEntities[entIdx].params_float.size() > 4) ? (int32_t)hardwareEntities[entIdx].params_float[4] : 0;
                            SYS_EXIT_CRITICAL();

                            auto runHomingSteps = [](int8_t sPin, int8_t dPin, int32_t count, uint32_t delayUs) {
                                if (count <= 0 || dPin == -1) return;
                                digitalWrite(dPin, HIGH);
                                for (int32_t b = 0; b < count; b++) {
                                    digitalWrite(sPin, HIGH);
                                    SYS_DELAY_US(delayUs * 5);
                                    digitalWrite(sPin, LOW);
                                    SYS_DELAY_US(delayUs * 5);
                                }
                            };

                            runHomingSteps(stepPin, dirPin, homingBack, (uint32_t)minStepDelay);
                            runHomingSteps(stepPin, dirPin, homingPost, (uint32_t)minStepDelay);

                            SYS_ENTER_CRITICAL();
                            hardwareEntities[entIdx].runtimeState1 = 0;
                            SYS_EXIT_CRITICAL();
                        }
                        continue;
                    }
                }

                // Step pulse execution and runtime position tracking
                err[a] -= chunk_max_steps;
                if (err[a] < 0) {
                    err[a] += chunk_max_steps;
                    remaining_d[a]--;

                    digitalWrite(stepPin, HIGH);

                    int32_t stepDir = (s[a] >= 0 && !is_homing[a]) ? 1 : -1;
                    SYS_ENTER_CRITICAL();
                    volatile int32_t &pos = hardwareEntities[entIdx].runtimeState1;
                    pos += stepDir;

                    float maxLimit = (hardwareEntities[entIdx].params_float.size() > 1) ? hardwareEntities[entIdx].params_float[1] : -1.0f;
                    float stepsPerUnit = (hardwareEntities[entIdx].params_float.size() > 0) ? hardwareEntities[entIdx].params_float[0] : 80.0f;
                    bool has_virtual = (maxLimit > 0.0f && maxLimit != -1.0f);
                    if (has_virtual && minPin == 0 && maxPin == 0) {
                        int32_t max_limit_steps = (int32_t)(maxLimit * stepsPerUnit);
                        if (pos >= max_limit_steps) {
                            pos = max_limit_steps;
                            remaining_d[a] = 0;
                        } else if (pos < 0) {
                            pos = 0;
                            remaining_d[a] = 0;
                        }
                    }
                    SYS_EXIT_CRITICAL();

                    SYS_ENTER_CRITICAL();
                    float minStepDelay = (hardwareEntities[entIdx].params_float.size() > 2) ? hardwareEntities[entIdx].params_float[2] : 2.0f;
                    SYS_EXIT_CRITICAL();

                    SYS_DELAY_US((uint32_t)minStepDelay);
                    digitalWrite(stepPin, LOW);
                }
            }

            if (all_done) break;
            if (current_delay > 0) SYS_DELAY_US(current_delay);
        }
    }
}

/**
 * @brief FreeRTOS background task continuously dispatching motion operations.
 * @param pvParameters Task runtime parameters (unused).
 */
void motionControlTask(void *pvParameters) {
    for (;;) {
        executeMotion();
        if (motionPipeline.isEmpty()) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

/**
 * @brief WebSocket command handler for processing individual or global axis homing requests.
 * @param client Pointer to the active WebSocket client.
 * @param payload Raw payload string containing target axis identifier (e.g., "X" or "ALL").
 */
void handleHome(AsyncWebSocketClient *client, char* payload) {
    char targetAxis[20];
    if (sscanf(payload, "%19s", targetAxis) != 1) return;

    bool isAll = (strcmp(targetAxis, "ALL") == 0);

    SYS_ENTER_CRITICAL();

    std::vector<size_t> validAxisIndices;
    size_t totalEntities = hardwareEntities.size();
    for (size_t i = 0; i < totalEntities; i++) {
        const auto& ent = hardwareEntities[i];
        if (ent.type == DEVICE_AXIS && !ent.params_int.empty() && ent.params_int[0] != -1) {
            validAxisIndices.push_back(i);
        }
    }

    size_t totalAxesCount = validAxisIndices.size();
    std::vector<int32_t> zeroSteps(totalAxesCount, 0);
    bool triggered = false;

    for (size_t pipelineIdx = 0; pipelineIdx < validAxisIndices.size(); pipelineIdx++) {
        size_t entityIdx = validAxisIndices[pipelineIdx];
        auto& ent = hardwareEntities[entityIdx];

        bool shouldHome = isAll || ent.name.equalsIgnoreCase(targetAxis);
        if (shouldHome) {
            int8_t minPin = (ent.params_int.size() > 3) ? ent.params_int[3] : 0;
            int8_t maxPin = (ent.params_int.size() > 4) ? ent.params_int[4] : 0;

            if (minPin != 0 || maxPin != 0) {
                ent.runtimeState2 = 1.0f; // Request homing state flag
                triggered = true;
            }
        }
    }
    SYS_EXIT_CRITICAL();

    if (!triggered) {
        client->text("ERR_HOME");
        return;
    }

    portENTER_CRITICAL(&ecsMux);
    bool success = motionPipeline.push(10000, zeroSteps); // Wake up execution loop with a dummy block
    portEXIT_CRITICAL(&ecsMux);

    if (success) {
        client->text("HACK");
    } else {
        client->text("ERR_BUFFER_FULL");
    }
}

/**
 * @brief WebSocket command handler for processing incoming G-code motion push streams.
 * @param client Pointer to the active WebSocket client.
 * @param buffer Raw string buffer containing execution time and axis step increments.
 */
void handleMotionPush(AsyncWebSocketClient *client, char* buffer) {
    char* token = strtok(buffer, ",");
    if (token == NULL) {
        client->text("{\"status\":\"ERR_GCODE\"}");
        return;
    }

    uint32_t t = (uint32_t)strtoul(token, NULL, 10);
    std::vector<int32_t> stepsList;

    while ((token = strtok(NULL, ",")) != NULL) {
        stepsList.push_back((int32_t)atol(token));
    }

    if (t >= 0.0f) {
        portENTER_CRITICAL(&ecsMux);
        bool success = motionPipeline.push(t, stepsList);
        size_t current_count = motionPipeline.count();
        size_t h = motionPipeline.head;
        size_t tl = motionPipeline.tail;
        portEXIT_CRITICAL(&ecsMux);

        bool is_warning = !success || (current_count > MOTION_BUFFER_SIZE * 0.85);
        const char* statusStr = is_warning ? "BUF_FULL" : "ACK";

        String json = "{\"status\":\"" + String(statusStr) +
        "\", \"head\":" + String(h) +
        ", \"tail\":" + String(tl) +
        ", \"count\":" + String(current_count) +
        ", \"size\":" + String(MOTION_BUFFER_SIZE) + "}";
        client->text(json);

    } else {
        client->text("{\"status\":\"ERR_GCODE\"}");
    }
}

/**
 * @brief Immediately halts motion operations and disables active outputs for emergency safety.
 */
void handleEmergencyStopMotion() {
    portENTER_CRITICAL(&ecsMux);
    motionPipeline.clear();
    portEXIT_CRITICAL(&ecsMux);

    SYS_ENTER_CRITICAL();
    for (auto& ent : hardwareEntities) {
        if (ent.type == DEVICE_AXIS && ent.params_int.size() >= 3) {
            digitalWrite(ent.params_int[2], HIGH); // Assert stepper driver disable pin
        }
    }
    SYS_EXIT_CRITICAL();
}

/**
 * @brief Scans all hardware entities, initializes stepper pins and endstops for valid axes,
 * and initializes the global motion pipeline ring buffer.
 */
inline void initMotionSubsystem() {
    size_t validAxesCount = 0;

    portENTER_CRITICAL(&ecsMux);
    motionPipeline.clear();
    portEXIT_CRITICAL(&ecsMux);

    for (auto& ent : hardwareEntities) {
        if (ent.type == DEVICE_AXIS) {
            if (!ent.params_int.empty() && ent.params_int[0] != -1) {
                validAxesCount++;

                pinMode(ent.params_int[0], OUTPUT); // Step Pin
                pinMode(ent.params_int[1], OUTPUT); // Direction Pin
                pinMode(ent.params_int[2], OUTPUT); // Enable Pin
                digitalWrite(ent.params_int[2], LOW);

                if (ent.params_int.size() >= 4 && ent.params_int[3] != 0) pinMode(ent.params_int[3], INPUT_PULLUP); // Min Endstop
                if (ent.params_int.size() >= 5 && ent.params_int[4] != 0) pinMode(ent.params_int[4], INPUT_PULLUP); // Max Endstop
            }
        }
    }

    motionPipeline.init(validAxesCount);
}
