// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

enum DeviceType {
    DEVICE_AXIS_LINEAR_LIMITED = 0,
    DEVICE_PWM = 1,
    DEVICE_GPIO =2,
    DEVICE_AXIS_LINEAR_UNLIMITED = 4,
    DEVICE_AXIS_ROTARY_LIMITED = 5,
    DEVICE_AXIS_ROTARY_UNLIMITED = 6,
    DEVICE_PIPELINE = 7
};

inline bool isAxisDevice(DeviceType type) {
    return (type == DEVICE_AXIS_LINEAR_LIMITED ||
            type == DEVICE_AXIS_LINEAR_UNLIMITED ||
            type == DEVICE_AXIS_ROTARY_LIMITED ||
            type == DEVICE_AXIS_ROTARY_UNLIMITED);
}

struct HardwareEntity {
    String name;
    DeviceType type;
    std::vector<int8_t> pins;
    std::vector<float> params;

    // Runtime States
    volatile int32_t runtimeState1;
    float runtimeState2;
    float runtimeState3;

    HardwareEntity(String n, DeviceType t, std::vector<int8_t> p, std::vector<float> pr, int32_t r1, float r2, float r3)
        : name(n), type(t), pins(p), params(pr), runtimeState1(r1), runtimeState2(r2), runtimeState3(r3) {}
};

struct MotionBufferECS {
    uint32_t* time_units;
    int32_t** steps_matrix;
    size_t numAxes;

    volatile size_t head = 0;
    volatile size_t tail = 0;

    void init(size_t axesCount) {
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

    bool push(uint32_t t, const std::vector<int32_t>& stepValues) {
        if (isFull()) return false;
        time_units[tail] = t;
        for (size_t i = 0; i < numAxes && i < stepValues.size(); i++) {
            steps_matrix[tail][i] = stepValues[i];
        }
        tail = (tail + 1) % MOTION_BUFFER_SIZE;
        return true;
    }

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
};

struct AxisContext {
    int entIdx;
    int8_t stepPin, dirPin, minPin, maxPin;
    float stepsPerUnit, maxLimit, minStepDelay;
    int32_t homingBackoff;
    int32_t homingPostOffset;
    DeviceType type;
};

Preferences preferences;
MotionBufferECS motionPipeline;
portMUX_TYPE hardwareMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE ecsMux = portMUX_INITIALIZER_UNLOCKED;
size_t globalNumAxes = 0;
