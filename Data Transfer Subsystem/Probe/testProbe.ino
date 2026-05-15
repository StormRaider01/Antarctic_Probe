/**
 * This module purely tests whether ESP NOW works and
 * if the Dongle is able to forward the commands of 
 * the GUI here to the Probe.
 * 
 * Once a command is received, it is decoded and an
 * ACK is sent back as a message to confirm it was received.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>        

// ===========================================================================
static uint8_t DongleAddress[6] = {0x9C, 0x13, 0x9E, 0xCC, 0x35, 0x50};     // Kiyuran's board
esp_now_peer_info_t peerInfo;
String globalDate = "";
String globalTime = "";


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
void cmd_Connect() {
    static const char* msg = "[ACK]:CONNECT"; 
    
    // strlen(msg) is used for char arrays instead of .length()
    esp_err_t result = esp_now_send(DongleAddress, (uint8_t*) msg, strlen(msg));

    if (result == ESP_OK) {
        Serial.println("Acknowledge Connect forwarded.");
    } else {
        Serial.println("Send failed!");
    }
}


// ===========================================================================
void cmd_Disconnect() {
    esp_err_t result = esp_now_deinit();
    WiFi.mode(WIFI_OFF);           

    if (result == ESP_OK) {
        Serial.println("ESP NOW De-initialised Successfully.");
        Serial.println("Disconnect command received and acknowledged.");
    } else {
        Serial.print("[Error] De-initialising ESP NOW failed: ");
        Serial.println(result);
    }
}


// ===========================================================================
void cmd_Prepare(String command) {
    // Expected format: "PREPARE, 2026-05-13, 12:15:00"
    // Indices:         01234567890123456789012345678
    //                            ^         ^
    //                         Index 9    Index 21

    if (command.length() >= 29) {
        // Extract into global variables
        // .substring(start, end) - end is exclusive
        globalDate = command.substring(9, 19);  // "2026-05-13"
        globalTime = command.substring(21, 29); // "12:15:00"

        Serial.println("Data Stored Globally:");
        Serial.println("Date: " + globalDate);
        Serial.println("Time: " + globalTime);

        // Send acknowledgment back to the Sender
        String ackMsg = "[ACK]:PREPARE";
        
        esp_err_t result = esp_now_send(DongleAddress, (uint8_t*) ackMsg.c_str(), ackMsg.length());

        if (result == ESP_OK) {
            Serial.println("Acknowledge Prepare forwarded.");
        }
    } else {
        Serial.println("Error: Received malformed PREPARE command.");
    }
}


// ===========================================================================
void cmd_Retrieve() {
    static const char* msg = "[ACK]:RETRIEVE"; 
    
    // strlen(msg) is used for char arrays instead of .length()
    esp_err_t result = esp_now_send(DongleAddress, (uint8_t*) msg, strlen(msg));

    if (result == ESP_OK) {
        Serial.println("Acknowledge Retrieve forwarded.");
    } else {
        Serial.println("Send failed!");
    }
}


// ===========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n===== SS1 Probe =====");

    WiFi.mode(WIFI_STA);
    Serial.print("[INFO] Probe MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] ESP NOW initialization failed.");
        return;
    }

    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);

    memcpy(peerInfo.peer_addr, DongleAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERROR] Failed to add peer.");
        return;
    }

    Serial.println("==========================");  
    Serial.println("Ready to receive commands.");
}


// ===========================================================================
void loop() {
    if (Serial.available() > 0) {
        String incoming = Serial.readStringUntil('\n');
        incoming.trim();

        if (incoming.startsWith("[CMD]:")) {
            String cmd = incoming.substring(6);

            if (cmd == "CONNECT") {
                cmd_Connect();          
            } else if (cmd == "RETRIEVE") {
                cmd_Retrieve();
            } else if (cmd == "DISCONNECT") {
                cmd_Disconnect();
            } else          // PREPARE only other command there is
                cmd_Prepare(cmd);
            }
        }
    }
}
