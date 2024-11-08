#include <Preferences.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <esp_eap_client.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "ESPAsyncWebServer.h"

#define SDA 12
#define SCL 13

Preferences preferences;

DNSServer dnsServer;
AsyncWebServer server(80);

String networkName;
String username;
String password;

String page;
String allnetworks;

int oldCount = -1;
int newCount = -1;
int oldVal, newVal, oldRem, newRem;

bool values_received = false;

const char *apiUrl = "https://graph.instagram.com/17841453413880469?fields=followers_count&access_token=IGQWRNSU1VU2otRTNaVXNYUm9oTUtwcUdQZAml1R3g3NmtBak5MNXR0bEVLQ3NFdFBORHM1SjVNTmprcDAycm5IWFVtemFXenlyYk02eDhrYjRTa0ZA1S2NOU1pUSDhYS2VCYnZANQkQ4YWlCSmFSSmlMTndacWZA4MzAZD";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smiirl Connect</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            margin: 0;
            padding: 0;
            min-height: 100vh;
            background: linear-gradient(135deg, #f09433 0%, #e6683c 25%, #dc2743 50%, #cc2366 75%, #bc1888 100%);
            color: #fff;
            text-align: center;
        }

        h1 {
            font-size: 32px;
            margin-top: 40px;
            margin-bottom: 30px;
            text-shadow: 2px 2px 5px rgba(0, 0, 0, 0.4);
            color: #ffffff;
        }

        .dc label,
        .ic label {
            font-size: 16px;
            margin-bottom: 5px;
            display: block;
            color: #f5f5f5;
        }

        .dpd,
        .ic input {
            width: 250px;
            font-size: 16px;
            margin-bottom: 15px;
            padding: 8px;
            border: 1px solid #f5f5f5;
            border-radius: 8px;
            background-color: #ffffff;
            color: #333;
            box-shadow: 0px 3px 6px rgba(0, 0, 0, 0.2);
            transition: box-shadow 0.2s ease;
        }

        .dpd {
            width: 270px;
        }

        .dpd:focus,
        .ic input:focus {
            outline: none;
            box-shadow: 0px 4px 8px rgba(0, 0, 0, 0.4);
        }

        .ins {
            font-size: 18px;
            margin: 20px 0 15px;
            color: #f5f5f5;
            max-width: 300px;
            line-height: 1.5;
        }

        .sc {
            background-color: #ffffff;
            border-radius: 12px;
            padding: 10px 15px;
            margin: 0px 100px;
            box-shadow: 0px 4px 8px rgba(0, 0, 0, 0.3);
            margin-bottom: 20px;
        }

        .sb {
            padding: 12px 24px;
            font-size: 16px;
            cursor: pointer;
            background: linear-gradient(135deg, #bc1888, #dc2743);
            color: #ffffff;
            font-weight: 700;
            border: none;
            border-radius: 8px;
            box-shadow: 0px 3px 6px rgba(0, 0, 0, 0.3);
            transition: background 0.3s ease, transform 0.2s ease;
        }

        .sb:hover {
            background: linear-gradient(135deg, #f09433, #e6683c);
            transform: scale(1.05);
        }
    </style>
</head>

<body>
    <h1>Smiirl Connect</h1>
    <form id="smiirlconnect" action="/get">
        <div class="dc">
            <label for="network">Choose Wi-Fi Network</label>
            <select id="network" name="network" class="dpd">
                <option value="" selected>Select option</option>)rawliteral";

const char footer_html[] PROGMEM = R"rawliteral(</select>
        </div>
        <div class="ic">
            <label for="username">Username</label>
            <input type="text" id="username" name="username" placeholder="Enter username if applicable">
        </div>
        <div class="ic">
            <label for="password">Password</label>
            <input type="text" id="password" name="password" placeholder="Enter password if applicable">
        </div></br>
        <div class="sc">
            <button type="submit" class="sb">SAVE</button>
        </div>
    </form>
</body>
</html>)rawliteral";

const int deviceAddresses[] = { 0x9, 0xA, 0xB, 0xC, 0xD }; // look up your own addresses using an I2C Scanning program

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request) {
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) {
    page = index_html + allnetworks + footer_html;
    request->send_P(200, "text/html", page.c_str());
    Serial.println("Client Connected");
  }
};

void getSavedPrefs() {
  if (!preferences.begin("smiirl", false)) {
    Serial.println("Failed to initialize preferences");
    return;
  }
  if (preferences.isKey("network")) {
    networkName = preferences.getString("network");
    Serial.print("Network found: ");
    Serial.println(networkName);
  } else {
    Serial.println("Network not found in preferences.");
    networkName = "";
  }
  if (preferences.isKey("password")) {
    password = preferences.getString("password");
    Serial.print("Password found: ");
    Serial.println(password);
  } else {
    Serial.println("Password not found in preferences.");
    password = "";
  }
  if (preferences.isKey("username")) {
    username = preferences.getString("username");
    Serial.print("Username found: ");
    Serial.println(username);
  } else {
    Serial.println("Username not found in preferences.");
    username = "";
  }
  preferences.end();
}

void attemptConnection() {
  if (networkName == "") {
    Serial.println("No saved network.");
    return;
  }

  int numNetworks = WiFi.scanNetworks();
  bool networkFound = false;
  int networkType = 0;  // 0 for nothing, 1 for open, 2 for WPA2-PSK and 3 for WPA2-Enterprise

  for (int i = 0; i < numNetworks; i++) {
    String ssid = WiFi.SSID(i);
    int encryptionType = WiFi.encryptionType(i);
    if (ssid == networkName) {
      networkFound = true;
      if (encryptionType == WIFI_AUTH_OPEN) {
        Serial.println("1");
        networkType = 1;
      } else if (encryptionType == WIFI_AUTH_WPA2_ENTERPRISE) {
        networkType = 3;
        Serial.println("3");
      } else {
        networkType = 2;
        Serial.println("2");
      }
      break;
    }
  }

  if (!networkFound) {
    Serial.println("Saved network not found in available networks.");
    return;
  }

  if (networkType == 1) {
    // Open WiFi
    Serial.print("Connecting to open WiFi: ");
    Serial.println(networkName);
    WiFi.disconnect(true);  // Disconnect if already connected
    WiFi.begin(networkName.c_str());
  } else if (networkType == 2) {
    // WPA2-PSK
    Serial.print("Connecting to WPA2-PSK WiFi: ");
    Serial.println(networkName);
    WiFi.disconnect(true);  // Disconnect if already connected
    WiFi.begin(networkName.c_str(), password.c_str());
  } else {
    // WPA2 Enterprise
    Serial.print("Connecting to WPA2 Enterprise WiFi: ");
    Serial.println(networkName);
    WiFi.disconnect(true);  // Disconnect if already connected
    WiFi.begin(networkName.c_str());
    esp_eap_client_set_identity((uint8_t *)username.c_str(), username.length());
    esp_eap_client_set_username((uint8_t *)username.c_str(), username.length());
    esp_eap_client_set_password((uint8_t *)password.c_str(), password.length());
    esp_wifi_sta_enterprise_enable();
    WiFi.begin();
  }

  int retries = 5;

  while (WiFi.status() != WL_CONNECTED && retries-- > 0) {
    delay(2000);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    setBlanks();
  } else {
    Serial.println("\nFailed to connect to WiFi.");
  }
}

String getWiFiNetworks() {
  String options;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    options += "<option value=\"" + String(WiFi.SSID(i)) + "\">" + WiFi.SSID(i) + "</option>";
  }
  return options;
}

void setServerDetails() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    page = index_html + allnetworks + footer_html;
    request->send_P(200, "text/html", page.c_str());
    Serial.println("Client Connected");
  });

  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("network")) {
      networkName = request->getParam("network")->value();
      Serial.println(networkName);
    }
    if (request->hasParam("username")) {
      username = request->getParam("username")->value();
      Serial.println(username);
    }
    if (request->hasParam("password")) {
      password = request->getParam("password")->value();
      Serial.println(password);
    }
    if (!preferences.begin("smiirl", false)) {
      Serial.println("Failed to initialize preferences");
    } else {
      preferences.putString("network", networkName);
      preferences.putString("username", username);
      preferences.putString("password", password);
    }
    values_received = true;
    request->send(200, "text/html", "The values entered by you have been successfully sent to the device. Closing access point and attempting connection...");
  });
}

void startServer() {
  WiFi.mode(WIFI_STA);
  allnetworks = getWiFiNetworks();
  delay(1000);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Smiirl Connect");
  setServerDetails();
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  server.begin();
  waitForDetails(1);
  dnsServer.stop();
  server.end();
  WiFi.mode(WIFI_STA);
}

void waitForDetails(int tries) {
  if (!values_received && tries <= 9) {  //Give user 45 seconds to connect and set new values
    dnsServer.processNextRequest();
    delay(5000);
    waitForDetails(tries + 1);
  }
}

void setLines() {
  Serial.println("Lines");
  for (int i = 4; i >= 0; i--) {
    Wire.beginTransmission(deviceAddresses[i]);
    Wire.write(0x02);
    Wire.write(0xB);
    Wire.write(0x1);
    Wire.write(0x0);
    Wire.write(0x1);
    Wire.endTransmission();
    delay(4000);
  }

}

void setBlanks() {
  Serial.println("Blanks");
  for (int i = 0; i < 5; i++) {
    Wire.beginTransmission(deviceAddresses[i]);
    Wire.write(0x02);
    Wire.write(0xA);
    Wire.write(0x0);
    Wire.write(0x0);
    Wire.write(0x1);
    Wire.endTransmission();
    delay(3000);
  }
}

void getNewCounts() {
  HTTPClient http;
  http.begin(apiUrl);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String payload = http.getString();
    Serial.println("Response: " + payload);
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      newCount = doc["followers_count"];
      Serial.print("Followers Count: ");
      Serial.println(newCount);
    } else {
      Serial.print("JSON Deserialization Error: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void updateCounter() {
  if (oldCount != newCount) {
    oldVal = oldCount;
    newVal = newCount;
    for (int i = 0; i < 5; i++) {
      oldRem = oldVal % 10;
      newRem = newVal % 10;
      if (oldRem != newRem) {
        setFlipper(i, newRem);
        delay(3500);
      }
      oldVal /= 10;
      newVal /= 10;
    }
  }
  oldCount=newCount;
}

void setFlipper(int pos, int val) {
  Wire.beginTransmission(deviceAddresses[pos]);
  Wire.write(0x02);
  Wire.write(val);
  Wire.write(0x1);
  Wire.write(0x0);
  Wire.write(0x1);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);  // For debugging
  Wire.begin(SDA, SCL);  // Initialize I2C communication
  delay(5000);
  getSavedPrefs();  // Get saved network details
  setLines();
  if (networkName != "")  // If there is a saved network
    attemptConnection();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    getNewCounts();
    updateCounter();
    delay(10000);
  } else {
    setLines();
    attemptConnection();  // Try again with previous details
    if (WiFi.status() != WL_CONNECTED) {
      startServer();  // Start server and AP to set new creds
      delay(1000);
      attemptConnection();
      if (WiFi.status() != WL_CONNECTED) {  // If still not connected after attempting through portal
        esp_restart();
      }
    }
  }
}
