/* UART-only milestone for the ESP32-S2 DevKitM-1 from CG2271 Lab 3.
 * Serial = USB-to-UART monitor; Serial1 = MCXC444 link.
 * No Wi-Fi, API or cloud code is included yet.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <errno.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define COMM_IMPLEMENTATION
#define MY_ESP
#include "common.h"

#include "uart_protocol.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S2)
#error "This wiring targets ESP32-S2. Select ESP32S2 Dev Module, or verify/revise pins for your actual board."
#endif

#define KEY_UP      0   //w
#define KEY_LEFT    1   //a
#define KEY_DOWN    2   //s
#define KEY_RIGHT   3   //d
#define KEY_NIGHT   4   //t
#define KEY_HORN    5
#define KEY_START   6

constexpr int MCX_RX_PIN = 2; /* ESP32 GPIO2 <- MCXC444 PTE22 TX */
constexpr int MCX_TX_PIN = 1; /* ESP32 GPIO1 -> MCXC444 PTE23 RX */
constexpr int HIT_BUTTON_PIN = 0; /* ESP32-S2 BOOT button, active LOW */
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t ACK_TIMEOUT_MS = 1000;

UartProtocol_Receiver receiver = {};
uint32_t nextCommandId = 1;
std::atomic<bool> waitingForAck{false};
uint32_t pendingCommandId = 0;
uint32_t commandSentAt = 0;
bool haveData = false;
uint32_t lastDataAt = 0;
bool linkWarningPrinted = false;
uint32_t lastHornCount = 0, lastBootCount = 0;
uint32_t lastDataSequence = 0;
bool lastRawButton = HIGH, stableButton = HIGH;
uint32_t buttonChangedAt = 0;

//wifi
const char *ssid = "Galaxy S24 Ultra 29CA";
const char *password = "asdfghjkl";
WiFiClient connection;

//server
String server_host;
int server_port;
int server_fd;

//server global
bool is_data_ready = false;
uint8_t controller_data[MAX_DATA_SIZE - 1] = {0};

// prepare mcxc input to server
// void input_thread(void *arg) {
//     while (connected) {
//         if (Serial.available() > 0) {
//             key = (char)Serial.read();
//             is_data_ready = true;
//             Serial.print((char)key.load());
//         }
//         vTaskDelay(pdMS_TO_TICKS(1));
//     }

//     vTaskDelete(NULL);
// }

// receive data from server
void receive_thread(void *arg) {
    while (connected) {
        uint8_t data[MAX_DATA_SIZE - 1];
        uint8_t url;
        Recv(data, &url, server_fd);
        Serial.println("hit!!!!!");
        sendObstacleHit();
    }

    vTaskDelete(NULL);
}

//used to find the server host and port
// bool discover_server() {
//     WiFiClientSecure client;
//     client.setInsecure();
//     HTTPClient http;

//     http.begin(client, "https://gist.githubusercontent.com/the-RoaringCat/9066d77d403a693d38fb0a46c94368cb/raw/server.txt");

//     int code = http.GET();

//     if (code != 200) {
//         http.end();
//         return false;
//     }

//     String response = http.getString();
//     http.end();

//     int separator = response.indexOf(' ');

//     if (separator < 0) return false;

//     server_host = response.substring(0, separator);
//     server_port = response.substring(separator + 1).toInt();
//     Serial.println(server_host);
//     Serial.println(server_port);

//     return server_port > 0;
// }
bool discover_server() {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    String url = "https://gist.githubusercontent.com/the-RoaringCat/9066d77d403a693d38fb0a46c94368cb/raw/server.txt";
    url += "?t=" + String(millis());

    http.begin(client, url);

    http.addHeader("Cache-Control", "no-cache, no-store, max-age=0");
    http.addHeader("Pragma", "no-cache");

    int code = http.GET();

    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    int separator = response.indexOf(' ');

    if (separator < 0) return false;

    server_host = response.substring(0, separator);
    server_port = response.substring(separator + 1).toInt();

    Serial.println(server_host);
    Serial.println(server_port);

    return server_port > 0;
}

/* Later, call this from the API/collision-handler path instead of BOOT.
 * This loop owns Serial1. A future callback in another task should enqueue
 * a hit request for this loop rather than writing Serial1 concurrently. */
void sendObstacleHit()
{
    if (waitingForAck) {
        Serial.println("Hit not sent: still waiting for the previous ACK.");
        return;
    }
    pendingCommandId = nextCommandId++;
    waitingForAck = true;
    commandSentAt = millis();
    char line[UART_LINK_LINE_SIZE];
    snprintf(line, sizeof(line), "CMD,BUMP,%lu\n", (unsigned long)pendingCommandId);
    Serial1.print(line);
    Serial.print("ESP -> MCX: ");
    Serial.print(line);
}

void handleMcxLine(const char *line)
{
    uint32_t v[6];
    if (UartProtocol_Parse(line, "DATA,", v, 6u)) {
        /* seq, raw X, raw Y, raw light, horn-event count, boot-event count */
        bool validX = v[1] <= 4095u || v[1] == 65535u;
        bool validY = v[2] <= 4095u || v[2] == 65535u;
        bool validLight = v[3] <= 4095u || v[3] == 65535u;
        if (!validX || !validY || !validLight) {
            Serial.println("Rejected DATA: ADC field out of range.");
            return;
        }
        Serial.print("MCX -> ESP: ");
        Serial.println(line);
        /* A reset restarts telemetry sequence at zero; counters start over.
         * First packet after ESP startup establishes a baseline, not old events. */
        if (v[0] >= lastDataSequence && haveData) {
            if (v[4] != lastHornCount) {
                Serial.println("EVENT: horn input detected on MCX.");
                controller_data[KEY_HORN] = 1;
            }
            if (v[5] != lastBootCount) {
                controller_data[KEY_START] = 1;
                Serial.println("EVENT: onboard boot input detected on MCX.");
            }
            
            lastDataSequence = v[0];
            lastHornCount = v[4];
            lastBootCount = v[5];

            //v[1] is x axis, 0 ->left
            if (v[1] < 1500) {
                controller_data[KEY_LEFT] = 1;
            } else if (v[1] > 2500) {
                controller_data[KEY_RIGHT] = 1;
            }

            //v[2] is y axis, 0 ->up
            if (v[2] < 1500) {
                controller_data[KEY_UP] = 1;
            } else if (v[2] > 2500) {
                controller_data[KEY_DOWN] = 1;
            }
            
            //v[3] is environment light, 0 -> bright

            if (v[3] > 1000) { //dark
                controller_data[KEY_NIGHT] = 1;
            }

            is_data_ready = true;
            lastDataAt = millis();
        }
        
        haveData = true;
        
        linkWarningPrinted = false;
        /* Future UI/API code can use v[1..5]; do not make network calls here. */
    } else if (UartProtocol_Parse(line, "ACK,BUMP,", v, 1u)) {
        Serial.print("MCX -> ESP: ");
        Serial.println(line);
        if (waitingForAck && v[0] == pendingCommandId) {
            waitingForAck = false;
            Serial.println("ACK received: MCX accepted the motor-pulse request.");
        } else {
            Serial.println("Late/unmatched ACK (not an ACK for the current request).");
        }
    } else {
        Serial.print("Rejected/unknown MCX line: ");
        Serial.println(line);
    }
}

void setup()
{
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    if (!discover_server()) return;
    
    if (!connection.connect(server_host.c_str(), server_port)) {
        Serial.println("Server connection failed");
        return;
    }

    connection.setNoDelay(true);  // TCP_NODELAY
    server_fd = connection.fd();  // Reuse your socket-based functions.
    connected = true;

    if (xTaskCreate(receive_thread, "receive", 4096, NULL, 1, NULL) != pdPASS) {
        connected = false;
        connection.stop();
        Serial.println("Failed to create receive task");
        return;
    }

    // if (xTaskCreate(input_thread, "input", 4096, NULL, 1, NULL) != pdPASS) {
    //     connected = false;
    //     connection.stop();
    //     Serial.println("Failed to create input task");
    //     return;
    // }

    Serial.println("Server connected");

    /* Order is baud, framing, RX pin, TX pin. Same remap as testuart.ino. */
    Serial1.begin(115200, SERIAL_8N1, MCX_RX_PIN, MCX_TX_PIN);
    pinMode(HIT_BUTTON_PIN, INPUT_PULLUP);
    lastRawButton = stableButton = digitalRead(HIT_BUTTON_PIN);
    buttonChangedAt = millis();
    lastDataAt = millis();
    Serial.println("ESP32-S2 UART bridge ready: 115200 8N1, RX=GPIO2 TX=GPIO1.");
    Serial.println("Press BOOT during normal running for a collision vibration pulse.");
    Serial.println("Or send h from Serial Monitor to test the same command.");
}

void loop()
{
    /* Drain incoming bytes without waiting for a full packet or timeout. */
    while (Serial1.available() > 0) {
        char c = (char)Serial1.read();
        if (UartProtocol_Feed(&receiver, c)) handleMcxLine(receiver.line);
    }

    if (is_data_ready) {
        Send(controller_data, URL_CONTROLLER_INPUT, server_fd);
        memset(controller_data, 0, sizeof(controller_data));
        is_data_ready = false;
    }


    // uint32_t now = millis();
    // bool raw = digitalRead(HIT_BUTTON_PIN);
    // if (raw != lastRawButton) {
    //     lastRawButton = raw;
    //     buttonChangedAt = now;
    // }
    // if (raw != stableButton && (uint32_t)(now - buttonChangedAt) >= DEBOUNCE_MS) {
    //     stableButton = raw;
    //     if (stableButton == LOW) sendObstacleHit(); /* once per debounced press */
    // }

    //send hit 
    // while (Serial.available() > 0) {
    //     char c = (char)Serial.read();
    //     if (c == 'h' || c == 'H') sendObstacleHit();
    // }

    uint32_t now = millis();
    if (waitingForAck && (uint32_t)(now - commandSentAt) >= ACK_TIMEOUT_MS) {
        waitingForAck = false;
        Serial.println("ACK TIMEOUT: check MCX running, TX/RX wiring and shared GND. No automatic retry.");
    }
    if (!linkWarningPrinted && (uint32_t)(now - lastDataAt) >= 2000u) {
        Serial.println("No recent MCX telemetry: check wiring, firmware and 115200 baud.");
        linkWarningPrinted = true;
    }
    delay(1); /* yield to the ESP32 runtime; no busy-wait for incoming UART */
}
