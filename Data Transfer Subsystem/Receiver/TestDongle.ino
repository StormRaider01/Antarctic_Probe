#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h> 

// ===========================================================================
// Probe MAC — set this to the probe ESP32's MAC address
//static uint8_t PROBE_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t PROBE_MAC[6] = {0x9C, 0x13, 0x9E, 0xCD, 0x0B, 0x84};       // Devon's Board IP

esp_now_peer_info_t peerInfo;       // To hold information on the peer


// ===========================================================================
// Heartbeat timing
static uint32_t s_last_heartbeat_ms = 0;
#define HEARTBEAT_INTERVAL_MS  5000
unsigned long lastAttemptTime = 0;
const unsigned long attemptInterval = 2000; // 2 seconds between connection attempt


// ===========================================================================
// State machine
typedef enum {
    STATE_IDLE      = 0,   // heartbeat running
    STATE_READY     = 1,   // heartbeat stopped, ESP-NOW not yet up
    STATE_CONNECTED = 2,   // ESP-NOW up, ready to receive data
} DongleState_t;

static DongleState_t s_state = STATE_IDLE;


// ===========================================================================
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    Serial.print("[INFO] Send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}


// ===========================================================================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    String incoming = "";
    for (int i = 0; i < len; i++) incoming += (char)data[i];

    char buf[len + 1];
    memcpy(buf, data, len);
    buf[len] = '\0';
    Serial.println(buf);

    // Print
    Serial.println(incoming);
    
    // Check for [ACK]
    if (incoming == "[ACK]:CONNECT") {
        s_state = STATE_CONNECTED;
    }
}

// ===========================================================================
void cmd_StopHeartBeat() {
    s_state = STATE_READY;
    Serial.println("Heartbeat Stopped. Ready to connect to probe.");
    digitalWrite(15, LOW);
}


// ===========================================================================
void cmd_Connect() {
    while (s_state != STATE_CONNECTED) {
        unsigned long currentMillis = millis();

        if (currentMillis - lastAttemptTime >= attemptInterval) {
            lastAttemptTime = currentMillis;

            Serial.println("Attempting to connect to probe...");
            static const char* msg = "[CMD]:CONNECT"; 
    
            // strlen(msg) is used for char arrays instead of .length()
            esp_err_t result = esp_now_send(PROBE_MAC, (uint8_t*) msg, strlen(msg));

            if (result == ESP_OK) {
                Serial.println("[INFO]: Command Connect forwarded.");
            } else {
                Serial.println("[ERROR]: Send failed!");
            }
        }
    }
}


// ===========================================================================
void cmd_Prepare(String command) {
    // format: [CMD]:PREPARE, 2026-05-13, 12:15:00
    // Indexing check: 
    // [ C M D ] : P R E P A R E , 2 0 2 6 - 0 5 - 1 3 , 1 2 : 1 5 : 0 0
    // 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2
    //                     10                  20                  30
    
    if (command.length() < 30) {
        Serial.println("Error: Command string too short!");
        return;
    }

    String date = command.substring(14, 24);
    String time = command.substring(25);
    String msg = "[CMD]:PREPARE," + date + "," + time;

    // For debugging
    Serial.println("[DEBUG]: The date received is "+ date);
    Serial.println("[DEBUG]: The time received is " + time);

    // Create a static or temporary buffer to hold the data
    // ESP-NOW payload limit is 250 bytes
    uint8_t buffer[msg.length() + 1]; 
    memcpy(buffer, msg.c_str(), msg.length());

    esp_err_t result = esp_now_send(PROBE_MAC, buffer, msg.length());

    if (result == ESP_OK) {
        Serial.println("[INFO]: 👍 Command Prepare forwarded successfully.");
    } else {
        Serial.print("[IGNORE]: Error sending ESP-NOW: ");
        Serial.println(result);
    }
}


// ===========================================================================
void cmd_Retrieve() {
    static const char* msg = "[CMD]:RETRIEVE"; 
    
    // strlen(msg) is used for char arrays instead of .length()
    esp_err_t result = esp_now_send(PROBE_MAC, (uint8_t*) msg, strlen(msg));

    if (result == ESP_OK) {
        Serial.println("Command Retrieve forwarded.");
    } else {
        Serial.println("Send failed!");
    }
}


// ===========================================================================
void cmd_Disconnect() {
    static const char* msg = "[CMD]:DISCONNECT"; 
    
    // strlen(msg) is used for char arrays instead of .length()
    esp_err_t result = esp_now_send(PROBE_MAC, (uint8_t*) msg, strlen(msg));

    if (result == ESP_OK) {
        Serial.println("Command Disconnect forwarded.");
    } else {
        Serial.println("Send failed!");
    }
}


// ===========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n===== SS1 Receiver Dongle =====");

    WiFi.mode(WIFI_STA);
    Serial.print("[INFO] Dongle MAC: ");
    Serial.println(WiFi.macAddress());

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] ESP NOW initialization failed.");
        return;
    }

    // Register callbacks
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);

    // Register peer (the dongle)
    memcpy(peerInfo.peer_addr, PROBE_MAC, 6);
    peerInfo.channel = 0; // use current channel
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERROR] Failed to add peer.");
        return;
    }

    // First heartbeat fires immediately so Python doesn't have to wait 1 s
    Serial.println("[INFO] Ready. Waiting for probe...");
    s_last_heartbeat_ms = millis();

    // For visual confirmation
    pinMode(15, OUTPUT);
}


// ===========================================================================
void loop() {

    // Heartbeat logic (if needed)
    if (s_state == STATE_IDLE) {
        // For visual confirmation
        digitalWrite(15, HIGH);

        if (millis() - s_last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
            Serial.println("[HBT] Waiting for GUI connection...");
            s_last_heartbeat_ms = millis();
        }
    }
    
    // Only read if data is actually available
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');

        if (cmd == "[CMD]:STOPHEARTBEAT") {
            cmd_StopHeartBeat();
        } else if (cmd == "[CMD]:CONNECT") {        // <-- also fixed typo: was "COMMAND"
            cmd_Connect();
        } else if (cmd == "[CMD]:RETRIEVE") {
            cmd_Retrieve();
        } else if (cmd == "[CMD]:DISCONNECT") {
            cmd_Disconnect();
        } else if (cmd.startsWith("[CMD]:PREPARE,")) {
            cmd_Prepare(cmd);
        }
    }
}