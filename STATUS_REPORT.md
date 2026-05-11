# SS2 Development Status Report & Logbook

> **Instructions for AI Agent:** After completing any task from `TASKS.md`, you MUST append a new entry to this document using the template below. This document serves as the academic and engineering logbook for the final EEE4113F report.

---

### [Date: YYYY-MM-DD] - [Task Name / Number]
* **Description of Work Completed:** [Detailed paragraph explaining what code was written, its purpose, and how it functions].
* **Engineering Rationale & Design Choices:** [Why was this specific library, algorithm, or hardware register used? Explain the trade-offs regarding power, memory, or processing speed].
* **Testing & Validation:** [How was this code tested? e.g., "Simulated dummy data via serial monitor", "Verified execution time using time.time()"].
* **References / Citations:** [List URLs to official documentation, Scikit-learn docs, Espressif ESP32-C6 Technical Reference Manuals, or academic papers relevant to the logic/theory].

---

### 2026-05-08 - Task 1.1: Generate Dummy Dataset
* **Description of Work Completed:** Created a Python script (`GUI/ml_backend/generate_dummy_data.py`) to generate a dummy dataset of 5,050 rows representing raw marine sensor data from the Antarctic probe. The data conforms exactly to the required 15-value string format (RecordNum, UnixTime, Temp, Pressure, and 11 Spectrometer channels). 5,000 rows represent normal data, while 50 anomalous rows were randomly injected with massive spikes in Spectrometer channels 6 and 7 to simulate abnormal microbial community shifts.
* **Engineering Rationale & Design Choices:** Since the probe logs raw sensor values with zero floating-point math to conserve power and memory, all generated values are simulated as integer ADC counts (e.g., 12-bit for Temp/Pressure, 8-to-12-bit counts for the spectrometer). Python's built-in `random` and `csv` modules were selected to keep the script highly modular and independent, with a minimal execution time (<0.05 seconds). The dataset includes a header row for easier parsing via pandas during the upcoming ML training phase.
* **Testing & Validation:** Validated through `if __name__ == '__main__':` execution block. The script successfully executed in ~0.045 seconds, producing a 322.65 KB CSV file.
* **References / Citations:** Python `csv` documentation (https://docs.python.org/3/library/csv.html), Python `random` documentation (https://docs.python.org/3/library/random.html).

---

### 2026-05-09 - Task 1.2: Train Isolation Forest
* **Description of Work Completed:** Created a Python script (`GUI/ml_backend/train_model.py`) that uses `pandas` to ingest the previously generated `dummy_marine_data.csv`. The `RecordNum` and `UnixTime` features were deliberately dropped, allowing the model to focus exclusively on the physical and chemical environment factors (Temp, Pressure, Spectrometer channels). We then configured and trained a `scikit-learn` Isolation Forest model, exporting it to `anomaly_model.joblib` using the `joblib` library.
* **Engineering Rationale & Design Choices:** The `contamination` parameter of the Isolation Forest was strictly set to `0.0099` (50 anomalies / 5050 total rows) to accurately reflect the expected statistical likelihood of anomaly occurrence based on our injected dataset. The model evaluates only the 13 raw sensor columns. An Isolation Forest is highly effective for this edge-ML context because its inference is fast, deterministic, and requires low memory, making it ideal for the laptop-side GUI.
* **Testing & Validation:** Ground truth was extracted using the same threshold logic used to inject anomalies (`RawSpec6` > 1000 or `RawSpec7` > 1000). The validation achieved near-perfect accuracy (Overall Accuracy: 1.00). The confusion matrix yielded 4999 True Negatives, 49 True Positives, 1 False Negative, and 1 False Positive, demonstrating a 0.98 precision and recall on the anomaly class. Execution runs locally via `n_jobs=-1` for maximum inference speed on available cores.
* **References / Citations:** Scikit-learn Isolation Forest Documentation (https://scikit-learn.org/stable/modules/generated/sklearn.ensemble.IsolationForest.html), Joblib documentation for model persistence (https://joblib.readthedocs.io/en/latest/).

---

### 2026-05-09 - Task 1.3: Modular Anomaly Detector Class
* **Description of Work Completed:** Created `GUI/ml_backend/anomaly_detector.py` containing the `MarineAnomalyDetector` class. This class acts as a modular wrapper around the exported Isolation Forest model. It handles the instantiation and loading of `anomaly_model.joblib` via `joblib`, parsing of the incoming raw serial comma-separated strings from the ESP-NOW receiver, extraction of the relevant 13 sensor parameters, and inference. The output is mapped strictly to boolean values (`True` for Anomaly, `False` for Normal) for direct use by the laptop GUI.
* **Engineering Rationale & Design Choices:** The method accepts raw strings directly to minimize parsing responsibilities in the main `GUI.py` application. Features are converted into a pandas DataFrame using precise column headers before inference to avoid scikit-learn feature name warnings. Explicit error handling (`try-except`) is used for model loading and data formatting to prevent the overarching mission control GUI from crashing if invalid data is received.
* **Testing & Validation (SR3 Check):** Verified using a simulated normal string and a simulated anomalous string. Inference execution speeds were `~0.012` seconds (Normal) and `~0.006` seconds (Anomalous). This successfully satisfies System Requirement SR3 (< 2.0 seconds).
* **Acceptance Test Procedures (ATPs) Outline:**
  * **Previous (Task 1.1 & 1.2):** 
    * *ATP 1.1:* Execute `generate_dummy_data.py`. Verify `dummy_marine_data.csv` is created. Assert exactly 5050 rows and exactly 15 comma-separated integer values per row.
    * *ATP 1.2:* Execute `train_model.py`. Validate the printed confusion matrix for >90% accuracy. Assert the creation of `anomaly_model.joblib`.
  * **Current (Task 1.3):** 
    * *ATP 1.3:* Instantiate `MarineAnomalyDetector`. Pass a valid 15-value string. Assert return type is boolean. Assert total execution time is < 2.0s via Python `time` module.
  * **Future (Phase 2 & 3):**
    * *ATP 2.2 (F-RAM):* Call simulated `fram_write()`, then `fram_read()`. Assert retrieved bytes exactly match written bytes.
    * *ATP 2.3 (Firmware):* Monitor serial output. Assert the ESP32 enters Deep Sleep and wakes on timer. Assert NO blocking `delay()` is executed.
    * *ATP 3.1 (Integration):* Transmit packets from ESP32 via ESP-NOW. Assert laptop Python GUI receives matching packets via serial receiver.
* **References / Citations:** Joblib loading (https://joblib.readthedocs.io/en/latest/), Python Time Module (https://docs.python.org/3/library/time.html).

---

### 2026-05-10 - Task 2.1 to 2.3: Onboard Firmware Development
* **Description of Work Completed:** Created the `kiyuran_firmware` directory for Subsystem 2 onboard processing. `platformio.ini` was configured for the ESP32-C6 using the community `pioarduino` fork to access upstream Arduino v3.0 core support. `fram_manager.cpp` implements a simulated Hardware Abstraction Layer (HAL) for the delayed F-RAM chip. `main.cpp` orchestrates the mission lifecycle with a 3-state machine (`DEEP_SLEEP`, `WAKE_AND_LOG`, `OFFLOAD`) routing via `esp_sleep_get_wakeup_cause()`.
* **Engineering Rationale & Design Choices:** Because the F-RAM is unavailable, it is simulated using a 2D `char` array explicitly tagged with `RTC_DATA_ATTR`. This correctly emulates non-volatile memory survival across Deep Sleep cycles, maintaining memory state without draining power. C++ `String` objects are not retained in RTC memory, thus fixed char arrays were utilized to adhere to our 50-byte maximum payload constraint. The state machine avoids all blocking `delay()` functions (adhering to SR1). Instead, it rapidly evaluates the wake cause (Timer vs GPIO), executes either data-logging or offload routines, configures the next sleep triggers (`esp_sleep_enable_timer_wakeup` / `esp_sleep_enable_ext0_wakeup`), and instantly flushes Serial before calling `esp_deep_sleep_start()`.
* **Testing & Validation:** The firmware successfully compiled for the RISC-V ESP32-C6 architecture via PlatformIO CLI (using upstream core 51.03.04). Code logic ensures minimal active duty cycle.
* **Acceptance Test Procedures (ATPs) Outline:**
  * **Previous (Phase 1):** Execute `generate_dummy_data.py` (Assert 15-val string). Execute `train_model.py` (Assert 1.0 accuracy). Instantiate `MarineAnomalyDetector` (Assert <2.0s eval logic).
  * **Current (Phase 2):** 
    * *ATP 2.2 (F-RAM HAL):* Ensure simulated data struct is tagged with `RTC_DATA_ATTR`. Assert memory increments correctly.
    * *ATP 2.3 (Firmware):* Compile via PlatformIO targeting ESP32-C6. Assert zero `delay()` calls in the state machine loop. Assert valid wakeup triggers.
  * **Future (Phase 3):**
    * *ATP 3.1 (Integration):* Splice F-RAM Offload function into Saeed's ESP-NOW transmission buffer. Assert laptop receives bytes matching logged records exactly.
* **References / Citations:** Espressif ESP32 Deep Sleep API Documentation (https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/system/sleep_modes.html), ESP32-C6 FireBeetle Specs (https://www.dfrobot.com/product-2771.html).

---

### 2026-05-11 - Task 3.1 & 3.2: Phase 3 Integration (Firmware & GUI ML)
* **Description of Work Completed:** Merged Subsystem 2's onboard processing firmware into Subsystem 1's `Antarctic_probe.ino` ESP-NOW transmission framework. Re-architected the `ProbeRecord_t` structure across the firmware to support a full 15-channel output, allowing all 11 spectrometer channels to be transmitted directly over the ESP-NOW binary protocol. In the Python layer, `Backend.py` was updated to import the `MarineAnomalyDetector` class, run instantaneous ML evaluations on the inflated 15-value incoming CSV packet, and pass a boolean `is_anomaly` flag into the GUI. The main `GUI.py` frontend was modified to plot anomalous points dynamically using a high-contrast red scatter plot overlay on the matplotlib UI axes, alongside a textual `[ANOMALY]` flag in the raw data feed.
* **Engineering Rationale & Design Choices:** Instead of artificially padding data on the backend, extending `ProbeRecord_t` to hold all 11 floats guarantees strict data continuity between the F-RAM state, the ESP-NOW transmission layer, and the offline ML evaluation. The packet size remains well below the 250-byte ESP-NOW Maximum Transmission Unit (MTU), resulting in zero network overhead penalty. Mapping the incoming parsed packet directly to a boolean anomaly flag inside the existing asynchronous `sync_probe` method elegantly preserves Saeed's UI decoupling and async event loops.
* **Testing & Validation:** Code statically analyzed to ensure `is_anomaly` flags propagate properly without interrupting the serial connection handshake. Verified that `Backend.py` retains its fallback timeout logic. 
* **References / Citations:** Matplotlib Scatter Plots (https://matplotlib.org/stable/api/_as_gen/matplotlib.pyplot.scatter.html), Espressif ESP-NOW Documentation (https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html).

---