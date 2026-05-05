# Antarctic Submersible Probe — Mission Control System

## Project Overview

This repository contains the firmware, data pipeline, and GUI for a submersible Antarctic probe system. The architecture uses **ESP-NOW** for ultra-low-power wireless data transfer from a retrieved probe to a researcher's laptop — operating entirely **offline**, with no internet dependency.

**System flow:**
```
Submersible Probe (ESP32 + F-RAM)
        │  [ESP-NOW]
        ▼
Receiver ESP32 (USB → Laptop)
        │  [Serial/USB]
        ▼
Local Mission Control GUI (Python)
        │
        ▼
Offline ML Anomaly Detection
```

---

## Repository Structure

```
├── Data Transfer Subsystem/
│   ├── Probe/
│   │   ├── Antarctic_probe.ino
│   │   ├── ble_server.cpp / .h
│   │   ├── data_packet.cpp / .h
│   └── Receiver/
│       ├── ble_client.py
│       ├── data_store.py
│       ├── packet_parser.py
│       └── probe_interface.py
└── GUI/
    ├── GUI.py
    ├── GUI_idea.jpg
    └── Graph_tab.jpg
```

---

## Data Transfer Subsystem

### Probe (`Data Transfer Subsystem/Probe/`)

Arduino/ESP-IDF firmware running on the submersible ESP32.

| File | Description |
|------|-------------|
| `Antarctic_probe.ino` | Main Arduino sketch — entry point for probe firmware. Initialises peripherals, manages sensor data logging to F-RAM, and triggers ESP-NOW transmission on retrieval. |
| `ble_server.cpp/.h` | BLE/ESP-NOW server implementation. Handles wireless transmission of logged data to the receiver. *(Note: module named `ble_server` reflects earlier BLE architecture; now operates over ESP-NOW.)* |
| `data_packet.cpp/.h` | Defines the data packet structure used for transmission. Handles serialisation of sensor readings into fixed-format packets. |

### Receiver (`Data Transfer Subsystem/Receiver/`)

Python scripts running on the researcher's laptop, interfacing with the receiver ESP32 over USB serial.

| File | Description |
|------|-------------|
| `ble_client.py` | Connects to the receiver ESP32 and receives incoming data packets. Counterpart to `ble_server` on the probe side. |
| `packet_parser.py` | Parses raw binary/serial packet data into structured Python objects using the format defined by `data_packet`. |
| `data_store.py` | Handles local persistence of parsed sensor data (e.g., writing to file or local database for GUI consumption). |
| `probe_interface.py` | High-level interface that coordinates the client, parser, and store — the main entry point for the receiver pipeline. |

---

## GUI (`GUI/`)

Local Mission Control GUI running on the researcher's laptop.

| File | Description |
|------|-------------|
| `GUI.py` | Main Python GUI application. Displays incoming sensor data, supports real-time graphing, and interfaces with localised ML models for offline anomaly detection. |
| `GUI_idea.jpg` | Design mockup/wireframe used during GUI planning. |
| `Graph_tab.jpg` | Screenshot or mockup of the graph/visualisation tab in the GUI. |

---

## Architecture Notes

- **No internet required** — all processing, storage, and ML inference runs locally on the laptop.
- **ESP-NOW protocol** — chosen for ultra-low power consumption on the probe side; operates independently of Wi-Fi association.
- **F-RAM logging** — sensor data is written to non-volatile ferroelectric RAM on the probe during deployment; transmitted in bulk upon retrieval.
- **LTO battery** — powers the probe during submersion; selected for low-temperature performance in Antarctic conditions.
