// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

inline AsyncWebServer server(80);
inline AsyncWebSocket ws("/ws");

/**
 * @brief Forward declarations of modular system handlers.
 */

// Required by Init.h
void initHardwareECS();

// Required by Motion.h
void handleHome(AsyncWebSocketClient *client, char* payload);
void handleMotionPush(AsyncWebSocketClient *client, char* buffer);
void handleEmergencyStop(AsyncWebSocketClient *client);

// Required by SensorAndPWM.h
void handleSetPWM(AsyncWebSocketClient *client, char* payload);

// Required by GPIO.h
void handleGpio(AsyncWebSocketClient *client, char* payload);

/**
 * @brief Parses incoming JSON configuration payload and updates hardware entities safely.
 * @param client Pointer to the active WebSocket client.
 * @param jsonPayload Raw JSON character string containing configuration items.
 */
void handleSaveConfig(AsyncWebSocketClient *client, const char* jsonPayload) {
    std::vector<HardwareEntity> newEntities;
    int depth = 0;
    int startIndex = -1;

    String payloadStr(jsonPayload);
    for (size_t i = 0; i < payloadStr.length(); i++) {
        char c = payloadStr.charAt(i);
        if (c == '{') {
            if (depth == 0) startIndex = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0 && startIndex != -1) {
                String objStr = payloadStr.substring(startIndex, i + 1);

                String entName = "UNKNOWN";
                DeviceType entType = DEVICE_AXIS;
                std::vector<int8_t> entPins;
                std::vector<float> entParams;

                // Parse device name string
                int nameIdx = objStr.indexOf("\"name\":\"");
                if (nameIdx != -1) {
                    int startN = nameIdx + 8;
                    int endN = objStr.indexOf("\"", startN);
                    if (endN != -1) entName = objStr.substring(startN, endN);
                }

                // Parse device type integer/enum value
                int typeIdx = objStr.indexOf("\"type\":");
                if (typeIdx != -1) {
                    int startT = typeIdx + 7;
                    int endT = startT;
                    while (endT < objStr.length() && (isDigit(objStr.charAt(endT)) || objStr.charAt(endT) == '-')) endT++;
                    entType = (DeviceType)objStr.substring(startT, endT).toInt();
                }

                // Parse integer parameters/pins array (params_int)
                int params_intIdx = objStr.indexOf("\"params_int\":[");
                if (params_intIdx != -1) {
                    int startP = params_intIdx + 14;
                    int endP = objStr.indexOf("]", startP);
                    if (endP != -1) {
                        String params_intStr = objStr.substring(startP, endP);
                        int pStart = 0;
                        while (pStart < params_intStr.length()) {
                            int pComma = params_intStr.indexOf(",", pStart);
                            if (pComma == -1) pComma = params_intStr.length();
                            String pVal = params_intStr.substring(pStart, pComma);
                            pVal.trim();
                            if (pVal.length() > 0) entPins.push_back((int8_t)pVal.toInt());
                            pStart = pComma + 1;
                        }
                    }
                }

                // Parse floating-point parameters array (params_float)
                int params_floatIdx = objStr.indexOf("\"params_float\":[");
                if (params_floatIdx != -1) {
                    int startPr = params_floatIdx + 16;
                    int endPr = objStr.indexOf("]", startPr);
                    if (endPr != -1) {
                        String params_floatStr = objStr.substring(startPr, endPr);
                        int prStart = 0;
                        while (prStart < params_floatStr.length()) {
                            int prComma = params_floatStr.indexOf(",", prStart);
                            if (prComma == -1) prComma = params_floatStr.length();
                            String prVal = params_floatStr.substring(prStart, prComma);
                            prVal.trim();
                            if (prVal.length() > 0) entParams.push_back(prVal.toFloat());
                            prStart = prComma + 1;
                        }
                    }
                }

                newEntities.push_back(HardwareEntity(entName, entType, entPins, entParams, 0, 0.0f, 0.0f));
                startIndex = -1;
            }
        }
    }

    if (newEntities.empty()) {
        client->text("ERR_JSON");
        return;
    }

    SYS_ENTER_CRITICAL();
    hardwareEntities = newEntities;
    SYS_EXIT_CRITICAL();

    // Reinitialize hardware subsystems with the updated configuration
    initHardwareECS();

    client->text("SCA");
}

/**
 * @brief Serializes current hardware entities into a structured JSON string response.
 * @param client Pointer to the active WebSocket client.
 */
void handleGetConfig(AsyncWebSocketClient *client) {
    String responseJson = "CFG_RESP,[";
    SYS_ENTER_CRITICAL();
    for (size_t i = 0; i < hardwareEntities.size(); i++) {
        responseJson += "{\"name\":\"" + hardwareEntities[i].name + "\",";
        responseJson += "\"type\":" + String(hardwareEntities[i].type) + ",";
        responseJson += "\"params_int\":[";
        for (size_t p = 0; p < hardwareEntities[i].params_int.size(); p++) {
            responseJson += String(hardwareEntities[i].params_int[p]);
            if (p + 1 < hardwareEntities[i].params_int.size()) responseJson += ",";
        }
        responseJson += "],\"params_float\":[";
        for (size_t pr = 0; pr < hardwareEntities[i].params_float.size(); pr++) {
            responseJson += String(hardwareEntities[i].params_float[pr]);
            if (pr + 1 < hardwareEntities[i].params_float.size()) responseJson += ",";
        }
        responseJson += "]}";
        if (i + 1 < hardwareEntities.size()) responseJson += ",";
    }
    SYS_EXIT_CRITICAL();
    responseJson += "]";
    client->text(responseJson);
}

/**
 * @brief Immediately halts motion operations and disables all active outputs for emergency safety.
 * @param client Pointer to the active WebSocket client.
 */
void handleEmergencyStop(AsyncWebSocketClient *client) {
    handleEmergencyStopMotion(); //Required by Motion.h
    handleEmergencyPWMAndSensorStop(); //Required by PWMAndSensor.h
    client->text("EMG_ACK");
}

/**
 * @brief Pauses execution for a specified duration in milliseconds (G-code Dwell equivalent).
 * @param client Pointer to the active WebSocket client.
 * @param payload Character string containing dwell time duration.
 */
void handleDwell(AsyncWebSocketClient *client, char* payload) {
    unsigned long dwellTime = atoi(payload);
    unsigned long startWait = millis();
    while ((millis() - startWait) < dwellTime) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    client->text("ACK");
}

/**
 * @brief Global AsyncWebSocket event callback hook.
 */
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            break;

        case WS_EVT_DISCONNECT:
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;

            if (len >= 2048) {
                client->text("ERR_LEN");
                return;
            }

            char buffer[2048];
            memcpy(buffer, data, len);
            buffer[len] = '\0';

            if      (strncmp(buffer, "SAVE_CONFIG_JSON,", 17) == 0) handleSaveConfig(client, buffer + 17);
            else if (strcmp(buffer, "GFG_JSON") == 0)             handleGetConfig(client);
            else if (strncmp(buffer, "SET_PWM,", 8) == 0)         handleSetPWM(client, buffer + 8);
            else if (strncmp(buffer, "HOME,", 5) == 0)             handleHome(client, buffer + 5);
            else if (strncmp(buffer, "GPIO,", 5) == 0)             handleGpio(client, buffer + 5);
            else if (strcmp(buffer, "EMG_STOP") == 0)             handleEmergencyStop(client);
            else if (strcmp(buffer, "R_MCU") == 0)                { delay(50); ESP.restart(); }
            else if (strncmp(buffer, "DWELL,", 6) == 0)           handleDwell(client, buffer + 6);
            else                                                  handleMotionPush(client, buffer);
            break;
        }

        default:
            break;
    }
}
