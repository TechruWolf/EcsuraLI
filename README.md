# EcsuraLI
### _The Metadata-Driven, Microcontroller-Agnostic Engine for Custom CNCs, 3D Printers & Robotics_

> **"No recompilation. No heavy host dependencies. Pure metadata-driven embedded architecture."**

![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg) ![Architecture: ECS](https://img.shields.io/badge/Architecture-Dynamic%20ECS-success.svg) ![Status: Active](https://img.shields.io/badge/Status-Active-blue.svg)

💡 _Note: While the core architecture is designed to be hardware-agnostic and portable across capable microcontrollers, it has been rigorously tested and optimized primarily on the **ESP32-S3**._

## 🔥 What is this?

**EcsuraLI** is a next-generation, highly generalized embedded control framework designed to overcome the rigid, hardcoded constraints of legacy monolithic firmware, while eliminating the bulky, expensive host-computer dependencies typical of distributed control setups.

Powered by a pure **Entity Component System (ECS)** architecture and a **Runtime Hardware Registry**, this firmware allows you to completely reconfigure your physical machine—adding or removing motor axes, thermal PWM outputs, or GPIO peripherals—on the fly directly from a modern Web UI, **without ever recompiling or reflashing your microcontroller.**

## ⚡ Key Architecture Highlights

-   **🧩 100% Dynamic ECS Registry**: Firmware logic is fully decoupled from physical hardware configurations. Devices are generic entities (`DEVICE_PWM_PID`, `LINEAR_LIMITED`, `ROTARY_UNLIMITED`, etc.) managed via a runtime metadata array.
-   **🌐 Zero-Flash-Recompile Configuration**: Change your hardware map, swap GPIO pins, or adjust axes configuration through the web interface, applying changes instantly to runtime RAM.
-   **🖥️ Configuration-Driven Web UI**: No hardcoded frontend controls. The web application (`app.js` + `index.html`) dynamically queries the microcontroller registry upon connection to auto-render interactive dashboards and control panels.
-   **⚡ Standalone Single-Chip Powerhouse**: Runs entirely on a compact microcontroller equipped with network connectivity and WebSockets, completely removing the need for an external companion Linux host or secondary computer.
-   **📉 Dynamic G-Code Streaming**: G-code files are parsed and streamed on-the-fly by the connected client UI, mapping directly against the live, runtime hardware layout managed by the ESP32 registry.

## 🏗️ System Architecture Overview

⚠️ Project Status: Early Development (Alpha). The core metadata-driven ECS architecture is functional and tested on ESP32-S3, but APIs, registry structures, and features may change as the project evolves. Contributions and feedback are highly appreciated!

```
+----------------------------------------------------------+
|                 Modern Web Frontend                      |
|  (Auto-rendered UI via Dynamic Microcontroller Registry) |
+---------------------------+------------------------------+
                            | WebSocket (JSON & Stream)
                            v
+-------------------------------------------------------+
|            Core Microcontroller Firmware              |
|  +-------------------------------------------------+  |
|  |           Runtime RAM ECS Registry              |  |
|  +-------------------------------------------------+  |
|  |     Unified DEVICE_PWM_PID & Axis Engine        |  |
|  +-------------------------------------------------+  |
|  |     Client-Streamed G-Code Execution Engine     |  |
|  +-------------------------------------------------+  |
+---------------------------+---------------------------+
                            | Real-time Step/PWM Output
                            v
+-------------------------------------------------------+
|              Physical Hardware / Motors               |
+-------------------------------------------------------+
```

## 📊 Architectural Paradigm Comparison

| Design Aspect | Legacy Monolithic Approach | Distributed Split Approach | EcsuraLI (This Project) |
| --- | --- | --- | --- |
| **Hardware Configuration** | Static source-code compilation (e.g., header configuration files) | External text configuration files parsed by an auxiliary host | **Dynamic Runtime JSON Registry via Web GUI** |
| **Recompilation / Flashing** | Required for every minor pin, sensor, or axis modification | Not required for config changes | **Not required (Changes apply instantly on RAM)** |
| **Hardware Footprint** | Standard Microcontroller | Microcontroller + Dedicated Auxiliary Computer/Host | **Standalone Single Microcontroller** |
| **Core Architecture** | Traditional Procedural / Monolithic C++ | Client-Server Split (Host + MCU Firmware) | **Pure Entity Component System (ECS)** |

## 🛠️ Quick Start

1.  **Get the Source**: Download or obtain the source code files of this project.
2.  **Configure & Build**: Open the project folder in your preferred embedded IDE (such as **PlatformIO** or **Arduino IDE**), select your target microcontroller (optimized and tested primarily on the **ESP32-S3**), and build/flash the firmware.
3.  **Connect & Configure**:
    -   Power up your board and connect to its network interface.
    -   Open the Web Control panel in any modern browser.
    -   Navigate to **Hardware Config**, customize your machine profile, and click **Save Configuration (Runtime RAM)**!

## 🤝 Contributing

Contributions, architectural debates, multi-MCU porting efforts, and custom machine profiles are warmly welcomed! Feel free to open an issue or submit a Pull Request.

## 📜 License

Distributed under the **MIT License**. See `LICENSE` for more information.

___

_Created with passion by **TechruWolf** & **Gemini (AI Collaborator)**._
