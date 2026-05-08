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