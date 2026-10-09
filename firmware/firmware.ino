//MIT License
//
//Copyright (c) 2026 TechruWolf & Gemini (AI Collaborator)
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <vector>
#include <cmath>
#include <algorithm>

// ==========================================
// 1. UNIVERSAL HARDWARE ECS ARCHITECTURE
// ==========================================
enum DeviceType {
    DEVICE_AXIS_LINEAR_LIMITED = 0,
    DEVICE_PWM_PID,
    DEVICE_GPIO_OUT,
    DEVICE_GPIO_IN,
    DEVICE_AXIS_LINEAR_UNLIMITED,
    DEVICE_AXIS_ROTARY_LIMITED,
    DEVICE_AXIS_ROTARY_UNLIMITED
};

inline bool isAxisDevice(DeviceType type) {
    return (type == DEVICE_AXIS_LINEAR_LIMITED || 
            type == DEVICE_AXIS_LINEAR_UNLIMITED || 
            type == DEVICE_AXIS_ROTARY_LIMITED || 
            type == DEVICE_AXIS_ROTARY_UNLIMITED);
}

Preferences preferences;

// ==========================================
// 2. WI-FI CONFIGURATION (STA MODE)
// ==========================================
const char* ssid = "YOUR_NEIGHBOR";
const char* password = "PASSWORD_YOU_GUESS";
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ==========================================
// 3. SAFETY PIN BLACKLIST FOR ESP32-S3
// ==========================================
struct PinRange {
    int8_t start;
    int8_t end;
};

const PinRange PIN_BLACKLIST[] = {
    {0, 0},     // Boot strap
    {3, 3},     // Strapping pin
    {26, 37},   // Flash / PSRAM internal bus
    {43, 44},   // UART0 debug
    {45, 46}    // VDD_SPI
};

bool isPinSafe(int8_t pin) {
    if (pin == -1) return true;
    if (pin < 0 || pin > 48) return false;
    for (const auto& range : PIN_BLACKLIST) {
        if (pin >= range.start && pin <= range.end) return false;
    }
    return true;
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

portMUX_TYPE hardwareMux = portMUX_INITIALIZER_UNLOCKED;

// Default Hardware Entity List
std::vector<HardwareEntity> hardwareEntities = {
    { "X",          DEVICE_AXIS_LINEAR_LIMITED,    {13, 12,  6, 48, 38},    {80.0f, 350.0f,  0.0f,  0.0f,  0.0f},      0,    0.0f,   0.0f },
    { "Y",          DEVICE_AXIS_LINEAR_LIMITED,    {10,  9, 11, 39, 40},    {80.0f, 350.0f,  0.0f,  0.0f,  0.0f},      0,    0.0f,   0.0f },
    { "Z",          DEVICE_AXIS_LINEAR_LIMITED,    { 5,  7,  8, 41, -1},    {400.0f, 350.0f,  0.0f,  0.0f,  0.0f},     0,    0.0f,   0.0f },
    { "E0",         DEVICE_AXIS_ROTARY_UNLIMITED,  {16, 17, 18, -1, -1},  {100.0f,   0.0f,  0.0f,  0.0f,  0.0f},     0,    0.0f,   0.0f },
    { "HEATER_0",   DEVICE_PWM_PID,             {14, 1},               {0.0f,   0.0f, 22.2f, 1.08f, 114.0f},       0,    0.0f,   0.0f },
    { "HEATED_BED", DEVICE_PWM_PID,             {21, 4},               {0.0f,   0.0f, 10.0f, 0.5f,  20.0f},        0,    0.0f,   0.0f },
    { "FAN_PART",   DEVICE_GPIO_OUT,               {42},                  {0.0f,   0.0f,  0.0f,  0.0f,  0.0f},        0,    0.0f,   0.0f },
    { "DOOR_SW",    DEVICE_GPIO_IN,                {2},                   {0.0f,   0.0f,  0.0f,  0.0f,  0.0f},        0,    0.0f,   0.0f }
};


// ==========================================
// 4. TEMPERATURE SENSOR & PID ENGINE
// ==========================================
float readThermistorTherm(uint8_t pin) {
    int rawADC = analogRead(pin);
    if (rawADC <= 0 || rawADC >= 4095) return 0.0f;
    float r_ntc = 4700.0f / ((4095.0f / (float)rawADC) - 1.0f);
    float steinhart = (log(r_ntc / 100000.0f) / 3950.0f) + (1.0f / 298.15f);
    return (1.0f / steinhart) - 273.15f;
}

void tempControlTask(void *pvParameters) {
    portENTER_CRITICAL(&hardwareMux);
    for (auto& ent : hardwareEntities) {
        if (ent.type == DEVICE_PWM_PID && !ent.pins.empty()) {
            ledcAttach(ent.pins[0], 5000, 8);
        }
    }
    portEXIT_CRITICAL(&hardwareMux);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(200);
    const float dt = 0.2f;

    for (;;) {
        portENTER_CRITICAL(&hardwareMux);
        for (auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM_PID && ent.pins.size() >= 2 && ent.params.size() >= 5) {
                uint8_t heaterPin = ent.pins[0];
                uint8_t sensorPin = ent.pins[1];
                
                float targetTemp = ent.params[0];
                float currentTemp = readThermistorTherm(sensorPin);
                ent.params[1] = currentTemp; 

                if (targetTemp <= 0) {
                    ledcWrite(heaterPin, 0);
                    ent.runtimeState2 = 0; 
                    ent.runtimeState3 = 0; 
                    ent.runtimeState1 = 0; 
                } else {
                    float error = targetTemp - currentTemp;
                    float Kp = ent.params[2], Ki = ent.params[3], Kd = ent.params[4];
                    
                    ent.runtimeState2 = constrain(ent.runtimeState2 + (error * dt), -100.0f, 100.0f);
                    float derivative = (error - ent.runtimeState3) / dt;
                    ent.runtimeState3 = error;

                    int pwmValue = constrain((int)((Kp * error) + (Ki * ent.runtimeState2) + (Kd * derivative)), 0, 255);
                    ledcWrite(heaterPin, pwmValue);
                    ent.runtimeState1 = pwmValue;
                }
            }
        }
        portEXIT_CRITICAL(&hardwareMux);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ==========================================
// 5. DYNAMIC MOTION PIPELINE & ECS ENGINE
// ==========================================
#define MOTION_BUFFER_SIZE 1000

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

MotionBufferECS motionPipeline;
portMUX_TYPE ecsMux = portMUX_INITIALIZER_UNLOCKED;
size_t globalNumAxes = 0;

// ==========================================
// 6. WEBSOCKET, RUNTIME GPIO & COMMAND PARSER
// ==========================================
void handleWebSocketMessage(AsyncWebSocketClient *client, void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        if (len >= 2048) {
            client->text("ERROR_BUFFER_TOO_LONG");
            return;
        }
        char buffer[2048];
        memcpy(buffer, data, len);
        buffer[len] = '\0';

        if (strncmp(buffer, "SAVE_CONFIG_JSON,", 17) == 0) {
            String jsonPayload = String(buffer + 17);
            
            std::vector<HardwareEntity> newEntities;
            int depth = 0;
            int startIndex = -1;
            
            for (size_t i = 0; i < jsonPayload.length(); i++) {
                char c = jsonPayload.charAt(i);
                if (c == '{') {
                    if (depth == 0) startIndex = i;
                    depth++;
                } else if (c == '}') {
                    depth--;
                    if (depth == 0 && startIndex != -1) {
                        String objStr = jsonPayload.substring(startIndex, i + 1);
                        
                        String entName = "UNKNOWN";
                        DeviceType entType = DEVICE_AXIS_LINEAR_LIMITED;
                        std::vector<int8_t> entPins;
                        std::vector<float> entParams;
                        
                        // 1. Parse name
                        int nameIdx = objStr.indexOf("\"name\":\"");
                        if (nameIdx != -1) {
                            int startN = nameIdx + 8;
                            int endN = objStr.indexOf("\"", startN);
                            if (endN != -1) entName = objStr.substring(startN, endN);
                        }
                        
                        // 2. Parse type
                        int typeIdx = objStr.indexOf("\"type\":");
                        if (typeIdx != -1) {
                            int startT = typeIdx + 7;
                            int endT = startT;
                            while (endT < objStr.length() && (isDigit(objStr.charAt(endT)) || objStr.charAt(endT) == '-')) endT++;
                            entType = (DeviceType)objStr.substring(startT, endT).toInt();
                        }
                        
                        // 3. Parse pins array
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
                        
                        // 4. Parse params array
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

            if (!newEntities.empty()) {
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
                    }
                    else if (ent.type == DEVICE_PWM_PID) {
                        if (ent.pins.size() >= 2) {
                            pinMode(ent.pins[1], INPUT); 
                        }
                    }
                    else if (ent.type == DEVICE_GPIO_OUT) {
                        if (ent.pins.size() >= 1) {
                            pinMode(ent.pins[0], OUTPUT);
                            digitalWrite(ent.pins[0], LOW);
                        }
                    }
                    else if (ent.type == DEVICE_GPIO_IN) {
                        if (ent.pins.size() >= 1) {
                            pinMode(ent.pins[0], INPUT_PULLUP);
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

                client->text("SAVE_CONFIG_ACK");
            } else {
                client->text("ERROR_INVALID_JSON_FORMAT");
            }
            return;
        }

        if (strcmp(buffer, "GET_CONFIG_JSON") == 0) {
                    String responseJson = "CONFIG_JSON_RESP,[";
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
                    return;
                }

        if (strncmp(buffer, "SAVE_CONFIG_JSON,", 17) == 0) {
            portENTER_CRITICAL(&hardwareMux);
            portEXIT_CRITICAL(&hardwareMux);
            client->text("SAVE_CONFIG_ACK");
            return;
        }

        if (strncmp(buffer, "SET_PWM,", 9) == 0) {
            char deviceName[20];
            float targetT = 0;
            if (sscanf(buffer + 9, "%19[^,],%f", deviceName, &targetT) == 2) {
                portENTER_CRITICAL(&hardwareMux);
                for (auto& ent : hardwareEntities) {
                    if (ent.name == deviceName && ent.type == DEVICE_PWM_PID && !ent.params.empty()) {
                        ent.params[0] = targetT; 
                        portEXIT_CRITICAL(&hardwareMux);
                        client->text("ACK");
                        return;
                    }
                }
                portEXIT_CRITICAL(&hardwareMux);
            }
            client->text("ERROR_DEVICE_NOT_FOUND");
            return;
        }

        if (strncmp(buffer, "HOME,", 5) == 0) {
            char targetAxis[20];
            if (sscanf(buffer + 5, "%19s", targetAxis) == 1) {
                std::vector<int> axisIndices;
                portENTER_CRITICAL(&hardwareMux);
                for (size_t i = 0; i < hardwareEntities.size(); i++) {
                    if (isAxisDevice(hardwareEntities[i].type)) axisIndices.push_back(i);
                }
                portEXIT_CRITICAL(&hardwareMux);

                size_t numAxes = axisIndices.size();
                if (numAxes > 0) {
                    bool isAll = (strcmp(targetAxis, "ALL") == 0);
                    bool found = false;
                    
                    std::vector<int32_t> homingSteps(numAxes, 0);

                    portENTER_CRITICAL(&hardwareMux);
                    for (size_t i = 0; i < numAxes; i++) {
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
                        client->text("HOMING_ACK");
                    } else {
                        client->text("ERROR_AXIS_NOT_HOMABLE_OR_NOT_FOUND");
                    }
                } else {
                    client->text("ERROR_NO_AXES_DEFINED");
                }
                return;
            }
        }

        if (strncmp(buffer, "GPIO,", 5) == 0) {
            char deviceName[20];
            int val = -1;
            int scanned = sscanf(buffer + 5, "%19[^,],%d", deviceName, &val);
            
            portENTER_CRITICAL(&hardwareMux);
            for (auto& ent : hardwareEntities) {
                if (ent.name == deviceName) {
                    if (ent.type == DEVICE_GPIO_OUT && scanned == 2) {
                        if (!ent.pins.empty()) {
                            if (val > 1) {
                                ledcAttach(ent.pins[0], 25000, 8);
                                ledcWrite(ent.pins[0], constrain(val, 0, 255));
                            } else {
                                digitalWrite(ent.pins[0], val ? HIGH : LOW);
                            }
                            ent.runtimeState1 = val;
                        }
                        portEXIT_CRITICAL(&hardwareMux);
                        client->text("GPIO_ACK");
                        return;
                    } else if (ent.type == DEVICE_GPIO_IN) {
                        int state = digitalRead(ent.pins[0]);
                        portEXIT_CRITICAL(&hardwareMux);
                        char resp[40];
                        snprintf(resp, sizeof(resp), "GPIO_READ,%s,%d", ent.name.c_str(), state);
                        client->text(resp);
                        return;
                    }
                }
            }
            portEXIT_CRITICAL(&hardwareMux);
            client->text("ERROR_GPIO_NOT_FOUND");
            return;
        }

        if (strcmp(buffer, "EMERGENCY_STOP") == 0) {
            portENTER_CRITICAL(&ecsMux);
            motionPipeline.clear();
            portEXIT_CRITICAL(&ecsMux);

            portENTER_CRITICAL(&hardwareMux);
            for (auto& ent : hardwareEntities) {
                if (ent.type == DEVICE_PWM_PID && !ent.params.empty()) {
                    ent.params[0] = 0.0f; 
                    if (!ent.pins.empty()) ledcWrite(ent.pins[0], 0);
                }
                if (isAxisDevice(ent.type) && ent.pins.size() >= 3) {
                    digitalWrite(ent.pins[2], HIGH); 
                }
            }
            portEXIT_CRITICAL(&hardwareMux);
            client->text("EMERGENCY_ACK");
            return;
        }

        if (strcmp(buffer, "RESET_MCU") == 0) {
            delay(50);
            ESP.restart();
            return;
        }

        if (strncmp(buffer, "DWELL,", 6) == 0) {
            unsigned long dwellTime = atoi(buffer + 6);
            unsigned long startWait = millis();
            while (millis() - startWait < dwellTime) { vTaskDelay(pdMS_TO_TICKS(10)); }
            client->text("ACK");
            return;
        }

        uint32_t t = 0;
        std::vector<int32_t> stepsList;
        
        char* token = strtok(buffer, ",");
        if (token != NULL) {
            t = (uint32_t)strtoul(token, NULL, 10);
            
            while ((token = strtok(NULL, ",")) != NULL) {
                stepsList.push_back((int32_t)atol(token));
            }
        }

        if (t > 0 && !stepsList.empty()) {
            portENTER_CRITICAL(&ecsMux);
            bool success = motionPipeline.push(t, stepsList);
            size_t current_count = motionPipeline.count();
            portEXIT_CRITICAL(&ecsMux);

            if (!success || current_count > 800) {
                client->text("BUFFER_FULL");
            } else {
                client->text("ACK");
            }
        } else {
            client->text("ERROR_INVALID_GCODE_FORMAT");
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        handleWebSocketMessage(client, arg, data, len);
    }
}

// ==========================================
// TELEMETRY TASK (Pure ECS Dynamic Architecture)
// ==========================================
void telemetryTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);
    
    for (;;) {
        String statusMsg = "STATUS";
        
        portENTER_CRITICAL(&hardwareMux);
        
        for (const auto& ent : hardwareEntities) {
            if (ent.type == DEVICE_PWM_PID) {
                float currentTemp = (ent.params.size() >= 2) ? ent.params[1] : 0.0f;
                statusMsg += "," + String(currentTemp, 1);
            }
        }
        
        for (const auto& ent : hardwareEntities) {
            if (isAxisDevice(ent.type)) {
                float stepsPerUnit = (!ent.params.empty() && ent.params[0] > 0) ? ent.params[0] : 80.0f;
                float currentPos = (float)ent.runtimeState1 / stepsPerUnit;
                statusMsg += "," + String(currentPos, 2);
            }
        }
        
        portEXIT_CRITICAL(&hardwareMux);

        ws.textAll(statusMsg);
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ==========================================
// 7. MOTION CONTROL ENGINE
// ==========================================

void executeECSFrame() {
    uint32_t t;
    std::vector<int> axisIndices;
    
    portENTER_CRITICAL(&hardwareMux);
    for (size_t i = 0; i < hardwareEntities.size(); i++) {
        if (isAxisDevice(hardwareEntities[i].type)) axisIndices.push_back(i);
    }
    portEXIT_CRITICAL(&hardwareMux);

    size_t numAxes = axisIndices.size();
    if (numAxes == 0) return;

    std::vector<int32_t> s(numAxes, 0);

    portENTER_CRITICAL(&ecsMux);
    if (motionPipeline.isEmpty()) { 
        portEXIT_CRITICAL(&ecsMux); 
        return; 
    }
    motionPipeline.pop(t, s);
    portEXIT_CRITICAL(&ecsMux);

    if (t == 0) return;

    std::vector<int8_t> stepPins(numAxes);
    std::vector<int8_t> dirPins(numAxes);
    std::vector<int> entityIndices(numAxes);
    std::vector<float> stepsPerUnitArr(numAxes);
    std::vector<float> maxLimitArr(numAxes);
    std::vector<int8_t> minPins(numAxes);
    std::vector<int8_t> maxPins(numAxes);
    std::vector<DeviceType> axisTypes(numAxes);

    portENTER_CRITICAL(&hardwareMux);
    for (size_t i = 0; i < numAxes; i++) {
        int entIdx = axisIndices[i];
        entityIndices[i] = entIdx;
        const auto& axEnt = hardwareEntities[entIdx];
        stepPins[i] = axEnt.pins.size() > 0 ? axEnt.pins[0] : -1;
        dirPins[i] = axEnt.pins.size() > 1 ? axEnt.pins[1] : -1;
        stepsPerUnitArr[i] = axEnt.params.size() > 0 ? axEnt.params[0] : 80.0f;
        
        float configuredLimit = axEnt.params.size() > 1 ? axEnt.params[1] : 0.0f;
        maxLimitArr[i] = (configuredLimit > 0.0f) ? configuredLimit : 999999.0f;

        minPins[i] = axEnt.pins.size() > 3 ? axEnt.pins[3] : -1;
        maxPins[i] = axEnt.pins.size() > 4 ? axEnt.pins[4] : -1;
        axisTypes[i] = axEnt.type;
    }
    portEXIT_CRITICAL(&hardwareMux);

    std::vector<bool> is_homing(numAxes, false);
    std::vector<int32_t> d(numAxes, 0);
    
    for (size_t i = 0; i < numAxes; i++) {
        if (dirPins[i] == -1) continue;
        if (s[i] == -1 && (axisTypes[i] == DEVICE_AXIS_LINEAR_LIMITED || axisTypes[i] == DEVICE_AXIS_ROTARY_LIMITED)) {
            is_homing[i] = true;
            digitalWrite(dirPins[i], LOW);
            d[i] = 1000000;
        } else {
            digitalWrite(dirPins[i], s[i] >= 0 ? HIGH : LOW);
            d[i] = abs(s[i]);
        }
    }

    int32_t max_steps = 0;
    for (size_t i = 0; i < numAxes; i++) {
        if (d[i] > max_steps) max_steps = d[i];
    }

    if (max_steps == 0) {
        delayMicroseconds((t > 400000) ? 400000 : (t * 10));
        return;
    }

    std::vector<int32_t> err(numAxes, max_steps / 2);
    uint32_t base_delay_per_step = (uint32_t)(((uint64_t)t * 1000ULL) / (uint64_t)max_steps);
    bool use_scurve = (max_steps > 30); 
    uint32_t accel_steps = use_scurve ? (max_steps / 4) : 0;

    for (int32_t i = 0; i < max_steps; i++) {
        bool all_done = true;
        uint32_t current_delay = base_delay_per_step;

        if (use_scurve) {
            if (i < accel_steps) {
                float factor = 1.0f - (sin(((float)i / (float)accel_steps) * M_PI_2));
                current_delay = base_delay_per_step + (uint32_t)(base_delay_per_step * 3.0f * factor);
            } else if (i > max_steps - accel_steps) {
                float factor = sin(((float)(i - (max_steps - accel_steps)) / (float)accel_steps) * M_PI_2);
                current_delay = base_delay_per_step + (uint32_t)(base_delay_per_step * 3.0f * factor);
            }
        }

        for (size_t a = 0; a < numAxes; a++) {
            if (stepPins[a] != -1 && d[a] > 0) {
                int32_t direction = (s[a] < 0 || is_homing[a]) ? -1 : 1;
                
                if ((axisTypes[a] == DEVICE_AXIS_LINEAR_LIMITED || axisTypes[a] == DEVICE_AXIS_ROTARY_LIMITED) && ((i & 0x0F) == 0)) {
                    portENTER_CRITICAL(&hardwareMux);
                    int entIdx = entityIndices[a];
                    float current_pos_unit = (stepsPerUnitArr[a] > 0) ? ((float)hardwareEntities[entIdx].runtimeState1 / stepsPerUnitArr[a]) : 0;
                    portEXIT_CRITICAL(&hardwareMux);

                    bool soft_triggered = (direction < 0) ? (current_pos_unit <= -0.1f) : (current_pos_unit >= maxLimitArr[a] + 0.1f);
                    bool physical_triggered = false;
                    if (direction < 0) {
                        physical_triggered = (minPins[a] != -1 && digitalRead(minPins[a]) == LOW);
                    } else {
                        physical_triggered = (maxPins[a] != -1 && digitalRead(maxPins[a]) == LOW);
                    }

                    if (soft_triggered || physical_triggered) {
                        d[a] = 0;
                        if (is_homing[a]) {
                            portENTER_CRITICAL(&hardwareMux);
                            hardwareEntities[entityIndices[a]].runtimeState1 = 0;
                            portEXIT_CRITICAL(&hardwareMux);
                        }
                        continue;
                    }
                }
                
                all_done = false;
                err[a] -= d[a];
                if (err[a] < 0) {
                    err[a] += max_steps;
                    digitalWrite(stepPins[a], HIGH);
                    
                    int32_t stepDirection = (s[a] >= 0 && !is_homing[a]) ? 1 : -1;
                    portENTER_CRITICAL(&hardwareMux);
                    
                    volatile int32_t &posState = hardwareEntities[entityIndices[a]].runtimeState1;
                    posState += stepDirection;
                    if (axisTypes[a] == DEVICE_AXIS_ROTARY_UNLIMITED && maxLimitArr[a] < 999999.0f && stepsPerUnitArr[a] > 0) {
                        int32_t wrapSteps = (int32_t)(maxLimitArr[a] * stepsPerUnitArr[a]);
                        if (wrapSteps > 0) {
                            if (posState >= wrapSteps) posState -= wrapSteps;
                            else if (posState < 0) posState += wrapSteps;
                        }
                    }
                    
                    portEXIT_CRITICAL(&hardwareMux);

                    delayMicroseconds(2);
                    digitalWrite(stepPins[a], LOW);
                }
            }
        }
        if (all_done) break;
        if (current_delay > 0) delayMicroseconds(current_delay);
    }
}

void motionControlTask(void *pvParameters) {
    for (;;) {
        executeECSFrame();
        if (motionPipeline.isEmpty()) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

// ==========================================
// 8. SETUP & MAIN LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    if(!LittleFS.begin(true)) Serial.println("LittleFS Mount Failed!");

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
        }
        else if (ent.type == DEVICE_PWM_PID) {
            if (ent.pins.size() >= 2) {
                pinMode(ent.pins[1], INPUT); 
            }
        }
        else if (ent.type == DEVICE_GPIO_OUT) {
            if (ent.pins.size() >= 1) {
                pinMode(ent.pins[0], OUTPUT);
                digitalWrite(ent.pins[0], LOW);
            }
        }
        else if (ent.type == DEVICE_GPIO_IN) {
            if (ent.pins.size() >= 1) {
                pinMode(ent.pins[0], INPUT_PULLUP);
            }
        }
    }
    
    size_t detectedAxes = 0;
    for (const auto& ent : hardwareEntities) {
        if (isAxisDevice(ent.type)) detectedAxes++;
    }
    motionPipeline.init(detectedAxes);

    xTaskCreatePinnedToCore(tempControlTask, "PIDTask", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(telemetryTask, "TelemetryTask", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(motionControlTask, "MotionTask", 4096, NULL, 2, NULL, 1);

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

void loop() {
    ws.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(10));
}
