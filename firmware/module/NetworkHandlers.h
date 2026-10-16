// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

inline AsyncWebServer server(80);
inline AsyncWebSocket ws("/ws");

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
                DeviceType entType = DEVICE_AXIS_LINEAR_LIMITED;
                std::vector<int8_t> entPins;
                std::vector<float> entParams;

                // Parse name
                int nameIdx = objStr.indexOf("\"name\":\"");
                if (nameIdx != -1) {
                    int startN = nameIdx + 8;
                    int endN = objStr.indexOf("\"", startN);
                    if (endN != -1) entName = objStr.substring(startN, endN);
                }

                // Parse type
                int typeIdx = objStr.indexOf("\"type\":");
                if (typeIdx != -1) {
                    int startT = typeIdx + 7;
                    int endT = startT;
                    while (endT < objStr.length() && (isDigit(objStr.charAt(endT)) || objStr.charAt(endT) == '-')) endT++;
                    entType = (DeviceType)objStr.substring(startT, endT).toInt();
                }

                // Parse pins
                int pinsIdx = objStr.indexOf("\"pins\":[");
                if (pinsIdx != -1) {
                    int startP = pinsIdx + 8;
                    int endP = objStr.indexOf("]", startP);
                    if (endP != -1) {
                        String pinsStr = objStr.substring(startP, endP);
                        int pStart = 0;
                        while (pStart < pinsStr.length()) {
                            int pComma = pinsStr.indexOf(",", pStart);
                            if (pComma == -1) pComma = pinsStr.length();
                            String pVal = pinsStr.substring(pStart, pComma);
                            pVal.trim();
                            if (pVal.length() > 0) entPins.push_back((int8_t)pVal.toInt());
                            pStart = pComma + 1;
                        }
                    }
                }

                // Parse params
                int paramsIdx = objStr.indexOf("\"params\":[");
                if (paramsIdx != -1) {
                    int startPr = paramsIdx + 10;
                    int endPr = objStr.indexOf("]", startPr);
                    if (endPr != -1) {
                        String paramsStr = objStr.substring(startPr, endPr);
                        int prStart = 0;
                        while (prStart < paramsStr.length()) {
                            int prComma = paramsStr.indexOf(",", prStart);
                            if (prComma == -1) prComma = paramsStr.length();
                            String prVal = paramsStr.substring(prStart, prComma);
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

    portENTER_CRITICAL(&hardwareMux);
    hardwareEntities = newEntities;
    portEXIT_CRITICAL(&hardwareMux);

    for (const auto& ent : hardwareEntities) {
        if (isAxisDevice(ent.type)) {
            if (ent.pins.size() >= 3) {
                pinMode(ent.pins[0], OUTPUT);
                pinMode(ent.pins[1], OUTPUT);
                pinMode(ent.pins[2], OUTPUT);
                digitalWrite(ent.pins[2], LOW);
            }
            if (ent.pins.size() >= 4 && ent.pins[3] != -1) pinMode(ent.pins[3], INPUT_PULLUP);
            if (ent.pins.size() >= 5 && ent.pins[4] != -1) pinMode(ent.pins[4], INPUT_PULLUP);
        } else if (ent.type == DEVICE_PWM) {
            if (ent.pins.size() >= 1) ledcAttach(ent.pins[0], 5000, 8);
            if (ent.pins.size() >= 2) pinMode(ent.pins[1], INPUT);
        }

        else if (ent.type == DEVICE_GPIO) {
            if (ent.pins.size() >= 1) {
                int isOutput = (ent.params.size() >= 1 && ent.params[0] == 1.0f) ? 1 : 0;
                if (isOutput) {
                    pinMode(ent.pins[0], OUTPUT);
                    digitalWrite(ent.pins[0], LOW);
                } else {
                    pinMode(ent.pins[0], INPUT_PULLUP);
                }
            }
        }
    }

    size_t detectedAxes = 0;
    for (const auto& ent : hardwareEntities) {
        if (isAxisDevice(ent.type)) detectedAxes++;
    }

    portENTER_CRITICAL(&ecsMux);
    motionPipeline.init(detectedAxes);
    portEXIT_CRITICAL(&ecsMux);

    client->text("SCA");
}

void handleGetConfig(AsyncWebSocketClient *client) {
    String responseJson = "CFG_RESP,[";
    portENTER_CRITICAL(&hardwareMux);
    for (size_t i = 0; i < hardwareEntities.size(); i++) {
        responseJson += "{\"name\":\"" + hardwareEntities[i].name + "\",";
        responseJson += "\"type\":" + String(hardwareEntities[i].type) + ",";
        responseJson += "\"pins\":[";
        for (size_t p = 0; p < hardwareEntities[i].pins.size(); p++) {
            responseJson += String(hardwareEntities[i].pins[p]);
            if (p + 1 < hardwareEntities[i].pins.size()) responseJson += ",";
        }
        responseJson += "],\"params\":[";
        for (size_t pr = 0; pr < hardwareEntities[i].params.size(); pr++) {
            responseJson += String(hardwareEntities[i].params[pr]);
            if (pr + 1 < hardwareEntities[i].params.size()) responseJson += ",";
        }
        responseJson += "]}";
        if (i + 1 < hardwareEntities.size()) responseJson += ",";
    }
    portEXIT_CRITICAL(&hardwareMux);
    responseJson += "]";
    client->text(responseJson);
}

void handleSetPWM(AsyncWebSocketClient *client, char* payload) {
    char deviceName[20];
    float targetT = 0;
    if (sscanf(payload, "%19[^,],%f", deviceName, &targetT) == 2) {
        portENTER_CRITICAL(&hardwareMux);
        for (auto& ent : hardwareEntities) {
            if (ent.name == deviceName && ent.type == DEVICE_PWM && !ent.params.empty()) {
                ent.params[0] = targetT;
                portEXIT_CRITICAL(&hardwareMux);
                client->text("ACK");
                return;
            }
        }
        portEXIT_CRITICAL(&hardwareMux);
    }
    client->text("ERR_DEV");
}

void handleHome(AsyncWebSocketClient *client, char* payload) {
    char targetAxis[20];
    if (sscanf(payload, "%19s", targetAxis) != 1) return;

    std::vector<int> axisIndices;
    portENTER_CRITICAL(&hardwareMux);
    for (size_t i = 0; i < hardwareEntities.size(); i++) {
        if (isAxisDevice(hardwareEntities[i].type)) axisIndices.push_back(i);
    }
    portEXIT_CRITICAL(&hardwareMux);

    if (axisIndices.empty()) {
        client->text("ERR_NO_AXIS");
        return;
    }

    bool isAll = (strcmp(targetAxis, "ALL") == 0);
    bool found = false;
    std::vector<int32_t> homingSteps(axisIndices.size(), 0);

    portENTER_CRITICAL(&hardwareMux);
    for (size_t i = 0; i < axisIndices.size(); i++) {
        String axName = hardwareEntities[axisIndices[i]].name;
        DeviceType axType = hardwareEntities[axisIndices[i]].type;
        if (isAll || axName.equalsIgnoreCase(targetAxis)) {
            if (axType == DEVICE_AXIS_LINEAR_LIMITED || axType == DEVICE_AXIS_ROTARY_LIMITED) {
                homingSteps[i] = -1;
                found = true;
            }
        }
    }
    portEXIT_CRITICAL(&hardwareMux);

    if (found) {
        portENTER_CRITICAL(&ecsMux);
        motionPipeline.push(50000, homingSteps);
        portEXIT_CRITICAL(&ecsMux);
        client->text("HACK");
    } else {
        client->text("ERR_HOME");
    }
}

void handleGpio(AsyncWebSocketClient *client, char* payload) {
    char deviceName[20];
    int val = -1;
    int scanned = sscanf(payload, "%19[^,],%d", deviceName, &val);

    portENTER_CRITICAL(&hardwareMux);
    bool found = false;
    int8_t pin = -1;
    float modeParam = 0;

    for (auto& ent : hardwareEntities) {
        if (ent.name == deviceName && ent.type == DEVICE_GPIO) {
            if (!ent.pins.empty()) pin = ent.pins[0];
            if (!ent.params.empty()) modeParam = ent.params[0];
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
    portEXIT_CRITICAL(&hardwareMux);

    if (!found || pin == -1) {
        client->text("ERR_GPIO");
        return;
    }

    int currentState = digitalRead(pin);
    char resp[40];
    snprintf(resp, sizeof(resp), "G_READ,%s,%d", deviceName, currentState);
    client->text(resp);
}

void handleEmergencyStop(AsyncWebSocketClient *client) {
    portENTER_CRITICAL(&ecsMux);
    motionPipeline.clear();
    portEXIT_CRITICAL(&ecsMux);

    portENTER_CRITICAL(&hardwareMux);
    for (auto& ent : hardwareEntities) {
        if (ent.type == DEVICE_PWM && !ent.params.empty()) {
            ent.params[0] = 0.0f;
            if (!ent.pins.empty()) ledcWrite(ent.pins[0], 0);
        }
        if (isAxisDevice(ent.type) && ent.pins.size() >= 3) {
            digitalWrite(ent.pins[2], HIGH);
        }
    }
    portEXIT_CRITICAL(&hardwareMux);
    client->text("EMG_ACK");
}

void handleDwell(AsyncWebSocketClient *client, char* payload) {
    unsigned long dwellTime = atoi(payload);
    unsigned long startWait = millis();
    while ((millis() - startWait) < dwellTime) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    client->text("ACK");
}

void handleMotionPush(AsyncWebSocketClient *client, char* buffer) {
    char* token = strtok(buffer, ",");
    if (token == NULL) {
        client->text("ERR_GCODE");
        return;
    }

    uint32_t t = (uint32_t)strtoul(token, NULL, 10);
    std::vector<int32_t> stepsList;

    while ((token = strtok(NULL, ",")) != NULL) {
        stepsList.push_back((int32_t)atol(token));
    }

    if (t > 0 && !stepsList.empty()) {
        portENTER_CRITICAL(&ecsMux);
        bool success = motionPipeline.push(t, stepsList);
        size_t current_count = motionPipeline.count();
        portEXIT_CRITICAL(&ecsMux);

        if (!success || current_count > 800) {
            client->text("BUF_FULL");
        } else {
            client->text("ACK");
        }
    } else {
        client->text("ERR_GCODE");
    }
}

void handleWebSocketMessage(AsyncWebSocketClient *client, void *arg, uint8_t *data, size_t len) {
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
    else if (strncmp(buffer, "HOME,", 5) == 0)            handleHome(client, buffer + 5);
    else if (strncmp(buffer, "GPIO,", 5) == 0)            handleGpio(client, buffer + 5);
    else if (strcmp(buffer, "EMG_STOP") == 0)            handleEmergencyStop(client);
    else if (strcmp(buffer, "R_MCU") == 0)               { delay(50); ESP.restart(); }
    else if (strncmp(buffer, "DWELL,", 6) == 0)           handleDwell(client, buffer + 6);
    else                                                  handleMotionPush(client, buffer);
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        handleWebSocketMessage(client, arg, data, len);
    }
}

inline void initNetwork(const char* ssid, const char* password) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWi-Fi Connected Successfully!");
    Serial.print("Printer IP Address: ");
    Serial.println(WiFi.localIP());

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.begin();
}
