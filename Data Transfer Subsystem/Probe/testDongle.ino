#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h> 

// ===========================================================================
// Probe MAC — set this to the probe ESP32's MAC address
static uint8_t PROBE_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
//static uint8_t PROBE_MAC[6] = {0x9C, 0x13, 0x9E, 0xCD, 0x0B, 0x84};       // Devon's Board IP

esp_now_peer_info_t peerInfo;       // To hold information on the peer


// ===========================================================================
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    Serial.print("[INFO] Send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    String incoming = "";
    for (int i = 0; i < len; i++) incoming += (char)data[i];
    Serial.print("[RECV] ");
    Serial.println(incoming);
}


// ===========================================================================
// Heartbeat timing
static uint32_t s_last_heartbeat_ms = 0;
#define HEARTBEAT_INTERVAL_MS  1000
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
void cmd_StopHeartBeat() {
    s_state = STATE_READY;
    Serial.println("Heartbeat Stopped. Ready to connect to probe.");
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
                Serial.println("Command Connect forwarded.");
            } else {
                Serial.println("Send failed!");
            }
        }

        if (Serial.available() > 0) {

            // Read incoming line
            String incoming = Serial.readStringUntil('\n');
            incoming.trim();        // removes \r if it was on string
            if (incoming.startsWith("[ACK]:CONNECT")) {
                s_state = STATE_CONNECTED;
                return;
            }
        }
    }
}


// ===========================================================================
void cmd_Prepare(String command) {
    // format: PREPARE, 2026-05-13, 12:15:00
    // Indexing check: 
    // P R E P A R E , _ <date>
    // 0 1 2 3 4 5 6 7 8 9...
    
    if (command.length() < 29) {
        Serial.println("Error: Command string too short!");
        return;
    }

    String date = command.substring(9, 19);
    String time = command.substring(21, 29);
    String msg = "[CMD]:PREPARE," + date + "," + time;

    // Create a static or temporary buffer to hold the data
    // ESP-NOW payload limit is 250 bytes
    uint8_t buffer[msg.length() + 1]; 
    memcpy(buffer, msg.c_str(), msg.length());

    esp_err_t result = esp_now_send(PROBE_MAC, buffer, msg.length());

    if (result == ESP_OK) {
        Serial.println("Command Prepare forwarded successfully.");
    } else {
        Serial.print("Error sending ESP-NOW: ");
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
}


// ===========================================================================
void loop() {
    // Heartbeat logic
    if (s_state == STATE_IDLE) {
        if (millis() - s_last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
            Serial.println("[HBT] Waiting for GUI connection...");
            s_last_heartbeat_ms = millis();
        }
    }

    // listen to serial monitor
    if (Serial.available() > 0) {

        // Read incoming line
        String incoming = Serial.readStringUntil('\n');
        incoming.trim();        // removes \r if it was on string

        // Filter for CMD
        // [=0 C1 M=2 D=3 ]=4 :=5 so the command is everything from 6 onwards
        if (incoming.startsWith("[CMD]:")) {
            String cmd = incoming.substring(6);

            // Decode command
            if (cmd == "STOPHEARTBEAT") {
                cmd_StopHeartBeat();
            } else if (cmd == "COMMAND") {
                cmd_Connect();
            } else if (cmd == "RETRIEVE") {
                cmd_Retrieve();
            } else if (cmd == "DISCONNECT") {
                cmd_Disconnect();
            } else {
                cmd_Prepare(cmd);      // only other command there is
            }
        }
    }

}