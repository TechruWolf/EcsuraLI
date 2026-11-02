// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

/**
 * @brief Computes resonant compensation factor using Series LC tank circuit parameters.
 * Formula: f_0 = 1 / (2 * PI * sqrt(L * C))
 *
 * @param targetFreqHz Target operating frequency in Hertz.
 * @param inductanceL Inductance in Henries (H) from ent.params_float[4].
 * @param capacitanceC Capacitance in Farads (F) from ent.params_float[5].
 * @return float Amplitude correction multiplier to compensate for LC impedance.
 */
inline float calculateSeriesLCCorrection(float targetFreqHz, float inductanceL, float capacitanceC) {
    if (inductanceL <= 0.0f || capacitanceC <= 0.0f || targetFreqHz <= 0.0f) {
        return 1.0f; // No compensation if parameters are invalid or zero
    }

    // 1. Calculate resonant frequency f_0
    float resonantFreq = 1.0f / (2.0f * (float)M_PI * sqrtf(inductanceL * capacitanceC));

    // 2. Compute frequency offset ratio to evaluate impedance impact
    float freqRatio = targetFreqHz / resonantFreq;
    float impedanceEffect = fabsf(freqRatio - 1.0f);

    // Scaling correction factor: dampens near resonance, boosts further away
    float correctionFactor = 1.0f / (1.0f + (1.0f / (impedanceEffect + 0.1f)) * 0.05f);

    return constrain(correctionFactor, 0.5f, 1.5f);
}

/**
 * @brief Generates a custom morphed waveform sample using a precalculated Sine Look-Up Table (LUT),
 * integrated with Series LC resonant impedance compensation.
 *
 * @param phase_idx Index in the LUT cycle (0 to LUT_SIZE - 1).
 * @param morphParam (0-100): 0 = Pure Sine, 100 = Pure Triangle.
 * @param clipParam  (0-100): 0 = Sharp peak, 100 = Fully flattened (Square wave).
 * @param freq       Target frequency in Hz (from params_float[2]).
 * @param inductanceL Inductance in Henries (from params_float[4]).
 * @param capacitanceC Capacitance in Farads (from params_float[5]).
 * @return float Final compensated waveform sample.
 */
inline float getCustomWaveformWithLC(int phase_idx, float morphParam, float clipParam, float freq, float inductanceL, float capacitanceC) {
    static float sine_lut[LUT_SIZE];
    static bool lut_initialized = false;

    if (!lut_initialized) {
        for (int i = 0; i < LUT_SIZE; ++i) {
            float angle = ((float)i / (float)LUT_SIZE) * (2.0f * (float)M_PI);
            sine_lut[i] = sinf(angle);
        }
        lut_initialized = true;
    }

    int idx = phase_idx % LUT_SIZE;
    if (idx < 0) idx += LUT_SIZE;

    float sineVal = sine_lut[idx];
    float triVal = 0.0f;
    float normalized_phase = (float)idx / (float)LUT_SIZE;

    if (normalized_phase < 0.25f) {
        triVal = normalized_phase * 4.0f;
    } else if (normalized_phase < 0.75f) {
        triVal = 1.0f - (normalized_phase - 0.25f) * 4.0f;
    } else {
        triVal = -1.0f + (normalized_phase - 0.75f) * 4.0f;
    }

    // 1. Morphing parameter blending (Sine <-> Triangle)
    float morph = constrain(morphParam, 0.0f, 100.0f) / 100.0f;
    float mixedVal = sineVal * (1.0f - morph) + triVal * morph;

    // 2. Clipping / Peak flattening parameter (Progressing towards Square wave)
    float clip = constrain(clipParam, 0.0f, 100.0f) / 100.0f;
    if (clip > 0.0f) {
        float threshold = 1.0f - (clip * 0.95f);
        if (mixedVal > threshold) mixedVal = threshold;
        if (mixedVal < -threshold) mixedVal = -threshold;
        mixedVal /= threshold;
    }

    // 3. Apply Series LC resonant compensation
    float lcCorrection = calculateSeriesLCCorrection(freq, inductanceL, capacitanceC);

    return mixedVal * lcCorrection;
}

/**
 * @brief Handles WebSocket command to control GPIO states, custom waveforms, or PWM triggers.
 */
void handleGpio(AsyncWebSocketClient *client, char* payload) {
    char deviceName[20];
    int val = -1;
    int scanned = sscanf(payload, "%19[^,],%d", deviceName, &val);

    SYS_ENTER_CRITICAL();
    bool found = false;
    int8_t pin = -1;
    float modeParam = 0;

    for (auto& ent : hardwareEntities) {
        if (ent.name == deviceName && ent.type == DEVICE_GPIO) {
            if (!ent.params_int.empty()) pin = ent.params_int[0];
            if (!ent.params_float.empty()) modeParam = ent.params_float[0];

            if (modeParam == 1.0f && scanned == 2 && pin != -1) {
                if (val > 1) {
                    ledcAttach(pin, 25000, 8);
                    ledcWrite(pin, constrain(val, 0, 255));
                } else {
                    digitalWrite(pin, val ? HIGH : LOW);
                }
                ent.runtimeState1 = val;
            }
            found = true;
            break;
        }
    }
    SYS_EXIT_CRITICAL();

    if (!found || pin == -1) {
        client->text("ERR_GPIO");
        return;
    }

    int currentState = digitalRead(pin);
    char resp[40];
    snprintf(resp, sizeof(resp), "G_READ,%s,%d", deviceName, currentState);
    client->text(resp);
}

/**
 * @brief Initializes hardware configuration for general-purpose GPIO devices.
 * Supports digital input/output, pre-attached PWM, and reserves indices for L, C, and Waveform parameters.
 */

// Parameter mapping scheme:
// params_float[0]: Mode (1.0f = Output, 0.0f = Input)
// params_float[1]: Initial State / Base Output level
// params_float[2]: Frequency (Hz)
// params_float[3]: Morphing Param (0-100: Sine -> Triangle)
// params_float[4]: Clipping/Flattening Param (0-100: Sharp -> Square)
// params_float[5]: Inductance L (H) - Series LC compensation
// params_float[6]: Capacitance C (F) - Series LC compensation

inline void initGpioSubsystem() {
    for (auto& ent : hardwareEntities)
        if (ent.type == DEVICE_GPIO && ent.params_int.size() >= 1) {
            int isOutput = (ent.params_float.size() >= 1 && ent.params_float[0] == 1.0f) ? 1 : 0;
            if (isOutput) {
                pinMode(ent.params_int[0], OUTPUT);
                int initState = (ent.params_float.size() >= 2) ? (int)ent.params_float[1] : 0;

                if (initState > 1) {
                    ledcAttach(ent.params_int[0], 25000, 8);
                    ledcWrite(ent.params_int[0], constrain(initState, 0, 255));
                } else {
                    digitalWrite(ent.params_int[0], initState ? HIGH : LOW);
                }
                ent.runtimeState1 = initState;
            } else {
                pinMode(ent.params_int[0], INPUT_PULLUP);
        }
    }
}
