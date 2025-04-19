// Include necessary libraries
#include <WiFiManager.h>               // For managing WiFi connections and configuration portal
#include <Wire.h>                      // For I2C communication
#include <Adafruit_GFX.h>             // OLED graphics support
#include <Adafruit_SSD1306.h>         // OLED screen driver
#include <SPI.h>                       // SPI protocol for RFID reader
#include <MFRC522.h>                   // RFID library
#include <HTTPClient.h>               // For HTTP POST/GET requests
#include <ArduinoJson.h>              // For parsing JSON responses

// OLED display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_SDA 4
#define I2C_SCL 15
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// RFID module configuration
#define SS_PIN 21
#define RST_PIN 22
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Buzzer pin
#define BUZZER_PIN 2

// Server to send attendance data
const char* serverName = "http://slumberjer.com/ipgktar/attendance.php";

// Global variables for device identity and location
String macID = "";
String macIDFormatted = "";
String locationName = "";

// Play buzzer tone with specific frequency and duration
void playTone(int freq, int duration) {
  ledcAttachPin(BUZZER_PIN, 0);
  ledcWriteTone(0, freq);
  delay(duration);
  ledcWriteTone(0, 0);
}

// Success sound
void toneSuccess() {
  playTone(1000, 100);
  delay(100);
  playTone(1500, 100);
}

// Error sound
void toneError() {
  playTone(500, 300);
}

// Failed attempt sound
void toneFailed() {
  playTone(700, 150);
  delay(150);
  playTone(600, 150);
}

// Trim string to fit screen width with ellipsis
String truncateToFit(String text, int maxWidth, int textSize = 1) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  if (w <= maxWidth) return text;
  while (w > maxWidth && text.length() > 0) {
    text.remove(text.length() - 1);
    display.getTextBounds(text + "...", 0, 0, &x1, &y1, &w, &h);
  }
  return text + "...";
}

// Display a centered line of text on OLED
void displayCenteredLine(String text, int y, int textSize = 1) {
  display.setTextSize(textSize);
  display.setTextColor(WHITE);
  int16_t x1, y1off;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1off, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.println(text);
}

// Display two centered lines
void displayCenteredText(const String& line1, const String& line2 = "") {
  display.clearDisplay();
  displayCenteredLine(line1, 10, 1);
  if (line2 != "") {
    displayCenteredLine(line2, 30, 1);
  }
  display.display();
}

// Display a large message (e.g., register prompt)
void displayLargeMessage(const String& msg1, const String& msg2) {
  display.clearDisplay();
  displayCenteredLine(msg1, 0, 1);
  displayCenteredLine(msg2, 20, 2);
  display.display();
}

// Show idle screen while waiting for card
void displayWaiting() {
  display.clearDisplay();
  String title = locationName != "" ? locationName : "Error";
  String footer = locationName != "" ? "Scan Card" : "Register Device";

  title = truncateToFit(title, SCREEN_WIDTH, 1);
  footer = truncateToFit(footer, SCREEN_WIDTH, 1);

  displayCenteredLine(title, 0, 1);
  display.setTextSize(1);
  displayCenteredLine(macIDFormatted, 22, 1);
  displayCenteredLine(footer, 50, 1);
  display.display();
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();  // Initialize RFID reader
  Serial.println("CARD INIT...");
  // Initialize OLED display
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    while (true); // Stop if OLED fails
  }
  Serial.println("OLED INIT...");

  // Get device MAC address and remove colons
  macID = WiFi.macAddress();
  macIDFormatted = macID;
  macIDFormatted.replace(":", "");
  Serial.println(macIDFormatted);
  // Attempt to connect to WiFi
  displayCenteredText("Connecting", "to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // try stored credentials

  // Wait up to 15s for connection
  unsigned long startAttempt = millis();
  const unsigned long wifiTimeout = 15000;
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < wifiTimeout) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WIFI INIT...");
  // If WiFi fails, start WiFiManager config portal
  if (WiFi.status() != WL_CONNECTED) {
    displayCenteredText("WiFi Setup Mode");
    delay(500);

    WiFiManager wm;
    wm.setTimeout(180); // 3-minute portal
    bool success = wm.autoConnect("AttendanceAP", "admin123");

    if (!success) {
      // Show instructions and restart
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi not set.");
      display.println("Connect to AP:");
      display.println("AttendanceAP");
      display.println("Visit: 192.168.4.1");
      display.display();
      delay(30000);
      ESP.restart();
    }
  }

  // Successfully connected
  displayCenteredText("WiFi Connected", WiFi.localIP().toString());
  delay(1000);

  // Request device location using MAC ID
  HTTPClient http;
  http.begin("http://slumberjer.com/ipgktar/get_device_location.php?macid=" + macIDFormatted);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      locationName = doc["location"].as<String>();
    }
  }
  http.end();
}

void loop() {
  displayWaiting();

  // Wait for new card scan
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  // Read UID and format as uppercase hex string
  String cardid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    cardid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    cardid += String(mfrc522.uid.uidByte[i], HEX);
  }
  cardid.toUpperCase();

  displayCenteredText("Card Detected", cardid);
  Serial.println(cardid);
  sendToServer(cardid);  // send data to PHP server
  mfrc522.PICC_HaltA();  // halt communication
}

void sendToServer(String cardid) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Format data to be sent
    String postData = "cardid=" + cardid + "&device_id=" + macIDFormatted;
    int code = http.POST(postData);
    String payload = http.getString();

    if (code == 200) {
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, payload);

      if (err) {
        displayCenteredText("Server JSON", "Error");
        toneError();
        return;
      }

      // Handle status returned by server
      String name = doc["name"] | "Unknown";
      String status = doc["status"];

      if (status == "checkin") {
        displayCenteredText("Checked in", name);
        toneSuccess();
      } else if (status == "checkout") {
        displayCenteredText("Checked out", name);
        toneSuccess();
      } else if (status == "already") {
        displayCenteredText("Already today", name);
        toneFailed();
      } else if (status == "error") {
        displayLargeMessage("Register card", cardid);
        toneError();
        delay(10000);
      } else {
        displayCenteredText("Unknown", "response");
        toneError();
      }

    } else {
      displayCenteredText("HTTP error", String(code));
      toneError();
    }

    http.end();
  } else {
    displayCenteredText("WiFi error", "Disconnected");
    toneError();
  }

  delay(2000);  // short pause before next scan
}
