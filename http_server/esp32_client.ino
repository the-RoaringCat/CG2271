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

const char *ssid = "Galaxy S24 Ultra 29CA";
const char *password = "asdfghjkl";

String server_host;
int server_port;

WiFiClient connection;
int server_fd;
//std::atomic<bool> connected{false};
std::atomic<char> key{0};
std::atomic<bool> is_data_ready{false};

void input_thread(void *arg) {
    while (connected) {
        if (Serial.available() > 0) {
            key = (char)Serial.read();
            is_data_ready = true;
            Serial.print((char)key.load());
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    vTaskDelete(NULL);
}

void receive_thread(void *arg) {
    while (connected) {
        uint8_t data[MAX_DATA_SIZE - 1];
        uint8_t url;
        Recv(data, &url, server_fd);
        Serial.println("hit!!!!!");
    }

    vTaskDelete(NULL);
}

bool discover_server() {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    http.begin(client, "https://gist.githubusercontent.com/the-RoaringCat/9066d77d403a693d38fb0a46c94368cb/raw/server.txt");

    int code = http.GET();

    if (code != 200) {
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

void setup() {
    Serial.begin(115200);

    WiFi.begin(ssid, password);

    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    if (!discover_server()) {
        return;
    }

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

    if (xTaskCreate(input_thread, "input", 4096, NULL, 1, NULL) != pdPASS) {
        connected = false;
        connection.stop();
        Serial.println("Failed to create input task");
        return;
    }

    Serial.println("Server connected");


}

void loop() {
    static unsigned long last_time = 0;

    if (micros() - last_time >= 16667) { //60Hz
        last_time += 16667; //avoid drift

        if (is_data_ready && connected) {
            uint8_t data[MAX_DATA_SIZE - 1] = {0};
            data[0] = key;
            Serial.println("sending key...");
            Send(data, URL_CONTROLLER_INPUT, server_fd);
            is_data_ready = false;
        }
    }
    delay(1);
}