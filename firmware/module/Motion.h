// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

void executeMotion() {
    std::vector<int> axisIndices;
    portENTER_CRITICAL(&hardwareMux);
    for (size_t i = 0; i < hardwareEntities.size(); i++) {
        if (!hardwareEntities[i].pins.empty() && hardwareEntities[i].pins[0] != -1) {
            axisIndices.push_back(i);
        }
    }
    portEXIT_CRITICAL(&hardwareMux);

    if (axisIndices.empty()) return;

    uint32_t t;
    size_t numAxes = axisIndices.size();
    std::vector<int32_t> s(numAxes, 0);

    portENTER_CRITICAL(&ecsMux);
    if (motionPipeline.isEmpty()) { portEXIT_CRITICAL(&ecsMux); return; }
    motionPipeline.pop(t, s);
    portEXIT_CRITICAL(&ecsMux);

    if (t == 0) return;

    std::vector<AxisContext> axes(numAxes);
    uint32_t global_min_delay = 5;

    portENTER_CRITICAL(&hardwareMux);
    for (size_t i = 0; i < numAxes; i++) {
        int idx = axisIndices[i];
        const auto& ent = hardwareEntities[idx];

        auto getPin = [&](size_t pIdx) -> int8_t {
            return (ent.pins.size() > pIdx) ? ent.pins[pIdx] : -1;
        };
        auto getParam = [&](size_t prIdx, float defVal) -> float {
            return (ent.params.size() > prIdx) ? ent.params[prIdx] : defVal;
        };

        axes[i] = {
            idx,
            getPin(0), // stepPin
            getPin(1), // dirPin
            getPin(3), // minPin
            getPin(4), // maxPin
            getParam(0, 80.0f),       // stepsPerUnit
            getParam(1, -1.0f),       // maxLimit
            getParam(2, 2.0f),        // minStepDelay
            (int32_t)getParam(3, 0.0f), // homingBackoffSteps
            (int32_t)getParam(4, 0.0f), // homingPostOffset
        };

        if (axes[i].minStepDelay > (float)global_min_delay) {
            global_min_delay = (uint32_t)axes[i].minStepDelay;
        }
    }
    portEXIT_CRITICAL(&hardwareMux);

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
                portENTER_CRITICAL(&hardwareMux);
                int32_t current_pos = hardwareEntities[axes[i].entIdx].runtimeState1;
                portEXIT_CRITICAL(&hardwareMux);

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

            if (use_scurve) {
                float factor = 0.0f;
                if (i < accel_steps) {
                    factor = 1.0f - sin(((float)i / (float)accel_steps) * M_PI_2);
                } else if (i > chunk_max_steps - accel_steps) {
                    factor = sin(((float)(i - (chunk_max_steps - accel_steps)) / (float)accel_steps) * M_PI_2);
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
                    bool physical_tripped = (direction < 0) ? (digitalRead(axes[a].minPin) == LOW)
                    : (digitalRead(axes[a].maxPin) == LOW);

                    if (physical_tripped) {
                        remaining_d[a] = 0;

                        if (is_homing[a]) {
                            if (axes[a].homingPostOffset > 0 && axes[a].dirPin != -1) {
                                digitalWrite(axes[a].dirPin, HIGH);
                                for (int32_t p = 0; p < axes[a].homingPostOffset; p++) {
                                    digitalWrite(axes[a].stepPin, HIGH);
                                    SYS_DELAY_US((uint32_t)axes[a].minStepDelay);
                                    digitalWrite(axes[a].stepPin, LOW);
                                    SYS_DELAY_US((uint32_t)axes[a].minStepDelay);
                                }
                            }

                            portENTER_CRITICAL(&hardwareMux);
                            hardwareEntities[axes[a].entIdx].runtimeState1 = 0;
                            portEXIT_CRITICAL(&hardwareMux);
                        }
                        continue;
                    }
                }

                // Bresenham Step Logic
                err[a] -= chunk_max_steps;
                if (err[a] < 0) {
                    err[a] += chunk_max_steps;
                    remaining_d[a]--;

                    digitalWrite(axes[a].stepPin, HIGH);

                    int32_t stepDir = (s[a] >= 0 && !is_homing[a]) ? 1 : -1;
                    portENTER_CRITICAL(&hardwareMux);
                    volatile int32_t &pos = hardwareEntities[axes[a].entIdx].runtimeState1;
                    pos += stepDir;

                    bool has_virtual = (axes[a].maxLimit > 0.0f && axes[a].maxLimit != -1.0f);
                    if (has_virtual && axes[a].minPin == -1 && axes[a].maxPin == -1) {
                        int32_t wrap = (int32_t)(axes[a].maxLimit * axes[a].stepsPerUnit);
                        if (wrap > 0) {
                            if (pos >= wrap) pos -= wrap;
                            else if (pos < 0) pos += wrap;
                        }
                    }
                    portEXIT_CRITICAL(&hardwareMux);

                    SYS_DELAY_US((uint32_t)axes[a].minStepDelay);
                    digitalWrite(axes[a].stepPin, LOW);
                }
            }

            if (all_done) break;
            if (current_delay > 0) SYS_DELAY_US(current_delay);
        }
    }
}
