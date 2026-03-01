/*
 ╔══════════════════════════════════════════════════════════════╗
 ║         SmartSense ESP32 — Firebase Realtime Database        ║
 ║              Updated with Your Exact Pin Layout              ║
 ╠══════════════════════════════════════════════════════════════╣
 ║  YOUR PIN WIRING:                                            ║
 ║  ┌─────────────────────────────────────────────────────┐     ║
 ║  │  GPIO 4   → DHT22  (Temp & Humidity — Outdoor)      │     ║
 ║  │  GPIO 15  → DHT11  (Temp & Humidity — Indoor)       │     ║
 ║  │  GPIO 32  → MQ-135 (Air Quality / CO2)              │     ║
 ║  │  GPIO 33  → MQ-7   (Carbon Monoxide)                │     ║
 ║  │  GPIO 34  → MQ-3   (Alcohol) [INPUT ONLY]           │     ║
 ║  │  GPIO 35  → Sound Sensor     [INPUT ONLY]           │     ║
 ║  │  GPIO 36  → Soil Moisture    [INPUT ONLY]           │     ║
 ║  │  GPIO 39  → Rain Sensor      [INPUT ONLY]           │     ║
 ║  │  GPIO 5   → Relay (Fan/Device control)              │     ║
 ║  │  GPIO 19  → Buzzer                                  │     ║
 ║  │  GPIO 2   → Onboard LED (status indicator)          │     ║
 ║  └─────────────────────────────────────────────────────┘     ║
 ║                                                              ║
 ║  ⚠ NOTE: GPIO 34,35,36,39 are INPUT-ONLY on ESP32.           ║
 ║    They have NO internal pull-up. Do NOT use as OUTPUT.      ║
 ║    GPIO 39 (Rain): Add 10kΩ pull-up to 3.3V externally.     ║
 ║                                                              ║
 ║  LIBRARIES (install via Arduino Library Manager):            ║
 ║    1. Firebase ESP Client  → mobizt/Firebase ESP Client      ║
 ║    2. DHT sensor library   → Adafruit DHT                    ║
 ║    3. Adafruit Unified Sensor                                ║
 ║    4. Adafruit SSD1306                                       ║
 ║    5. Adafruit GFX Library                                   ║
 ╚══════════════════════════════════════════════════════════════╝
*/

#include <WiFi.h>
#include <FirebaseESP32.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// ════════════════════════════════════════════════════
//  FORWARD DECLARATIONS — fixes "not declared in scope"
// ════════════════════════════════════════════════════
void connectWiFi();
void setupFirebase();
void readAllSensors();
void uploadSensorsToFirebase();
void uploadDeviceStates();
void readDeviceCommandsFromFirebase();
void runAutomation(unsigned long now);
void syncThresholdsFromFirebase();
void pushAlert(String severity, String title, String message, String icon, String zone);
void setRelay(bool on);
void setBuzzer(bool on);
void updateOLED();
void oledMsg(const char* l1, const char* l2);
void oledSplash();
void beepAlert(int times);
void emergencyBeep(int rounds);
void blinkLED(int times, int ms);
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max);
void streamCallback(StreamData data);
void streamTimeoutCallback(bool timeout);

// ════════════════════════════════════════════════════
//  ⚠️  REPLACE THESE WITH YOUR ACTUAL VALUES  ⚠️
// ════════════════════════════════════════════════════

#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"
#define FIREBASE_HOST    "https://YOUR_PROJECT_ID-default-rtdb.firebaseio.com"
#define FIREBASE_API_KEY "YOUR_WEB_API_KEY"
#define USER_EMAIL       "esp32@smartsense.io"
#define USER_PASSWORD    "your_esp32_password"

// ════════════════════════════════════════════════════
//  YOUR EXACT PIN CONFIGURATION
// ════════════════════════════════════════════════════

// Sensor INPUT pins
#define DHT22_PIN   4     // DHT22 — Outdoor temp & humidity
#define DHT11_PIN   15    // DHT11 — Indoor  temp & humidity
#define MQ135_PIN   32    // MQ-135 — Air Quality (CO2 / VOC)
#define MQ7_PIN     33    // MQ-7   — Carbon Monoxide
#define MQ3_PIN     34    // MQ-3   — Alcohol  ⚠ INPUT ONLY pin
#define SOUND_PIN   35    // Sound Sensor       ⚠ INPUT ONLY pin
#define SOIL_PIN    36    // Soil Moisture       ⚠ INPUT ONLY pin
#define RAIN_PIN    39    // Rain Sensor         ⚠ INPUT ONLY pin

// OUTPUT pins
#define RELAY_PIN   5     // Relay (controls Fan / main device)
#define BUZZER      19    // Buzzer (alert sounds)
#define LED_PIN     2     // Onboard LED (WiFi/Firebase status)

// OLED Display (I2C — SDA=21, SCL=22 default on ESP32)
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_RESET  -1
#define OLED_ADDR   0x3C

// DHT sensor types
#define DHT22_TYPE  DHT22
#define DHT11_TYPE  DHT11

// ════════════════════════════════════════════════════
//  ALERT THRESHOLDS (updatable from Firebase dashboard)
// ════════════════════════════════════════════════════

float TEMP_MAX        = 35.0;   // °C — triggers relay
float TEMP_MIN        = 10.0;   // °C
float HUMIDITY_MAX    = 80.0;   // % — triggers relay + alert
float AQI_MAX         = 150.0;  // AQI — triggers alert
float CO_MAX          = 35.0;   // ppm — emergency alert
float ALCOHOL_MAX     = 30.0;   // ppm — alert
float SOIL_DRY_THRESH = 30.0;   // % below = dry alert
int   SOUND_THRESH    = 80;     // dB above = noise alert

// ════════════════════════════════════════════════════
//  TIMING
// ════════════════════════════════════════════════════

#define UPLOAD_INTERVAL    5000    // ms — how often to send to Firebase
#define DEVICE_CHECK_MS    3000    // ms — how often to read device commands
#define DISPLAY_REFRESH    4000    // ms — OLED page switch interval
#define ALERT_COOLDOWN    60000    // ms — prevent repeated same alerts
#define THRESHOLD_CHECK   30000    // ms — check Firebase thresholds

// ════════════════════════════════════════════════════
//  GLOBALS
// ════════════════════════════════════════════════════

FirebaseData   fbData;
FirebaseData   fbStream;
FirebaseAuth   fbAuth;
FirebaseConfig fbConfig;

DHT dht22(DHT22_PIN, DHT22_TYPE);   // Outdoor
DHT dht11(DHT11_PIN, DHT11_TYPE);   // Indoor

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool oledAvailable = false;

// ── Sensor Readings ──
float  tempDHT22     = 0;     // Outdoor temperature (°C)
float  humDHT22      = 0;     // Outdoor humidity (%)
float  tempDHT11     = 0;     // Indoor temperature (°C)
float  humDHT11      = 0;     // Indoor humidity (%)
float  airQuality    = 0;     // AQI level
float  coLevel       = 0;     // CO ppm
float  alcoholLevel  = 0;     // Alcohol ppm
float  soundLevel    = 0;     // Estimated dB
float  soilMoisture  = 0;     // Soil moisture %
bool   rainDetected  = false; // Rain detected flag

// ── Device States ──
bool relayOn  = false;
bool buzzerOn = false;

// ── Timers ──
unsigned long lastUpload       = 0;
unsigned long lastDeviceCheck  = 0;
unsigned long lastDisplay      = 0;
unsigned long lastThreshCheck  = 0;
unsigned long lastCOAlert      = 0;
unsigned long lastAQIAlert     = 0;
unsigned long lastAlcAlert     = 0;
unsigned long lastTempAlert    = 0;
unsigned long lastHumAlert     = 0;
unsigned long lastRainAlert    = 0;

uint8_t  displayPage    = 0;
bool     firebaseReady  = false;
bool     lastRainState  = false;
uint32_t alertCounter   = 1;

// ════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("╔══════════════════════════╗");
  Serial.println("║  SmartSense ESP32 Boot   ║");
  Serial.println("╚══════════════════════════╝");
  Serial.println("Pin Map:");
  Serial.println("  DHT22=4 | DHT11=15 | MQ135=32 | MQ7=33");
  Serial.println("  MQ3=34  | Sound=35  | Soil=36  | Rain=39");
  Serial.println("  Relay=5 | Buzzer=19 | LED=2");

  // ── OUTPUT pins init ──
  pinMode(RELAY_PIN, OUTPUT); digitalWrite(RELAY_PIN, LOW);  // OFF
  pinMode(BUZZER,    OUTPUT); digitalWrite(BUZZER,    LOW);  // OFF
  pinMode(LED_PIN,   OUTPUT); digitalWrite(LED_PIN,   LOW);  // OFF

  // ── INPUT-ONLY pins 34,35,36,39 — no pinMode needed ──
  // They are hardware input-only. Do NOT call pinMode(..., OUTPUT).

  // ── Init both DHT sensors ──
  dht22.begin();
  delay(100);
  dht11.begin();
  delay(100);

  // ── Init OLED (SDA=21, SCL=22) ──
  Wire.begin(21, 22);
  if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledAvailable = true;
    Serial.println("[OLED] SSD1306 found on 0x3C");
    oledSplash();
  } else {
    Serial.println("[OLED] Not found — check wiring/I2C address");
  }

  blinkLED(3, 100);

  // ── WiFi ──
  connectWiFi();

  // ── Firebase ──
  setupFirebase();

  Serial.println("\n[Boot] Ready — entering main loop\n");
  blinkLED(5, 80);
}

// ════════════════════════════════════════════════════
//  MAIN LOOP
// ════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();

  // 1. Read sensors
  readAllSensors();

  // 2. Local automation (fast, no cloud needed)
  runAutomation(now);

  // 3. Upload to Firebase
  if (now - lastUpload >= UPLOAD_INTERVAL) {
    lastUpload = now;
    if (firebaseReady) {
      uploadSensorsToFirebase();
      uploadDeviceStates();
    }
  }

  // 4. Read device commands from dashboard
  if (now - lastDeviceCheck >= DEVICE_CHECK_MS) {
    lastDeviceCheck = now;
    if (firebaseReady) readDeviceCommandsFromFirebase();
  }

  // 5. Sync thresholds from Firebase
  if (now - lastThreshCheck >= THRESHOLD_CHECK) {
    lastThreshCheck = now;
    if (firebaseReady) syncThresholdsFromFirebase();
  }

  // 6. OLED update
  if (now - lastDisplay >= DISPLAY_REFRESH) {
    lastDisplay = now;
    if (oledAvailable) updateOLED();
    displayPage = (displayPage + 1) % 5;
  }

  // 7. LED heartbeat — slow blink = ok, fast blink = no WiFi
  static unsigned long lastLEDToggle = 0;
  int blinkRate = firebaseReady ? 2000 : 200;
  if (now - lastLEDToggle >= (unsigned long)blinkRate) {
    lastLEDToggle = now;
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }

  delay(50);
}

// ════════════════════════════════════════════════════
//  WIFI
// ════════════════════════════════════════════════════

void connectWiFi() {
  Serial.printf("[WiFi] Connecting to: %s\n", WIFI_SSID);
  if (oledAvailable) oledMsg("Connecting WiFi", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int dots = 0;
  while (WiFi.status() != WL_CONNECTED && dots < 60) {
    delay(500);
    Serial.print(".");
    dots++;
    if (dots % 20 == 0) Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected — IP: %s\n",
      WiFi.localIP().toString().c_str());
    if (oledAvailable)
      oledMsg("WiFi Connected!", WiFi.localIP().toString().c_str());
    blinkLED(3, 200);
    delay(800);
  } else {
    Serial.println("\n[WiFi] FAILED — offline mode");
    if (oledAvailable) oledMsg("WiFi FAILED", "Offline mode");
    delay(1500);
  }
}

// ════════════════════════════════════════════════════
//  FIREBASE SETUP
// ════════════════════════════════════════════════════

void setupFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Firebase] Skipped — no WiFi");
    return;
  }

  Serial.println("[Firebase] Initializing...");
  if (oledAvailable) oledMsg("Firebase", "Connecting...");

  fbConfig.host                  = FIREBASE_HOST;
  fbConfig.api_key               = FIREBASE_API_KEY;
  fbAuth.user.email              = USER_EMAIL;
  fbAuth.user.password           = USER_PASSWORD;
  fbConfig.token_status_callback = tokenStatusCallback;

  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);
  fbData.setResponseSize(4096);

  Serial.print("[Firebase] Authenticating");
  for (int i = 0; i < 40 && !Firebase.ready(); i++) {
    Serial.print(".");
    delay(500);
  }

  if (Firebase.ready()) {
    firebaseReady = true;
    Serial.println("\n[Firebase] ✓ Connected!");
    if (oledAvailable) oledMsg("Firebase OK!", "Connected");

    // Write ESP32 boot info
    Firebase.setString(fbData, "/system/status",    "online");
    Firebase.setString(fbData, "/system/ip",         WiFi.localIP().toString().c_str());
    Firebase.setString(fbData, "/system/firmware",   "SmartSense v1.0");
    Firebase.setString(fbData, "/system/board",      "ESP32");
    Firebase.setString(fbData, "/system/pinMap",
      "DHT22=4,DHT11=15,MQ135=32,MQ7=33,MQ3=34,Sound=35,Soil=36,Rain=39,Relay=5,Buzzer=19,LED=2");
    Firebase.setTimestamp(fbData, "/system/lastBoot");

    // Real-time stream on /devices path
    if (!Firebase.beginStream(fbStream, "/devices")) {
      Serial.println("[Stream] Failed: " + fbStream.errorReason());
    } else {
      Firebase.setStreamCallback(fbStream, streamCallback, streamTimeoutCallback);
      Serial.println("[Stream] ✓ Listening on /devices");
    }

    delay(500);
  } else {
    Serial.println("\n[Firebase] FAILED — check config");
    if (oledAvailable) oledMsg("Firebase FAIL", "Check config");
    delay(2000);
  }
}

// ════════════════════════════════════════════════════
//  SENSOR READING
// ════════════════════════════════════════════════════

void readAllSensors() {

  // ── DHT22 (GPIO 4) — Outdoor ──
  float t22 = dht22.readTemperature();
  float h22 = dht22.readHumidity();
  if (!isnan(t22) && t22 > -40 && t22 < 80)  tempDHT22 = t22;
  if (!isnan(h22) && h22 >= 0 && h22 <= 100) humDHT22  = h22;

  // ── DHT11 (GPIO 15) — Indoor ──
  float t11 = dht11.readTemperature();
  float h11 = dht11.readHumidity();
  if (!isnan(t11) && t11 > -10 && t11 < 60)  tempDHT11 = t11;
  if (!isnan(h11) && h11 >= 0 && h11 <= 100) humDHT11  = h11;

  // ── MQ-135 (GPIO 32) — Air Quality ──
  // ESP32 ADC1 = 12-bit → 0 to 4095
  // For accurate ppm: calibrate Ro in clean air (Rs/Ro ratio method)
  int raw135  = analogRead(MQ135_PIN);
  airQuality  = mapFloat(raw135, 0, 4095, 0, 500);

  // ── MQ-7 (GPIO 33) — Carbon Monoxide ──
  int raw7  = analogRead(MQ7_PIN);
  coLevel   = mapFloat(raw7, 0, 4095, 0, 1000);

  // ── MQ-3 (GPIO 34) — Alcohol [INPUT-ONLY] ──
  int raw3      = analogRead(MQ3_PIN);
  alcoholLevel  = mapFloat(raw3, 0, 4095, 0, 500);

  // ── Sound Sensor (GPIO 35) — Peak sampling [INPUT-ONLY] ──
  int maxSound = 0;
  for (int i = 0; i < 10; i++) {
    int s = analogRead(SOUND_PIN);
    if (s > maxSound) maxSound = s;
    delay(2);
  }
  soundLevel = mapFloat(maxSound, 0, 4095, 30, 130);

  // ── Soil Moisture (GPIO 36) — [INPUT-ONLY] ──
  // Capacitive sensor: HIGH voltage = dry, LOW voltage = wet
  // Calibrate: rawDry (~3000 in air), rawWet (~1000 in water)
  int rawSoil  = analogRead(SOIL_PIN);
  soilMoisture = mapFloat(rawSoil, 3000, 1000, 0, 100);
  soilMoisture = constrain(soilMoisture, 0.0f, 100.0f);

  // ── Rain Sensor (GPIO 39) — [INPUT-ONLY, no internal pull-up] ──
  // Wire: sensor DO pin → GPIO39, sensor VCC → 3.3V, GND → GND
  // Add 10kΩ pull-up resistor between GPIO39 and 3.3V externally
  // Analog threshold: low value = wet/rain
  int rawRain  = analogRead(RAIN_PIN);
  rainDetected = (rawRain < 1500);  // Adjust 1500 to your sensor

  // ── Serial debug every 5s ──
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 5000) {
    lastDebug = millis();
    Serial.println("─────────────────────────────────────────");
    Serial.printf("[DHT22 GPIO4 ] Outdoor Temp: %.1f°C  Hum: %.1f%%\n",
      tempDHT22, humDHT22);
    Serial.printf("[DHT11 GPIO15] Indoor  Temp: %.1f°C  Hum: %.1f%%\n",
      tempDHT11, humDHT11);
    Serial.printf("[MQ135 GPIO32] AQI: %.0f\n",           airQuality);
    Serial.printf("[MQ7   GPIO33] CO:  %.1f ppm\n",       coLevel);
    Serial.printf("[MQ3   GPIO34] Alc: %.1f ppm\n",       alcoholLevel);
    Serial.printf("[Sound GPIO35] ~%.0f dB\n",            soundLevel);
    Serial.printf("[Soil  GPIO36] %.0f%%\n",              soilMoisture);
    Serial.printf("[Rain  GPIO39] %s\n",
      rainDetected ? "RAIN ☔" : "Dry ☀");
    Serial.printf("[Relay GPIO5 ] %s | [Buzzer GPIO19] %s | [LED GPIO2] %s\n",
      relayOn ? "ON" : "OFF",
      buzzerOn ? "ON" : "OFF",
      digitalRead(LED_PIN) ? "ON" : "OFF");
    Serial.println("─────────────────────────────────────────");
  }
}

// ════════════════════════════════════════════════════
//  FIREBASE UPLOAD → /sensors
// ════════════════════════════════════════════════════

void uploadSensorsToFirebase() {
  FirebaseJson json;

  // DHT22 outdoor
  json.set("tempOutdoor",    tempDHT22);
  json.set("humOutdoor",     humDHT22);

  // DHT11 indoor (used as main dashboard display)
  json.set("temperature",    tempDHT11);
  json.set("humidity",       humDHT11);

  // Gas / Air sensors
  json.set("airQuality",     airQuality);
  json.set("coLevel",        coLevel);
  json.set("alcoholLevel",   alcoholLevel);

  // Environment
  json.set("soundLevel",     soundLevel);
  json.set("soilMoisture",   soilMoisture);
  json.set("rainDetected",   rainDetected);

  // System
  json.set("rssi",           (int)WiFi.RSSI());
  json.set("uptime",         (int)(millis() / 1000));
  json.set("lastUpdate/.sv", "timestamp");   // Server timestamp

  if (Firebase.updateNode(fbData, "/sensors", json)) {
    Serial.println("[Firebase] ✓ Sensors uploaded");
  } else {
    Serial.println("[Firebase] ✗ Error: " + fbData.errorReason());
  }
}

void uploadDeviceStates() {
  FirebaseJson d;
  d.set("relay/state",  relayOn  ? "on" : "off");
  d.set("buzzer/state", buzzerOn ? "on" : "off");
  Firebase.updateNode(fbData, "/deviceStates", d);
}

// ════════════════════════════════════════════════════
//  READ DEVICE COMMANDS FROM FIREBASE
//  Dashboard writes /devices → ESP32 reads & reacts
// ════════════════════════════════════════════════════

void readDeviceCommandsFromFirebase() {
  // Relay (GPIO 5)
  if (Firebase.getString(fbData, "/devices/relay/state")) {
    bool shouldBeOn = (fbData.stringData() == "on");
    if (shouldBeOn != relayOn) setRelay(shouldBeOn);
  }
  // Buzzer (GPIO 19)
  if (Firebase.getString(fbData, "/devices/buzzer/state")) {
    bool shouldBeOn = (fbData.stringData() == "on");
    if (shouldBeOn != buzzerOn) setBuzzer(shouldBeOn);
  }
}

// ════════════════════════════════════════════════════
//  DEVICE CONTROL
// ════════════════════════════════════════════════════

void setRelay(bool on) {
  relayOn = on;
  // Most relay boards: HIGH = relay ON coil energized
  // If yours triggers on LOW, swap HIGH/LOW below
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
  Serial.printf("[Relay  GPIO5 ] → %s\n", on ? "ON ✓" : "OFF");
  if (firebaseReady)
    Firebase.setString(fbData, "/devices/relay/state", on ? "on" : "off");
}

void setBuzzer(bool on) {
  buzzerOn = on;
  digitalWrite(BUZZER, on ? HIGH : LOW);
  Serial.printf("[Buzzer GPIO19] → %s\n", on ? "ON ✓" : "OFF");
  if (firebaseReady)
    Firebase.setString(fbData, "/devices/buzzer/state", on ? "on" : "off");
}

// ════════════════════════════════════════════════════
//  AUTOMATION RULES — runs locally (no cloud latency)
// ════════════════════════════════════════════════════

void runAutomation(unsigned long now) {

  // ── RULE 1: High Temperature → Relay ON ──
  if ((tempDHT11 > TEMP_MAX || tempDHT22 > TEMP_MAX + 2) && !relayOn) {
    setRelay(true);
    if (now - lastTempAlert > ALERT_COOLDOWN) {
      lastTempAlert = now;
      pushAlert("MEDIUM", "High Temperature",
        "Indoor: " + String(tempDHT11, 1) + "C  Outdoor: " + String(tempDHT22, 1) + "C. Relay activated.",
        "🌡", "Main Zone");
    }
  }

  // ── RULE 2: High Humidity → Relay ON (ventilation) ──
  if (humDHT11 > HUMIDITY_MAX && !relayOn) {
    setRelay(true);
    if (now - lastHumAlert > ALERT_COOLDOWN) {
      lastHumAlert = now;
      pushAlert("MEDIUM", "High Humidity",
        "Indoor humidity: " + String(humDHT11, 1) + "%. Ventilation activated.",
        "💧", "Indoor");
    }
  }

  // ── RULE 3: Carbon Monoxide DANGER → Relay + Buzzer ──
  if (coLevel > CO_MAX) {
    if (!relayOn)  setRelay(true);
    if (!buzzerOn) setBuzzer(true);
    if (now - lastCOAlert > ALERT_COOLDOWN) {
      lastCOAlert = now;
      emergencyBeep(5);
      pushAlert("HIGH", "Carbon Monoxide Warning",
        "MQ-7 (GPIO33): " + String(coLevel, 1) + " ppm — Limit: " + String(CO_MAX, 0) + " ppm!",
        "☣", "Kitchen / Garage");
      if (firebaseReady)
        Firebase.setBool(fbData, "/system/emergencyMode", true);
    }
  } else if (coLevel < CO_MAX * 0.6 && buzzerOn) {
    setBuzzer(false);
    if (firebaseReady)
      Firebase.setBool(fbData, "/system/emergencyMode", false);
  }

  // ── RULE 4: Poor Air Quality → Alert ──
  if (airQuality > AQI_MAX && (now - lastAQIAlert > ALERT_COOLDOWN)) {
    lastAQIAlert = now;
    pushAlert("MEDIUM", "Poor Air Quality",
      "MQ-135 (GPIO32) AQI: " + String(airQuality, 0) + ". Open windows / ventilate.",
      "💨", "Indoor Zone");
  }

  // ── RULE 5: Alcohol Detected → Alert + short Buzzer beep ──
  if (alcoholLevel > ALCOHOL_MAX && (now - lastAlcAlert > ALERT_COOLDOWN)) {
    lastAlcAlert = now;
    beepAlert(2);
    pushAlert("MEDIUM", "Alcohol Detected",
      "MQ-3 (GPIO34): " + String(alcoholLevel, 1) + " ppm. Check the area.",
      "🍺", "Zone");
  }

  // ── RULE 6: Rain New Event → Alert ──
  if (rainDetected && !lastRainState) {
    if (now - lastRainAlert > ALERT_COOLDOWN) {
      lastRainAlert = now;
      pushAlert("LOW", "Rain Detected",
        "Rain sensor (GPIO39) activated. Protect outdoor equipment.",
        "🌧", "Outdoor");
    }
  }
  lastRainState = rainDetected;

  // ── RULE 7: Dry Soil Alert ──
  static unsigned long lastSoilAlert = 0;
  if (soilMoisture > 0 && soilMoisture < SOIL_DRY_THRESH &&
      (now - lastSoilAlert > ALERT_COOLDOWN * 5)) {
    lastSoilAlert = now;
    pushAlert("LOW", "Soil Moisture Low",
      "Soil sensor (GPIO36): " + String(soilMoisture, 0) + "%. Consider watering.",
      "🌱", "Garden");
  }
}

// ════════════════════════════════════════════════════
//  PUSH ALERT → /alerts (dashboard reads & displays)
// ════════════════════════════════════════════════════

void pushAlert(String severity, String title,
               String message, String icon, String zone) {
  Serial.println("[Alert] " + severity + " — " + title);

  if (!firebaseReady) return;

  String alertId = "alert_" + String(alertCounter++);
  FirebaseJson a;
  a.set("id",            alertId);
  a.set("severity",      severity);
  a.set("title",         title);
  a.set("message",       message);
  a.set("icon",          icon);
  a.set("zone",          zone);
  a.set("resolved",      false);
  a.set("timestamp/.sv", "timestamp");

  if (Firebase.set(fbData, "/alerts/" + alertId, a)) {
    Serial.println("[Alert] ✓ Pushed to Firebase");
  } else {
    Serial.println("[Alert] ✗ Failed: " + fbData.errorReason());
  }

  beepAlert(severity == "HIGH" ? 3 : 1);
}

// ════════════════════════════════════════════════════
//  SYNC THRESHOLDS FROM FIREBASE
//  Dashboard Settings page writes /thresholds
// ════════════════════════════════════════════════════

void syncThresholdsFromFirebase() {
  if (!Firebase.getJSON(fbData, "/thresholds")) return;

  FirebaseJson &j = fbData.jsonObject();
  FirebaseJsonData r;

  if (j.get(r, "tempMax"))  TEMP_MAX     = r.floatValue;
  if (j.get(r, "tempMin"))  TEMP_MIN     = r.floatValue;
  if (j.get(r, "humMax"))   HUMIDITY_MAX = r.floatValue;
  if (j.get(r, "gasMax"))   CO_MAX       = r.floatValue;
  if (j.get(r, "alcMax"))   ALCOHOL_MAX  = r.floatValue;
  if (j.get(r, "aqiMax"))   AQI_MAX      = r.floatValue;

  Serial.printf("[Thresholds] TempMax:%.0f HumMax:%.0f CO:%.0f AQI:%.0f Alc:%.0f\n",
    TEMP_MAX, HUMIDITY_MAX, CO_MAX, AQI_MAX, ALCOHOL_MAX);
}

// ════════════════════════════════════════════════════
//  FIREBASE STREAM CALLBACK — instant device response
// ════════════════════════════════════════════════════

void streamCallback(StreamData data) {
  String path = data.dataPath();
  String val  = data.stringData();
  Serial.printf("[Stream] %s = %s\n", path.c_str(), val.c_str());

  bool on = (val == "on");
  if (path.indexOf("relay")  >= 0) setRelay(on);
  if (path.indexOf("buzzer") >= 0) setBuzzer(on);
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("[Stream] Timeout — reconnecting...");
}

// ════════════════════════════════════════════════════
//  OLED DISPLAY — 5 rotating pages
// ════════════════════════════════════════════════════

void updateOLED() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.invertDisplay(false);

  switch (displayPage) {

    case 0:  // Indoor Climate (DHT11)
      oled.setTextSize(1);  oled.setCursor(0, 0);
      oled.println("INDOOR  (DHT11 G15)");
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setTextSize(2);  oled.setCursor(0, 14);
      oled.printf("%.1fC", tempDHT11);
      oled.setTextSize(1);  oled.setCursor(0, 36);
      oled.printf("Humidity : %.0f%%", humDHT11);
      oled.setCursor(0, 46);
      oled.printf("AQI G32  : %.0f %s",
        airQuality,
        airQuality < 50  ? "Good" :
        airQuality < 150 ? "OK"   : "POOR");
      oled.setCursor(0, 56);
      oled.printf("WiFi:%s  FB:%s",
        WiFi.status() == WL_CONNECTED ? "OK" : "XX",
        firebaseReady ? "OK" : "XX");
      break;

    case 1:  // Outdoor Climate (DHT22)
      oled.setTextSize(1);  oled.setCursor(0, 0);
      oled.println("OUTDOOR (DHT22 G4)");
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setTextSize(2);  oled.setCursor(0, 14);
      oled.printf("%.1fC", tempDHT22);
      oled.setTextSize(1);  oled.setCursor(0, 36);
      oled.printf("Humidity : %.0f%%", humDHT22);
      oled.setCursor(0, 46);
      oled.printf("Rain G39 : %s",
        rainDetected ? "RAINING!" : "Dry");
      oled.setCursor(0, 56);
      oled.printf("Soil G36 : %.0f%%", soilMoisture);
      break;

    case 2: {  // Gas Sensors — braces required for local variable
      oled.setTextSize(1);  oled.setCursor(0, 0);
      oled.println("GAS SENSORS");
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 12);
      oled.printf("CO  G33: %.1f ppm", coLevel);
      oled.setCursor(0, 24);
      oled.printf("Alc G34: %.1f ppm", alcoholLevel);
      oled.setCursor(0, 36);
      oled.printf("AQI G32: %.0f", airQuality);
      oled.setCursor(0, 50);
      bool gasDanger = (coLevel >= CO_MAX || alcoholLevel >= ALCOHOL_MAX);
      if (gasDanger) {
        oled.setTextSize(2);
        oled.setCursor(10, 48);
        oled.print("!! ALERT");
        oled.invertDisplay(true);
      } else {
        oled.print("Status : SAFE");
      }
      break;
    }

    case 3:  // Sound + Devices
      oled.setTextSize(1);  oled.setCursor(0, 0);
      oled.println("DEVICES & SOUND");
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 12);
      oled.printf("Sound G35: %.0f dB %s",
        soundLevel, soundLevel > SOUND_THRESH ? "LOUD" : "OK");
      oled.setCursor(0, 24);
      oled.printf("Relay  G5 : %s", relayOn  ? "[  ON  ]" : "[  OFF ]");
      oled.setCursor(0, 36);
      oled.printf("Buzzer G19: %s", buzzerOn ? "[  ON  ]" : "[  OFF ]");
      oled.setCursor(0, 48);
      oled.printf("LED    G2 : %s",
        digitalRead(LED_PIN) ? "[  ON  ]" : "[  OFF ]");
      break;

    case 4: {  // System Info — braces required for local variables
      oled.setTextSize(1);  oled.setCursor(0, 0);
      oled.println("SYSTEM INFO");
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 12);
      oled.printf("WiFi: %s",
        WiFi.status() == WL_CONNECTED ? "Connected" : "OFFLINE");
      if (WiFi.status() == WL_CONNECTED) {
        oled.setCursor(0, 22);
        oled.printf("RSSI: %d dBm", WiFi.RSSI());
        oled.setCursor(0, 32);
        String ip = WiFi.localIP().toString();
        oled.printf("IP: %s", ip.c_str());
      }
      oled.setCursor(0, 42);
      oled.printf("FB: %s", firebaseReady ? "Connected" : "Offline");
      oled.setCursor(0, 52);
      unsigned long s = millis() / 1000;
      oled.printf("Up: %02lu:%02lu:%02lu", s / 3600, (s % 3600) / 60, s % 60);
      break;
    }

  }  // end switch(displayPage)

  oled.display();
}

void oledMsg(const char* l1, const char* l2) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 14); oled.println(l1);
  oled.setCursor(0, 32); oled.println(l2);
  oled.display();
}

void oledSplash() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(2);  oled.setCursor(8, 6);   oled.println("SmartSense");
  oled.setTextSize(1);  oled.setCursor(0, 36);  oled.println("  IoT Monitor v1.0");
  oled.setCursor(0, 48);                         oled.println("  ESP32 + Firebase");
  oled.display();
  delay(1800);
}

// ════════════════════════════════════════════════════
//  BUZZER PATTERNS (GPIO 19)
// ════════════════════════════════════════════════════

void beepAlert(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER, HIGH); delay(150);
    digitalWrite(BUZZER, LOW);  delay(100);
  }
}

void emergencyBeep(int rounds) {
  for (int r = 0; r < rounds; r++) {
    for (int i = 0; i < 6; i++) {
      digitalWrite(BUZZER, HIGH); delay(60);
      digitalWrite(BUZZER, LOW);  delay(50);
    }
    delay(200);
  }
}

// ════════════════════════════════════════════════════
//  LED HELPER (GPIO 2)
// ════════════════════════════════════════════════════

void blinkLED(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(ms);
    digitalWrite(LED_PIN, LOW);  delay(ms);
  }
}

// ════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════

float mapFloat(float x, float in_min, float in_max,
               float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
