# Antarctic Submersible Probe — Mission Control System

## Project Overview

This repository contains the integrated firmware, data pipeline, and GUI for a submersible Antarctic marine environmental probe system. The architecture represents the full integration of **Subsystem 1 (Wireless Communications)** and **Subsystem 2 (Edge Processing, Storage, and Machine Learning)**.

The system uses **ESP-NOW** for ultra-low-power wireless data transfer from a retrieved probe to a researcher's laptop, operating entirely **offline** with no internet dependency. Anomalies in microbial communities (detected via spectrometer spikes) are flagged instantly upon retrieval using an onboard offline Machine Learning model.

**Integrated System Flow:**
```text
Submersible Probe (ESP32-C6)
   ├── 3-State Firmware (Deep Sleep, Log, Offload)
   ├── Sensor readings parsed into 15-channel strings
   └── F-RAM (Simulated via RTC Memory)
        │
        │  [ESP-NOW Binary Protocol — ProbeRecord_t]
        ▼
Receiver Dongle (ESP32-C6 connected via USB)
   ├── Validates Checksums & Returns ACKs
   └── Serialises struct into CSV strings
        │
        │  [Serial/USB: 115200 baud]
        ▼
Local Mission Control GUI (Python)
   ├── Auto-connects to receiver dongle
   ├── Parses incoming data streams concurrently
   ├── Executes Scikit-Learn Isolation Forest Inference
   └── Visually flags [ANOMALY DETECTED] on GUI graphs
```

---

## Repository Structure

```text
├── Data Transfer Subsystem/
│   ├── Probe/
│   │   ├── Antarctic_probe.ino      # Main 3-state firmware loop
│   │   ├── espnow_transfer.cpp/.h   # ESP-NOW transmission logic
│   │   ├── fram_manager.cpp/.h      # F-RAM logging and struct parsing
│   │   └── data_packet.h            # Shared binary wire format (15 fields)
│   └── Receiver/
│       └── receiver_dongle.ino      # ESP-NOW receiver and Serial forwarder
└── GUI/
    ├── GUI.py                       # Main frontend dashboard
    ├── Backend.py                   # Serial comms, async sync_probe, anomaly evaluation
    └── ml_backend/                  
        ├── anomaly_detector.py      # MarineAnomalyDetector class wrapper
        ├── train_model.py           # Isolation Forest training script
        ├── generate_dummy_data.py   # Synthesises 15-channel marine data
        └── anomaly_model.joblib     # Persisted offline ML model
```

---

## Technical Architecture

### 1. Firmware & Storage (SS2)
- **Non-Blocking Architecture:** The probe operates a strict 3-state loop (`DEEP_SLEEP`, `WAKE_AND_LOG`, `OFFLOAD`). It uses hardware timer wakeups and GPIO interrupts (reed switch) to eliminate `delay()` calls entirely, preserving the LTO battery.
- **F-RAM Simulation:** Sensor arrays (Temp, Pressure, and 11x Spectrometer channels) are saved to RTC memory across deep sleep cycles to simulate high-speed, non-volatile F-RAM.
- **Data Packaging:** When retrieved, `fram_manager.cpp` inflates the stored strings back into binary `ProbeRecord_t` structs (incorporating 11 spectrometer arrays) for transmission.

### 2. ESP-NOW Wireless Transfer (SS1)
- **Binary Wire Format:** Uses packed C-structs (`ProbeRecord_t`) with XOR checksums to maximize MTU efficiency over the 2.4GHz spectrum.
- **Reliability:** Implements application-layer ACKs and automatic retries to ensure no packet drops between the probe and receiver dongle.

### 3. Mission Control GUI & Backend (SS1 + SS2)
- **Concurrency:** `Backend.py` uses threaded asynchronous loops to ensure the `customtkinter` frontend remains responsive while streaming high-speed serial data.
- **Auto-Discovery:** Performs DTR/RTS serial handshaking to locate the ESP32 receiver dongle automatically without manual COM port selection.

### 4. Edge Machine Learning (SS2)
- **Algorithm:** An Unsupervised `IsolationForest` identifies outliers based on 13 environmental features (dropping timestamps/IDs).
- **Inference Speed:** Executed directly on incoming serial packets within `Backend.py` via `anomaly_detector.py`. Average inference time is < 0.02 seconds per packet, satisfying real-time mission constraints.
- **Visualisation:** The GUI parses the boolean `is_anomaly` flags to plot outlier samples in high-contrast red over the Matplotlib telemetry graphs.

---

## How to Run

### 1. Setup the Hardware
1. Flash `Antarctic_probe.ino` to the primary ESP32-C6 (Probe).
2. Flash `receiver_dongle.ino` to the secondary ESP32-C6 (Receiver).
3. Connect the Receiver to your laptop via USB.

### 2. Setup the Python Environment
```bash
cd GUI
pip install -r requirements.txt
# Ensure scikit-learn, pandas, numpy, serial, customtkinter, matplotlib are installed
```

### 3. Start Mission Control
```bash
python GUI.py
```
- Click **"Connect"**. The backend will automatically handshake with the receiver dongle.
- Click **"Retrieve Data"**. 
- Simulate the probe retrieval by triggering GPIO 9 (Reed Switch) on the probe hardware. The F-RAM records will transfer wirelessly, be evaluated by the ML model, and plot directly to the dashboard.

### 4. Software Simulation Mode (Dev Mode)
If you do not have the physical hardware connected, you can simulate the full data processing and ML pipeline locally:
- Open the GUI (`python GUI.py`).
- Click **"🧪 Dev Mode: Simulate"** in the sidebar.
- The GUI will ingest the local synthetic dataset (`dummy_marine_data.csv`), simulating an ESP-NOW data stream at 0.5-second intervals. 
- Use the **Playback Control Panel** (Pause/Play, Restart, Stop) that appears to interactively control the simulation flow. Watch the graphs update live and verify the red anomaly flags without needing the physical ESP32 boards.
