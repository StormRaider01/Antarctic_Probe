/**
 * ble_server.cpp
 * ==============
 * Full implementation of the BLE GATT server using NimBLE-Arduino (h2zero).
 * Install via Arduino IDE Library Manager: search "NimBLE-Arduino".
 *
 * GATT structure:
 *   Service UUID:  "6e400000-b5a3-f393-e0a9-e50e24dcca9e"  (custom)
 *   ├── METADATA  "6e400001-..."  READ        — probe info (record count, firmware ver.)
 *   ├── CONTROL   "6e400002-..."  WRITE       — laptop sends b'\x01' to start transfer
 *   ├── DATA      "6e400003-..."  NOTIFY      — probe streams SensorPacket_t structs
 *   ├── STATUS    "6e400004-..."  READ/NOTIFY — transfer progress / error state
 *   └── BAT_LVL   "6e400005-..."          READ        — standard battery level (optional)
 *
 * UUIDs match ble_client.py on the Python side — do not change without updating both.
 */

#include "ble_server.h"
#include <NimBLEDevice.h>
#include <Arduino.h>

// ── UUIDs ──────────────────────────────────────────────────────────────────
// Must match UUID_* constants in ble_client.py exactly.
#define UUID_SERVICE   "cdc06d50-bd74-4799-8f11-1cff285d4a41"
#define UUID_METADATA  "cdc06d51-bd74-4799-8f11-1cff285d4a41"
#define UUID_CONTROL   "cdc06d52-bd74-4799-8f11-1cff285d4a41"
#define UUID_DATA      "cdc06d53-bd74-4799-8f11-1cff285d4a41"
#define UUID_STATUS    "cdc06d54-bd74-4799-8f11-1cff285d4a41"
#define UUID_BAT_LVL   "cdc06d55-bd74-4799-8f11-1cff285d4a41"

// Device name advertised over BLE — must match PROBE_NAME in ble_client.py
#define DEVICE_NAME    "AntarcticProbe"

// How long to wait between DATA notifications (ms).
// BLE has a maximum throughput of ~20 packets/connection interval.
// 20ms is a reasonable default; lower = faster but more collisions.
// Tune this if transfer takes too long on the ship deck.
#define NOTIFY_INTERVAL_MS   20

// ── Global metadata — written by the .ino before BLEServer_Init() ─────────
ProbeMetadata_t g_probe_metadata = {
    .record_count     = 0,
    .session_start_ms = 0,
    .firmware_version = 1,
    .reserved         = {0, 0, 0}
};

// ── Module-private state ───────────────────────────────────────────────────
// These are not exposed in the header — callers go through the API functions.
static NimBLEServer*         s_ble_server    = nullptr;
static NimBLECharacteristic* s_char_metadata = nullptr;
static NimBLECharacteristic* s_char_control  = nullptr;
static NimBLECharacteristic* s_char_data     = nullptr;
static NimBLECharacteristic* s_char_status   = nullptr;
static NimBLECharacteristic* s_char_bat_lvl  = nullptr;  

static bool s_initialized      = false;  // True after BLEServer_Init()
static bool s_transfer_pending = false;  // Set true by CONTROL write callback

// ── Server callbacks (connection/disconnection events) ────────────────────
/**
 * NimBLE calls these automatically on connect/disconnect.
 * You can add power management hooks here if needed (e.g. suspend sensor
 * polling while a client is connected to save power during transfer).
 */
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* srv, NimBLEConnInfo& info) override {
        Serial.print("[BLE] Client connected: ");
        Serial.println(info.getAddress().toString().c_str());

        NimBLEDevice::stopAdvertising();
    }

    void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& info, int reason) override {
        Serial.print("[BLE] Client disconnected. Reason: ");
        Serial.println(reason);

        // Reset transfer flag so a reconnection can trigger a fresh transfer
        s_transfer_pending = false;

        // Resume advertising so the laptop can reconnect if the link dropped.
        // The .ino can call BLEServer_StopAdvertising() to suppress this.
        NimBLEDevice::startAdvertising();
    }
};

// ── CONTROL characteristic write callback ─────────────────────────────────
/**
 * Called by NimBLE when the laptop writes to the CONTROL characteristic.
 * Expected commands:
 *   b'\x01'  — begin data transfer
 *   (extend this if you need more control commands later, e.g. abort)
 */
class ControlCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
        std::string val = chr->getValue();
        if (val.empty()) return;

        uint8_t cmd = (uint8_t)val[0];
        Serial.print("[BLE] CONTROL command received: 0x");
        Serial.println(cmd, HEX);

        if (cmd == 0x01) {
            // Laptop is ready to receive data.
            // Set the flag — the .ino should call BLEServer_StartTransfer()
            // when it sees BLEServer_TransferRequested() return true.
            // OR: you could start the transfer inline here, but that blocks
            // the NimBLE callback thread which is not ideal.
            s_transfer_pending = true;
            Serial.println("[BLE] Transfer requested by client.");
        }
        // Add more command codes here as needed (e.g. 0x02 = abort, 0x03 = resend)
    }
};

// ── BLEServer_Init ─────────────────────────────────────────────────────────
BLEStatus_t BLEServer_Init(void) {
    Serial.println("[BLE] Initialising NimBLE stack...");

    // Initialise NimBLE with the device name that will appear during scanning
    NimBLEDevice::init(DEVICE_NAME);

    // Optional: set TX power. Higher = longer range, more current draw.
    // ESP32-C6 supports: -27, -24, -21, -18, -15, -12, -9, -6, -3, 0, 3, 6 dBm
    // 0 dBm is a reasonable default for ship-deck range (~10m).
    // NimBLEDevice::setPower(0);  // uncomment and adjust if needed

    // Create the server and attach connection callbacks
    s_ble_server = NimBLEDevice::createServer();
    s_ble_server->setCallbacks(new ServerCallbacks());

    // Create the custom GATT service
    NimBLEService* service = s_ble_server->createService(UUID_SERVICE);

    // ── METADATA characteristic (READ) ────────────────────────────────────
    // Laptop reads this once on connect to find out how many records to expect.
    s_char_metadata = service->createCharacteristic(
        UUID_METADATA,
        NIMBLE_PROPERTY::READ
    );
    // Set initial value from g_probe_metadata (updated by .ino before advertising)
    s_char_metadata->setValue((uint8_t*)&g_probe_metadata, sizeof(ProbeMetadata_t));

    // ── CONTROL characteristic (WRITE) ────────────────────────────────────
    // Laptop writes b'\x01' here to trigger data streaming.
    s_char_control = service->createCharacteristic(
        UUID_CONTROL,
        NIMBLE_PROPERTY::WRITE
    );
    s_char_control->setCallbacks(new ControlCallbacks());

    // ── DATA characteristic (NOTIFY) ──────────────────────────────────────
    // Probe sends SensorPacket_t structs here as BLE notifications.
    // NOTIFY means the probe pushes data; the laptop does not poll.
    s_char_data = service->createCharacteristic(
        UUID_DATA,
        NIMBLE_PROPERTY::NOTIFY
    );

    // ── STATUS characteristic (READ | NOTIFY) ─────────────────────────────
    // Probe updates this with transfer progress. Laptop can poll or subscribe.
    s_char_status = service->createCharacteristic(
        UUID_STATUS,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    uint8_t idle_status[4] = {0x00, 0x00, 0x00, 0x00};
    s_char_status->setValue(idle_status, 4);

    // ── BAT_LVL characteristic (READ | NOTIFY) ──────────────────────────────────────
    // Probe updates this with battery level. Laptop can poll or subscribe.
    s_char_bat_lvl = service->createCharacteristic(
        UUID_BAT_LVL,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    uint8_t idle_bat_lvl[4] = {0x00, 0x00, 0x00, 0x00};
    s_char_bat_lvl->setValue(idle_bat_lvl, 4);

    // Start the service (makes all characteristics accessible to clients)
    service->start();

    s_initialized = true;
    Serial.println("[BLE] GATT server ready.");
    return BLE_OK;
}

// ── BLEServer_StartAdvertising ─────────────────────────────────────────────
BLEStatus_t BLEServer_StartAdvertising(void) {
    if (!s_initialized) {
        Serial.println("[BLE] ERROR: Call BLEServer_Init() first.");
        return BLE_ERR_NOT_INIT;
    }

    // Update the METADATA characteristic value before advertising in case
    // g_probe_metadata was updated after init (e.g. after reading MRAM record count)
    s_char_metadata->setValue((uint8_t*)&g_probe_metadata, sizeof(ProbeMetadata_t));

    // Configure the advertising packet
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(UUID_SERVICE);   // include service UUID so laptop can filter by it

    NimBLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising started. Waiting for client...");
    return BLE_OK;
}

// ── BLEServer_StopAdvertising ──────────────────────────────────────────────
void BLEServer_StopAdvertising(void) {
    NimBLEDevice::stopAdvertising();
    Serial.println("[BLE] Advertising stopped.");
}

// ── BLEServer_IsConnected ──────────────────────────────────────────────────
bool BLEServer_IsConnected(void) {
    if (!s_initialized || s_ble_server == nullptr) return false;
    return (s_ble_server->getConnectedCount() > 0);
}

// ── BLEServer_TransferRequested ───────────────────────────────────────────
/**
 * Returns true if the laptop has sent the b'\x01' CONTROL command.
 * Not declared in the header (the .ino calls BLEServer_StartTransfer()
 * directly), but you can add it to ble_server.h if your teammate needs it.
 */
bool BLEServer_TransferRequested(void) {
    return s_transfer_pending;
}

// ── BLEServer_StartTransfer ────────────────────────────────────────────────
BLEStatus_t BLEServer_StartTransfer(const SensorPacket_t* packets, uint32_t count) {
    if (!s_initialized)          return BLE_ERR_NOT_INIT;
    if (!BLEServer_IsConnected()) return BLE_ERR_NOT_CONN;

    Serial.print("[BLE] Starting transfer of ");
    Serial.print(count);
    Serial.println(" packets...");

    // Clear the pending flag now that we're acting on it
    s_transfer_pending = false;

    for (uint32_t i = 0; i < count; i++) {
        // Send one packet as a BLE notification.
        // notify() returns true if the notification was sent successfully.
        bool ok = s_char_data->notify(
            (uint8_t*)&packets[i],
            sizeof(SensorPacket_t)
        );

        if (!ok) {
            Serial.print("[BLE] Notify failed at packet ");
            Serial.println(i);
            // Options: retry, skip, or abort.
            // For now we skip and continue — the Python side will detect
            // the gap via the sequence number jump.
            // TODO: discuss retry policy with team (depends on MRAM re-read cost)
        }

        // Update STATUS with packets sent so far (progress tracking)
        // Status format: [packets_sent_low, packets_sent_high, total_low, total_high]
        // (two uint16_t packed as 4 bytes — extend this if you need more detail)
        uint8_t status[4] = {
            (uint8_t)(i & 0xFF),
            (uint8_t)((i >> 8) & 0xFF),
            (uint8_t)(count & 0xFF),
            (uint8_t)((count >> 8) & 0xFF)
        };
        s_char_status->setValue(status, 4);
        // Optionally notify STATUS as well so the laptop gets live progress:
        // s_char_status->notify();

        // Throttle notifications to avoid overwhelming the BLE stack
        delay(NOTIFY_INTERVAL_MS);
    }

    // ── End-of-transfer sentinel ──────────────────────────────────────────
    // Send a zero-length notification to tell the Python side we're done.
    // This matches the `if len(raw) == 0: done.set()` check in ble_client.py.
    s_char_data->notify((uint8_t*)nullptr, 0);

    // Update STATUS to "complete"
    uint8_t done_status[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    s_char_status->setValue(done_status, 4);
    s_char_status->notify();

    Serial.println("[BLE] Transfer complete.");
    return BLE_OK;
}

// ── BLEServer_UpdateStatus ────────────────────────────────────────────────
void BLEServer_UpdateStatus(const uint8_t* status_bytes, uint8_t len) {
    if (!s_initialized || s_char_status == nullptr) return;
    // Clamp to 4 bytes (arbitrary limit — increase if you need more fields)
    uint8_t clamped = (len > 4) ? 4 : len;
    s_char_status->setValue(status_bytes, clamped);
    if (BLEServer_IsConnected()) {
        s_char_status->notify();
    }
}