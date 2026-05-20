/**
 * Demo.ino
 * ========
 * Antarctic Probe - Demo Mode Firmware
 *
 * Differences from production firmware:
 *  - No deep sleep; runs continuously.
 *  - ESP-NOW stays active at all times (DISCONNECT does not deinit it).
 *  - Reed switch replaced by a 30-second software timer triggered by PREPARE.
 *  - Logs exactly 2 ramp-samples (11 steps each = 22 records) then waits for RETRIEVE.
 *  - Pin 8 held HIGH throughout to keep the sensor-power MOSFET on.
 *  - LED PWM ramp moved to pin 6 (was pin 8 in Sensors.ino).
 *
 * Demo flow:
 *   Boot → advertise → GUI sends PREPARE → 30 s background timer →
 *   sensors activate → 2 ramp samples logged → wait → GUI sends RETRIEVE → data offloaded → reset
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_AS7341.h>
#include "fram_manager.h"
#include "espnow_transfer.h"
#include "data_packet.h"

// =============================================================================
// Pin Definitions
// =============================================================================
#define MOSFET_PIN       8   // Always HIGH — keeps sensor power MOSFET on
#define LED_PIN          6   // AS7341 excitation LED PWM ramp (moved from pin 8)
#define ONE_WIRE_BUS     17  // DS18B20 data line
#define PRESSURE_ADC_PIN 1   // Pressure sensor ADC input

// =============================================================================
// Demo State Machine
// =============================================================================
#define STATE_IDLE    0  // Advertising, waiting for CONNECT + PREPARE
#define STATE_ARMED   1  // PREPARE received, 30 s countdown running
#define STATE_LOGGING 2  // Actively taking sensor samples
#define STATE_DONE    3  // Logging complete, waiting for RETRIEVE

// =============================================================================
// Timing & Sample Count
// =============================================================================
#define ARMED_DELAY_MS  30000  // 30 seconds between PREPARE and logging start
#define NUM_SAMPLES     2      // 2 ramp cycles × 11 steps = 22 records total

// =============================================================================
// AS7341 Settings  (matched to Sensors.ino)
// =============================================================================
static const as7341_gain_t SPEC_GAIN  = AS7341_GAIN_256X;
static const uint8_t       SPEC_ATIME = 10;
static const uint16_t      SPEC_ASTEP = 500;
static const int           NUM_STEPS  = 10;    // steps 0..10 = 11 readings
static const int           MAX_PWM    = 255;
static const float         PWM_STEP   = (float)MAX_PWM / NUM_STEPS;

// =============================================================================
// Global State
// =============================================================================
static int      demo_state       = STATE_IDLE;
static uint32_t armed_start_ms   = 0;
static int      record_counter   = 1;
static uint32_t session_start_ms = 0;
static uint32_t session_date     = 20260520;  // Updated by PREPARE (YYYYMMDD)

// =============================================================================
// Sensors
// =============================================================================
static OneWire         oneWire(ONE_WIRE_BUS);
static DallasTemperature tempSensor(&oneWire);
static Adafruit_AS7341 as7341;

// =============================================================================
// Helpers
// =============================================================================

int get_battery_level() { return 70; }

static float spec_integration_time_ms() {
    return (SPEC_ATIME + 1.0f) * (SPEC_ASTEP + 1.0f) * 0.00278f;
}

static float read_depth_meters() {
    int   raw            = analogRead(PRESSURE_ADC_PIN);
    float voltageAtAdc   = raw * (3.3f / 4095.0f);
    float sensorVoltage  = voltageAtAdc * ((3.3f + 10.0f) / 10.0f);
    if (sensorVoltage < 0.5f) sensorVoltage = 0.5f;
    if (sensorVoltage > 4.5f) sensorVoltage = 4.5f;
    float pressure_MPa   = (sensorVoltage - 0.5f) * (1.2f / 4.0f);
    return pressure_MPa * 1000.0f * 0.102f;
}

// =============================================================================
// Sensor Sample  (one full 11-step LED ramp → 11 records written to FRAM)
// =============================================================================
static void run_sensor_sample() {
    uint16_t rawDark[12], rawLight[12];

    // Read temperature and depth once per ramp
    tempSensor.requestTemperatures();
    float temperature = tempSensor.getTempCByIndex(0);
    float depth       = read_depth_meters();

    // Dark reference (LED off)
    analogWrite(LED_PIN, 0);
    delay(50);
    if (!as7341.readAllChannels(rawDark)) {
        Serial.println("[SENSOR] Dark read failed — skipping sample.");
        return;
    }

    // Collapse 12 raw channels to the 10 usable ones (skip indices 4 & 5)
    uint16_t dark[10] = {
        rawDark[0],  rawDark[1],  rawDark[2],  rawDark[3],
        rawDark[6],  rawDark[7],  rawDark[8],  rawDark[9],
        rawDark[10], rawDark[11]
    };

    // LED ramp: step 0 (duty=0) up to step 10 (duty=255)
    for (int step = 0; step <= NUM_STEPS; step++) {
        int duty = (int)roundf(PWM_STEP * step);
        if (duty > MAX_PWM) duty = MAX_PWM;
        analogWrite(LED_PIN, duty);

        delay((int)spec_integration_time_ms() + 20);

        if (!as7341.readAllChannels(rawLight)) {
            Serial.printf("[SENSOR] Light read failed at step %d.\n", step);
            continue;
        }

        uint16_t light[10] = {
            rawLight[0],  rawLight[1],  rawLight[2],  rawLight[3],
            rawLight[6],  rawLight[7],  rawLight[8],  rawLight[9],
            rawLight[10], rawLight[11]
        };

        // Net = light - dark (clamp to 0)
        int net[10];
        for (int i = 0; i < 10; i++) {
            net[i] = (int)light[i] - (int)dark[i];
            if (net[i] < 0) net[i] = 0;
        }

        uint32_t t_ms = millis() - session_start_ms;

        // CSV: reading, time_ms, temp_c, depth_m, F1..F8, clear, NIR  (14 values)
        String csv = String(record_counter) + "," + String(t_ms)           + "," +
                     String(temperature, 2) + "," + String(depth, 3);
        for (int i = 0; i < 10; i++) csv += "," + String(net[i]);

        fram_write_record(csv);
    }

    analogWrite(LED_PIN, 0);
    record_counter++;
}

// =============================================================================
// Command Handler  (called from loop; ESP-NOW stays active after DISCONNECT)
// =============================================================================
static void handle_command(const char* cmd_buf) {
    String cmd = String(cmd_buf);
    Serial.printf("[CMD] Received: %s\n", cmd_buf);

    if (cmd == "[CMD]:CONNECT") {
        char resp[32];
        snprintf(resp, sizeof(resp), "[ACK]:CONNECT,BATT:%d", get_battery_level());
        ESPNOW_SendString(resp);

    } else if (cmd.startsWith("[CMD]:PREPARE")) {
        // Format: [CMD]:PREPARE,2026-05-20,12:15:00
        if (cmd.length() >= 29) {
            String dateStr = cmd.substring(14, 24);
            String timeStr = cmd.substring(25);
            String y = dateStr.substring(0, 4);
            String m = dateStr.substring(5, 7);
            String d = dateStr.substring(8, 10);
            session_date = (uint32_t)(y.toInt() * 10000 + m.toInt() * 100 + d.toInt());

            ESPNOW_SendString("[ACK]:PREPARE");
            Serial.printf("[DEMO] Mission date: %lu, time: %s\n", session_date, timeStr.c_str());
            Serial.println("[DEMO] 30-second countdown started. GUI may disconnect.");

            demo_state     = STATE_ARMED;
            armed_start_ms = millis();
        }

    } else if (cmd == "[CMD]:RETRIEVE") {
        ESPNOW_SendString("[ACK]:RETRIEVE");

        if (demo_state == STATE_DONE) {
            Serial.println("[DEMO] RETRIEVE: Starting offload...");
            ProbeRecord_t* records = (ProbeRecord_t*)malloc(sizeof(ProbeRecord_t) * 100);
            if (records != NULL) {
                int count = fram_get_records(records, 100);
                if (count > 0) {
                    ESPNOW_StartTransfer(records, count, session_start_ms, session_date);
                } else {
                    Serial.println("[DEMO] No records in memory.");
                }
                free(records);
            }
            // Reset for a potential second demo run
            fram_clear();
            record_counter   = 1;
            session_start_ms = millis();
            demo_state       = STATE_IDLE;
            Serial.println("[DEMO] Offload complete. Reset to IDLE — ready for next run.");
        } else {
            Serial.println("[CMD] RETRIEVE received but logging not complete yet.");
        }

    } else if (cmd == "[CMD]:DISCONNECT") {
        ESPNOW_SendString("[ACK]:DISCONNECT");
        // Intentionally NOT calling ESPNOW_Deinit() — radio stays on for demo.
        Serial.println("[CMD] DISCONNECT ACK sent. ESP-NOW remains active.");
    }
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- Antarctic Probe DEMO Mode ---");

    // Pin 8 always HIGH: enables the MOSFET that powers the sensor circuit
    pinMode(MOSFET_PIN, OUTPUT);
    digitalWrite(MOSFET_PIN, HIGH);

    // LED PWM pin (now pin 6, not pin 8)
    pinMode(LED_PIN, OUTPUT);
    analogWriteFrequency(LED_PIN, 5000);
    analogWrite(LED_PIN, 0);
    analogReadResolution(12);

    // DS18B20
    tempSensor.begin();
    if (tempSensor.getDeviceCount() == 0) {
        Serial.println("[SENSOR] Warning: no DS18B20 detected. Check wiring.");
    }

    // AS7341
    Wire.begin();
    if (!as7341.begin()) {
        Serial.println("[SENSOR] AS7341 not found. Halting.");
        while (true) delay(1000);
    }
    as7341.setGain(SPEC_GAIN);
    as7341.setATIME(SPEC_ATIME);
    as7341.setASTEP(SPEC_ASTEP);

    // FRAM
    fram_init();
    fram_clear();
    session_start_ms = millis();

    // ESP-NOW: init once, stays on forever
    if (ESPNOW_Init() != ESPNOW_OK) {
        Serial.println("[ERROR] ESP-NOW init failed. Halting.");
        while (true) delay(1000);
    }

    Serial.print("[DEMO] Probe MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.println("[DEMO] Advertising. Waiting for PREPARE command...");
}

// =============================================================================
// Loop
// =============================================================================
void loop() {
    // Always poll for incoming ESP-NOW commands
    char cmd_buf[64];
    if (ESPNOW_GetCommand(cmd_buf, sizeof(cmd_buf))) {
        handle_command(cmd_buf);
    }

    // STATE_ARMED: fire logging once 30 s have elapsed
    if (demo_state == STATE_ARMED) {
        if (millis() - armed_start_ms >= ARMED_DELAY_MS) {
            Serial.println("[DEMO] 30 s elapsed — activating sensors.");
            demo_state       = STATE_LOGGING;
            session_start_ms = millis();

            for (int i = 0; i < NUM_SAMPLES; i++) {
                Serial.printf("[DEMO] Sample %d / %d\n", i + 1, NUM_SAMPLES);
                run_sensor_sample();
            }

            analogWrite(LED_PIN, 0);
            demo_state = STATE_DONE;
            int total = (record_counter - 1) * (NUM_STEPS + 1);  // records in FRAM
            Serial.printf("[DEMO] Logging done. %d records stored. Waiting for RETRIEVE.\n", total);
        }
    }

    delay(10);
}
