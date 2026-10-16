// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#include<module/Common.h>
#include<module/HAL.h>
#include<module/ECSContext.h>
#include "module/HardwareConfig.h"
#include<module/AnalogReader.h>
#include<module/NetworkHandlers.h>
#include<module/Motion.h>
#include<module/Tasks.h>

void setup() {
    Serial.begin(115200);
    if(!LittleFS.begin(true)) Serial.println("LittleFS Mount Failed!");
    initHardwareECS();
    initSystemTasks();
    initNetwork(WIFI_SSID, WIFI_PASS);
}

void loop() {
    ws.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(10));
}
