// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

#pragma once

/**
 * @brief Master hardware initialization function. Iterates through all entities,
 * dispatches initialization based on device type, and sets up the motion pipeline buffer.
 */
inline void initHardwareECS() {
    initMotionSubsystem(); // Required by Motion.h
    initPWMAndSensorSubsystem(); // Required by SensorAndPWM.h
    initGpioSubsystem(); // Required by GPIO.h
}

inline void initSystemTasks() {
    SYS_TASK_CREATE(SensorAndPWMTask, "PIDTask", 4096, NULL, 1, NULL, 0);         // Required by SensorAndPWM.h
    SYS_TASK_CREATE(telemetryTask, "TelemetryTask", 4096, NULL, 1, NULL, 0);      // Required by Task.h
    SYS_TASK_CREATE(motionControlTask, "MotionTask", 4096, NULL, 2, NULL, 1);     // Required by Motion.h
    SYS_TASK_CREATE(PipelineTask, "PipelineTask", 4096, NULL, 1, NULL, 0);        // Required by Pipeline.h
    SYS_TASK_CREATE(systemMonitorTask, "SysMonitorTask", 2048, NULL, 1, NULL, 0); // Required by Task.h
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
