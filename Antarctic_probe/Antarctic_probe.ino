/**
 * antarctic_probe_test.ino
 * ========================
 * Standalone BLE test script for SS1 — does NOT depend on SS2/SS4 hardware.
 * Generates synthetic sensor packets and streams them over BLE so the Python
 * backend (probe_interface.py / ble_client.py) can be validated end-to-end.
 *
 * What this test does:
 *   1. Initialises the BLE GATT server (BLEServer_Init)
 *   2. Populates metadata with a fake record count and start timestamp
 *   3. Starts advertising
 *   4. Waits for a laptop to connect and send the CONTROL b'\x01' command
 *   5. Builds NUM_TEST_PACKETS synthetic packets using build_packet()
 *   6. Streams them via BLEServer_StartTransfer()
 *   7. Stops advertising and halts (no deep sleep — keeps Serial open for debugging)
 *
 * ── How to change what is tested ─────────────────────────────────────────
 *   - Change NUM_TEST_PACKETS to send more or fewer records
 *   - Edit the BASE_* and DELTA_* constants to shift the simulated readings
 *   - To test deep sleep wakeup: replace the final while(1) with the
 *     esp_deep_sleep_enable_gpio_wakeup() block from your production .ino
 *   - To stress-test throughput: lower NOTIFY_INTERVAL_MS in ble_server.cpp
 *     and increase NUM_TEST_PACKETS to e.g. 500
 *
 * ── Expected Serial output (Arduino IDE Serial Monitor, 115200 baud) ──────
 *   [TEST] Building 20 synthetic packets...
 *   [BLE]  Initialising NimBLE stack...
 *   [BLE]  GATT server ready.
 *   [BLE]  Advertising started. Waiting for client...
 *   [BLE]  Client connected: XX:XX:XX:XX:XX:XX
 *   [BLE]  Transfer requested by client.
 *   [BLE]  Starting transfer of 20 packets...
 *   [BLE]  Transfer complete.
 *   [TEST] Done. Halted — reset board to run again.
 */

#include "ble_server.h"
#include "data_packet.h"

// ── Test parameters ────────────────────────────────────────────────────────
// Change these to adjust the synthetic dataset.

#define NUM_TEST_PACKETS   20       // How many records to send in this test run

// Starting (absolute) sensor values for packet 0
#define BASE_TIMESTAMP_MS  1000     // ms  — first record timestamp
#define BASE_TEMP_C        2.50f    // °C  — realistic Antarctic surface water
#define BASE_DEPTH_M       10.00f   // m
#define BASE_SALINITY_PSU  34.500f  // PSU
#define BASE_DO_UMOL       320.0f   // µmol/L dissolved oxygen
#define BASE_CHLORO_UG     0.500f   // µg/L chlorophyll-a
#define BASE_PH            8.100f   // pH

// Per-packet increments applied to each subsequent record
// Set any delta to 0.0f to keep that field constant across all packets
#define DELTA_TIMESTAMP_MS  5000    // ms between records (5 s interval)
#define DELTA_TEMP_C        0.05f   // slight warming with depth
#define DELTA_DEPTH_M       1.00f   // probe descending 1 m per sample
#define DELTA_SALINITY_PSU  0.010f
#define DELTA_DO_UMOL      -2.0f    // O2 decreasing with depth
#define DELTA_CHLORO_UG     0.010f
#define DELTA_PH           -0.002f

// ── Packet storage ─────────────────────────────────────────────────────────
// Stack-allocated array — fine for NUM_TEST_PACKETS up to ~200 on ESP32-C6.
// For larger tests move this to global scope (outside setup/loop).
static SensorPacket_t test_packets[NUM_TEST_PACKETS];

// ── build_test_packets ─────────────────────────────────────────────────────
/**
 * Fills test_packets[] with synthetic readings using the real build_packet()
 * function from data_packet.cpp. This exercises the same CRC and delta-
 * encoding path that production firmware will use, so any bug here will also
 * show up on the Python side.
 */
static void build_test_packets(void) {
    Serial.print("[TEST] Building ");
    Serial.print(NUM_TEST_PACKETS);
    Serial.println(" synthetic packets...");

    // Zero-initialise the previous-values tracker.
    // build_packet() treats a zeroed PreviousValues_t as "no previous record",
    // so packet 0 carries absolute values — matching what packet_parser.py expects.
    PreviousValues_t prev;
    memset(&prev, 0, sizeof(prev));

    // Accumulate floating-point values so each packet steps by the DELTA amounts
    float ts_ms    = BASE_TIMESTAMP_MS;
    float temp     = BASE_TEMP_C;
    float depth    = BASE_DEPTH_M;
    float salinity = BASE_SALINITY_PSU;
    float do_umol  = BASE_DO_UMOL;
    float chloro   = BASE_CHLORO_UG;
    float ph       = BASE_PH;

    for (uint16_t i = 0; i < NUM_TEST_PACKETS; i++) {
        test_packets[i] = build_packet(
            i,                      // sequence number
            &prev,                  // updated in-place each iteration
            (uint32_t)ts_ms,
            temp,
            depth,
            salinity,
            do_umol,
            chloro,
            ph
        );

        // Advance simulated readings for next packet
        ts_ms    += DELTA_TIMESTAMP_MS;
        temp     += DELTA_TEMP_C;
        depth    += DELTA_DEPTH_M;
        salinity += DELTA_SALINITY_PSU;
        do_umol  += DELTA_DO_UMOL;
        chloro   += DELTA_CHLORO_UG;
        ph       += DELTA_PH;
    }

    Serial.println("[TEST] Packets built OK.");
}

// ── setup ──────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);   // give Serial monitor time to attach before first print
    Serial.println("\n========== SS1 BLE Test ==========");

    // Step 1: Build synthetic data BEFORE initialising BLE so the metadata
    // record_count is accurate when the laptop reads it on connect.
    build_test_packets();

    // Step 2: Fill global metadata struct (declared in ble_server.h as extern).
    // The laptop reads this on connect to know how many packets to expect.
    g_probe_metadata.record_count     = NUM_TEST_PACKETS;
    g_probe_metadata.session_start_ms = BASE_TIMESTAMP_MS;
    g_probe_metadata.firmware_version = 1;

    // Step 3: Initialise BLE GATT server
    BLEStatus_t status = BLEServer_Init();
    if (status != BLE_OK) {
        Serial.print("[TEST] BLEServer_Init failed, code: ");
        Serial.println(status);
        while (1);   // halt — check Serial output
    }

    // Step 4: Start advertising so the laptop can find the probe
    status = BLEServer_StartAdvertising();
    if (status != BLE_OK) {
        Serial.print("[TEST] BLEServer_StartAdvertising failed, code: ");
        Serial.println(status);
        while (1);
    }

    Serial.println("[TEST] Waiting for laptop to connect and send CONTROL 0x01...");

    pinMode(15, OUTPUT);
}

// ── loop ───────────────────────────────────────────────────────────────────
/**
 * Polls for two conditions before starting the transfer:
 *   (a) a client is connected  — BLEServer_IsConnected()
 *   (b) the client wrote 0x01  — BLEServer_TransferRequested()
 *
 * Once both are true, builds packets and streams them.
 * After transfer, stops advertising and halts.
 * Reset the board to run the test again.
 */
void loop() {
    // put led on so aware that board is working correctly
    digitalWrite(15, HIGH);

    // Wait until laptop is connected AND has sent the CONTROL command
    if (!BLEServer_IsConnected() || !BLEServer_TransferRequested()) {
        delay(50);   // poll every 50 ms — low CPU cost
        return;
    }

    Serial.println("[TEST] Client ready — starting transfer.");

    BLEStatus_t result = BLEServer_StartTransfer(test_packets, NUM_TEST_PACKETS);

    if (result == BLE_OK) {
        Serial.println("[TEST] Transfer succeeded.");
    } else {
        Serial.print("[TEST] Transfer failed, code: ");
        Serial.println(result);
    }

    // Stop advertising — no more connections needed after transfer
    BLEServer_StopAdvertising();

    Serial.println("[TEST] Done. Halted — reset board to run again.");
    while (1);   // halt here; replace with deep sleep call in production
}
