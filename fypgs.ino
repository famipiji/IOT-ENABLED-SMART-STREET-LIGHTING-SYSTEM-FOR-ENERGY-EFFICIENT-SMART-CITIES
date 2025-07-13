#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_INA219.h>
#include <ArduinoJson.h>

// === Hardware Pins ===
const int photoPin = 34;
const int pirPin = 4;
const int ledPin = 13;

// === PWM Setup ===
const int pwmFreq = 5000;
const int pwmResolution = 8;
const int dimBrightness = 100;
const int fullBrightness = 255;

// === Ambient Light Hysteresis ===
const int lightThresholdLow = 110;
const int lightThresholdHigh = 350;

bool isLowLight = false;
bool motionActive = false;
unsigned long motionStartTime = 0;

// === Rolling Average Filter ===
const int avgWindow = 20;
int lightBuffer[avgWindow];
int bufferIndex = 0;

// === WiFi Credentials ===
const char* ssid = "Sharief7 2.4G";
const char* password = "@Shxriep7";

// === Google Sheets ===
const String googleScriptUrl = "https://script.google.com/macros/s/AKfycbyP0-YSTKLdTgF2TgkYcLVTxOd43FvPfO74bvixgwYAnK6rdPYTOUA0s3P4f8siX16OIg/exec";

// === Telegram Bot ===
const String botToken = "7603445630:AAFPw-j9MLfrrAp8fOsKrxIzeTeAQ9KKHgI";
const String chatID = "285672416";

// === Fault Detection ===
bool faultReported = false;
unsigned long lastMotionDetected = millis();
unsigned long lastFaultCheck = 0;
const unsigned long faultInterval = 30000; // 30 seconds
unsigned long pirLowStart = 0;

// === Data Logging ===
unsigned long lastDataSent = 0;
const unsigned long dataInterval = 30000;

// === INA219 Current Sensor ===
Adafruit_INA219 ina219;

// === PIR Counter ===
int pirPreviousState = LOW;
int pirTriggerCount = 0;

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected.");
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(pirPin, INPUT);
  ledcAttach(ledPin, pwmFreq, pwmResolution);

  for (int i = 0; i < avgWindow; i++) {
    lightBuffer[i] = analogRead(photoPin);
  }

  connectToWiFi();

  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    while (1) { delay(10); }
  }

  Serial.println("System Initialized");
}

int getSmoothedLightValue() {
  lightBuffer[bufferIndex] = analogRead(photoPin);
  bufferIndex = (bufferIndex + 1) % avgWindow;

  long total = 0;
  for (int i = 0; i < avgWindow; i++) {
    total += lightBuffer[i];
  }
  return total / avgWindow;
}

void sendToGoogleSheets(String jsonData) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(googleScriptUrl);
    http.addHeader("Content-Type", "application/json");
    Serial.println("Sending to Google Sheets:");
    Serial.println(jsonData);
    int httpCode = http.POST(jsonData);
    Serial.print("Google Sheets Response: ");
    Serial.println(httpCode);
    if (httpCode > 0) {
      String response = http.getString();
      Serial.println("Response: " + response);
    } else {
      Serial.println("Failed to send data");
    }
    http.end();
  }
}

void sendToTelegram(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + botToken + "/sendMessage?chat_id=" + chatID + "&text=" + message;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.println("Telegram message sent");
    } else {
      Serial.println("Failed to send Telegram message");
    }
    http.end();
  }
}

void sendFaultToGoogleSheets(String message) {
  StaticJsonDocument<200> doc;
  doc["type"] = "fault";
  doc["message"] = message;
  String jsonData;
  serializeJson(doc, jsonData);
  sendToGoogleSheets(jsonData);
}

void sendDataToGoogleSheets(int lightValue, int pirState, int ledState) {
  float current_mA = ina219.getCurrent_mA();
  StaticJsonDocument<300> doc;
  doc["type"] = "data";
  doc["ldr"] = lightValue;
  doc["pir_state"] = pirState;
  doc["led_brightness"] = ledState;
  doc["current_ma"] = current_mA;
  String jsonData;
  serializeJson(doc, jsonData);
  sendToGoogleSheets(jsonData);
}

void checkForFaults(int lightValue, int pirState) {
  bool fault = false;
  String message = "";
  if (lightValue < 10 || lightValue > 4000) {
    fault = true;
    message += "LDR failure; ";
  }
  if (pirState == LOW) {
    if (pirLowStart == 0) pirLowStart = millis();
    if (millis() - pirLowStart > 60000) {
      fault = true;
      message += "PIR stuck LOW; ";
    }
  } else {
    pirLowStart = 0;
  }
  if (fault) {
    if (millis() - lastFaultCheck >= faultInterval) {
      sendFaultToGoogleSheets(message);
      sendToTelegram("FAULT DETECTED: " + message);
      lastFaultCheck = millis();
    }
    faultReported = true;
  } else {
    faultReported = false;
  }
}

void loop() {
  int lightValue = getSmoothedLightValue();
  int pirState = digitalRead(pirPin);

  if (pirPreviousState == LOW && pirState == HIGH) {
    pirTriggerCount++;
  }
  pirPreviousState = pirState;

  Serial.print("Smoothed LDR Value: ");
  Serial.print(lightValue);
  Serial.print(" | PIR: ");
  Serial.print(pirState);
  Serial.print(" | PIR Trigger Count: ");
  Serial.println(pirTriggerCount);

  if (lightValue < lightThresholdLow) {
    isLowLight = true;
  } else if (lightValue > lightThresholdHigh) {
    isLowLight = false;
  }

  int currentLedBrightness = 0;
  if (isLowLight) {
    if (pirState == HIGH) {
      Serial.println("Motion Detected! Full brightness for 5s");
      motionStartTime = millis();
      motionActive = true;
      lastMotionDetected = millis();
    }
    if (motionActive) {
      ledcWrite(ledPin, fullBrightness);
      currentLedBrightness = fullBrightness;
      if (millis() - motionStartTime > 5000) {
        motionActive = false;
      }
    } else {
      ledcWrite(ledPin, dimBrightness);
      currentLedBrightness = dimBrightness;
    }
  } else {
    ledcWrite(ledPin, 0);
    currentLedBrightness = 0;
    motionActive = false;
  }

  if (millis() - lastDataSent > dataInterval) {
    sendDataToGoogleSheets(lightValue, pirTriggerCount, currentLedBrightness);
    pirTriggerCount = 0;
    lastDataSent = millis();
  }

  checkForFaults(lightValue, pirState);

  delay(100);
}
