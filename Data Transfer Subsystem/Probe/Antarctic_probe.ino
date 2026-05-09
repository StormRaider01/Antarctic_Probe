/**
 * antarctic_probe.ino
 * ===================
 * SS1 test sketch — generates synthetic probe records and transfers them
 * via ESP-NOW to the receiver dongle.
 *
 * In PRODUCTION, Kiyuran's state machine replaces this file. It:
 *   1. Calls ESPNOW_Init() after reed switch wakeup
 *   2. Reads ProbeRecord_t structs from FRAM (SS2's job)
 *   3. Calls ESPNOW_StartTransfer(records, count, session_start_ms)
 *   4. Calls ESPNOW_Deinit() then enters deep sleep
 *
 * This test sketch does the same steps but with RAM-generated fake data,
 * so SS1 can be validated independently of SS2 (FRAM) and SS4 (sensors).
 *
 * ── Expected Serial output (115200 baud) ─────────────────────────────────
 *   [TEST] Building 20 synthetic records...
 *   [ESPNOW] Initialised. Receiver peer registered.
 *   [ESPNOW] Sending header: 20 records, session_start=1000 ms
 *   [ESPNOW] ... (per-record ACK logs)
 *   [ESPNOW] Transfer complete. Sent: 20, Skipped: 0 / 20 total.
 *   [TEST] Done. Reset board to run again.
 */

#include "espnow_transfer.h"
#include "data_packet.h"

// ── Test parameters ────────────────────────────────────────────────────────
#define NUM_TEST_RECORDS     20

#define BASE_TIMESTAMP_MS    1000      // ms — session start (fake)
#define BASE_TEMP_C          2.50f
#define BASE_PRESSURE_DBAR   10.00f
#define BASE_EXCITATION      512.0f    // raw ADC counts (0–4095 for 12-bit)
#define BASE_FLUORESCENCE    128.0f

#define DELTA_TIMESTAMP_MS   5000      // 5 s between records
#define DELTA_TEMP_C         0.05f
#define DELTA_PRESSURE_DBAR  1.00f
#define DELTA_EXCITATION     0.0f      // hold constant for test
#define DELTA_FLUORESCENCE   2.5f      // slight rise with depth

// ── Record storage ─────────────────────────────────────────────────────────
static ProbeRecord_t test_records[NUM_TEST_RECORDS];

/* =============================================================================
    This function is purely for testing, needs to be changed!!!
    A similar function must replace it, one that creates records after reading in
    lines from the FRAM storage on the probe
    =============================================================================
*/
static void build_test_records(void) {
    Serial.printf("[TEST] Building %d synthetic records...\n", NUM_TEST_RECORDS);

    uint32_t ts         = BASE_TIMESTAMP_MS;
    float    temp       = BASE_TEMP_C;
    float    pressure   = BASE_PRESSURE_DBAR;
    float    excitation = BASE_EXCITATION;
    float    fluoro     = BASE_FLUORESCENCE;

    for (uint32_t i = 0; i < NUM_TEST_RECORDS; i++) {
        // ms_since_start = ts - BASE_TIMESTAMP_MS (0 for first record)

        // build record comes from data_packet.h, it fills in the checksum for us
        test_records[i] = build_record(
            i,
            ts - BASE_TIMESTAMP_MS,
            temp,
            pressure,
            excitation,
            fluoro
        );

        ts         += DELTA_TIMESTAMP_MS;
        temp       += DELTA_TEMP_C;
        pressure   += DELTA_PRESSURE_DBAR;
        excitation += DELTA_EXCITATION;
        fluoro     += DELTA_FLUORESCENCE;
    }

    Serial.println("[TEST] Records built OK.");
}
// =============================================================================


void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n===== SS1 ESP-NOW Transfer Test =====");

    build_test_records();   // might change to compressData() in production, but this is fine for testing

    ESPNowStatus_t status = ESPNOW_Init();
    if (status != ESPNOW_OK) {
        Serial.printf("[TEST] ESPNOW_Init failed: %d\n", status);
        while (1);
    }

    status = ESPNOW_StartTransfer(test_records, NUM_TEST_RECORDS, BASE_TIMESTAMP_MS);
    if (status != ESPNOW_OK) {
        Serial.printf("[TEST] Transfer failed: %d\n", status);
    }

    ESPNOW_Deinit();
    Serial.println("[TEST] Done. Reset board to run again.");
}

void loop() {
    // Nothing — all logic in setup() for this test
}
