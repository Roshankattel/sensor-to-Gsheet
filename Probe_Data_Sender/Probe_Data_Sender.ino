#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <nvs_flash.h>

#include "Adafruit_SHT31.h"
Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Google Apps Script Web App URL
const char* serverName = ""; 

// WiFi provisioning variables
const char* ap_ssid = "ESP32_Hotspot";
const char* ap_password = "12345678";
Preferences preferences;

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

const String redirectUrl = "http://192.168.4.1/";

void handleNotFound() {
  server.sendHeader("Location", redirectUrl, true);
  server.send(302, "text/plain", "");
}

// Function to display a simple HTML page for WiFi input
const char* html_page = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <title>ESP32 WiFi Setup</title>
  </head>
  <body>
    <h1>WiFi Configuration</h1>
    <form action="/configure" method="POST">
      <label>SSID:</label><br>
      <input type="text" name="ssid"><br>
      <label>Password:</label><br>
      <input type="password" name="password"><br><br>
      <input type="submit" value="Submit">
    </form>
  </body>
</html>
)rawliteral";


// Function to start access point and provisioning page
void startAccessPoint() {
  Serial.println("Starting AP...");
  WiFi.softAP(ap_ssid, ap_password);

  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  delay(100);

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // Handle GET request for the root page
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", html_page);
  });

  // Handle POST request for form submission
  server.on("/configure", HTTP_POST, []() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
      String ssid = server.arg("ssid");
      String password = server.arg("password");

      // Log credentials to the Serial Monitor
      Serial.println("Received SSID: " + ssid);
      Serial.println("Received Password: " + password);

      // Save WiFi credentials
      preferences.putString("ssid", ssid);
      preferences.putString("password", password);

      // Respond to the client
      server.send(200, "text/plain", "WiFi credentials received. Restarting...");
      delay(2000);
      ESP.restart();  // Restart the device
    } else {
      server.send(400, "text/plain", "Missing SSID or Password.");
    }
  });
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("AP and server started.");
  while (1) {
    dnsServer.processNextRequest();
    server.handleClient();
    // if (millis() - timer >= WEB_TIMEOUT)
    // {
    //     showSetupPage();
    //     esp_deep_sleep_start();
    // }
    delay(1);
  }
}

void connectToWiFi() {
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");

  if (ssid != "" && password != "") {
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);

    WiFi.begin(ssid.c_str(), password.c_str());
    int attempt = 0;

    while (WiFi.status() != WL_CONNECTED && attempt < 20) {
      delay(500);
      Serial.print(".");
      attempt++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to WiFi!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nWiFi connection failed. Starting Access Point...");
      startAccessPoint();
    }
  } else {
    Serial.println("No WiFi credentials found. Starting Access Point...");
    startAccessPoint();
  }
}


bool uploadDataToCloud() {
  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "application/json");

  float temperature = 1.1;
  float humidity = 2.2;
  // float temperature = sht31.readTemperature();
  // float humidity = sht31.readHumidity();

  if (!isnan(temperature) && !isnan(humidity)) {
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.print(" °C | Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  } else {
    Serial.println("Failed to read SHT32 sensor data!");
    return false;
  }

  // Prepare JSON payload with MAC, sensor data, and  timestamp;
  String jsonData = "{\"method\":\"append\",\"mac\":\"" + WiFi.macAddress() + "\",\"temperature\":" + String(temperature) + ",\"humidity\":" + String(humidity) + ",\"timestamp\":\"" + String(millis()) + "\"}";

  // Send HTTP POST request
  int httpResponseCode = http.POST(jsonData);

  if (httpResponseCode > 0) {
    String response = http.getString();
    // Serial.println(httpResponseCode);
    // Serial.println(response);
  } else {
    Serial.println("Error on sending POST: " + String(httpResponseCode));
    return false;
  }
  http.end();  // Close connection
  return true;
}


void setup() {
  Serial.begin(115200);

  //To format perefences
  // nvs_flash_erase();
  // nvs_flash_init();

  // Set custom I2C pins for ESP32
  Wire.begin(16, 17);  // SDA on IO16, SCL on IO17

  // Initialize Preferences for saving WiFi credentials
  preferences.begin("wifi", false);

  // Initialize the SHT3x sensor
  // if (!sht31.begin(0x44)) {  // Default I2C address is 0x44
  //   Serial.println("Could not find SHT31 sensor. Check wiring!");
  //   while (1)
  //     ;
  // }

  // Connect to WiFi or start AP
  connectToWiFi();
}

void loop() {
  // Only read temperature and humidity if connected to WiFi
  if (WiFi.status() == WL_CONNECTED) {
    if (uploadDataToCloud())
      Serial.println("Values uploaded to cloud Sucessfully!");
    else
      Serial.println("Uploaded to cloud Failed!");

  } else {
    Serial.println("Wi-Fi not connected. Attempting to reconnect...");
    connectToWiFi();  // connect
  }

  delay(10000);  // Delay between readings
}
