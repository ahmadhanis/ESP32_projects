#include <WiFi.h>
#include <WiFiManager.h>

// Set your WiFi SSID and password here
const char* ssid = "yourNetworkName";
const char* password = "yourNetworkPass";

void setup() {
  Serial.begin(115200);

  // Initialize the WiFiManager
  WiFiManager wifiManager;

  // Set the default SSID and password
  wifiManager.setAPName("ESP32-WiFiManager");
  wifiManager.setAPPassword("12345678");

  // Initialize the web server
  WiFiServer server(80);

  // Start the web server
  server.begin();

  // Check if there is a saved WiFi configuration
  if (wifiManager.autoConnect()) {
    Serial.println("Connected to WiFi!");
  } else {
    Serial.println("Failed to connect to WiFi!");

    // Setup the web configuration portal
    wifiManager.startWebServer();

    // Wait for user to configure the WiFi
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }

    Serial.println("Connected to WiFi!");
  }
}

void loop() {
  // Handle web requests
  WiFiClient client = server.available();

  if (client) {
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
      }
    }

    // Close the connection
    client.stop();
  }
}