// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

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

void executeMotion() {
    std::vector<int> axisIndices;
    SYS_ENTER_CRITICAL();
    size_t totalEntities = hardwareEntities.size();
    for (size_t i = 0; i < totalEntities; i++) {
        const auto& ent = hardwareEntities[i];
        if (ent.type == DEVICE_AXIS && !ent.pins.empty() && ent.pins[0] != -1) {
            axisIndices.push_back(i);
        }
    }
    SYS_EXIT_CRITICAL();

    if (axisIndices.empty()) return;
    size_t numAxes = axisIndices.size();

    std::vector<MotionBlock> lookAheadQueue;

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

    uint32_t t;
    std::vector<int32_t> s(numAxes, 0);
    portENTER_CRITICAL(&ecsMux);
    motionPipeline.pop(t, s);
    portEXIT_CRITICAL(&ecsMux);

    if (t == 0) return;
    float current_entry_factor = lookAheadQueue[0].entry_speed_factor;
    float current_exit_factor = lookAheadQueue[0].exit_speed_factor;

    std::vector<AxisContext> axes(numAxes);
    uint32_t global_min_delay = 5;

    for (size_t i = 0; i < numAxes; i++) {
        int idx = axisIndices[i];

        SYS_ENTER_CRITICAL();
        const auto& ent = hardwareEntities[idx];
        int8_t stepPin = (ent.pins.size() > 0) ? ent.pins[0] : -1;
        int8_t dirPin  = (ent.pins.size() > 1) ? ent.pins[1] : -1;
        int8_t minPin  = (ent.pins.size() > 3) ? ent.pins[3] : -1;
        int8_t maxPin  = (ent.pins.size() > 4) ? ent.pins[4] : -1;

        float stepsPerUnit  = (ent.params.size() > 0) ? ent.params[0] : 80.0f;
        float maxLimit      = (ent.params.size() > 1) ? ent.params[1] : -1.0f;
        float minStepDelay  = (ent.params.size() > 2) ? ent.params[2] : 2.0f;
        int32_t homingBack  = (ent.params.size() > 3) ? (int32_t)ent.params[3] : 0;
        int32_t homingPost  = (ent.params.size() > 4) ? (int32_t)ent.params[4] : 0;

        bool mInit = (minPin != -1 && ent.params.size() > 5) ? (bool)ent.params[5] : true;
        bool xInit = (maxPin != -1 && ent.params.size() > 6) ? (bool)ent.params[6] : true;
        SYS_EXIT_CRITICAL();

        axes[i] = {idx, stepPin, dirPin, minPin, maxPin, stepsPerUnit, maxLimit, minStepDelay, homingBack, homingPost, mInit, xInit};

        if (axes[i].minStepDelay > (float)global_min_delay) {
            global_min_delay = (uint32_t)axes[i].minStepDelay;
        }
    }

    std::vector<bool> is_homing(numAxes, false);
    std::vector<int32_t> d(numAxes, 0);
    int32_t total_max_steps = 0;

    for (size_t i = 0; i < numAxes; i++) {
        if (axes[i].dirPin == -1) continue;

        bool has_physical_min = (axes[i].minPin != -1);
        if (s[i] == -1 && has_physical_min) {
            is_homing[i] = true;
            digitalWrite(axes[i].dirPin, LOW);
            d[i] = 1000000;
        } else {
            int32_t stepDir = (s[i] >= 0) ? 1 : -1;
            digitalWrite(axes[i].dirPin, stepDir == 1 ? HIGH : LOW);
            int32_t req_steps = abs(s[i]);

            bool has_virtual = (axes[i].maxLimit > 0.0f && axes[i].maxLimit != -1.0f);
            if (has_virtual) {
                SYS_ENTER_CRITICAL();
                int32_t current_pos = hardwareEntities[axes[i].entIdx].runtimeState1;
                SYS_EXIT_CRITICAL();

                int32_t max_limit_steps = (int32_t)(axes[i].maxLimit * axes[i].stepsPerUnit);
                int32_t allowed = (stepDir > 0) ? (max_limit_steps - current_pos) : current_pos;
                if (allowed < 0) allowed = 0;
                if (req_steps > allowed) req_steps = allowed;
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
                if (axes[a].stepPin == -1 || remaining_d[a] <= 0) continue;

                all_done = false;
                int32_t direction = (s[a] < 0 || is_homing[a]) ? -1 : 1;

                bool has_physical_endstop = (direction < 0 && axes[a].minPin != -1) || (direction > 0 && axes[a].maxPin != -1);
                if (has_physical_endstop) {
                    bool physical_tripped = false;
                    if (direction < 0 && axes[a].minPin != -1) {
                        physical_tripped = (digitalRead(axes[a].minPin) != axes[a].minInitialState);
                    } else if (direction > 0 && axes[a].maxPin != -1) {
                        physical_tripped = (digitalRead(axes[a].maxPin) != axes[a].maxInitialState);
                    }

                    if (physical_tripped) {
                        remaining_d[a] = 0;

                        if (is_homing[a]) {
                            auto runHomingSteps = [](int8_t stepPin, int8_t dirPin, int32_t count, uint32_t delayUs) {
                                if (count <= 0 || dirPin == -1) return;
                                digitalWrite(dirPin, HIGH);
                                for (int32_t b = 0; b < count; b++) {
                                    digitalWrite(stepPin, HIGH);
                                    SYS_DELAY_US(delayUs);
                                    digitalWrite(stepPin, LOW);
                                    SYS_DELAY_US(delayUs);
                                }
                            };

                            runHomingSteps(axes[a].stepPin, axes[a].dirPin, axes[a].homingBackoff, (uint32_t)axes[a].minStepDelay);
                            runHomingSteps(axes[a].stepPin, axes[a].dirPin, axes[a].homingPostOffset, (uint32_t)axes[a].minStepDelay);

                            SYS_ENTER_CRITICAL();
                            hardwareEntities[axes[a].entIdx].runtimeState1 = 0;
                            SYS_EXIT_CRITICAL();
                        }
                        continue;
                    }
                }

                err[a] -= chunk_max_steps;
                if (err[a] < 0) {
                    err[a] += chunk_max_steps;
                    remaining_d[a]--;

                    digitalWrite(axes[a].stepPin, HIGH);

                    int32_t stepDir = (s[a] >= 0 && !is_homing[a]) ? 1 : -1;
                    SYS_ENTER_CRITICAL();
                    volatile int32_t &pos = hardwareEntities[axes[a].entIdx].runtimeState1;
                    pos += stepDir;

                    bool has_virtual = (axes[a].maxLimit > 0.0f && axes[a].maxLimit != -1.0f);
                    if (has_virtual && axes[a].minPin == -1 && axes[a].maxPin == -1) {
                        int32_t max_limit_steps = (int32_t)(axes[a].maxLimit * axes[a].stepsPerUnit);
                        if (pos >= max_limit_steps) {
                            pos = max_limit_steps;
                            remaining_d[a] = 0;
                        } else if (pos < 0) {
                            pos = 0;
                            remaining_d[a] = 0;
                        }
                    }
                    SYS_EXIT_CRITICAL();

                    SYS_DELAY_US((uint32_t)axes[a].minStepDelay);
                    digitalWrite(axes[a].stepPin, LOW);
                }
            }

            if (all_done) break;
            if (current_delay > 0) SYS_DELAY_US(current_delay);
        }
    }
}
