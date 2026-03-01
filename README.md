# CARMEL-FLAME-PRO
### DEMO Live Site: https://carmel-smarsense.vercel.app/

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
 ║    GPIO 39 (Rain): Add 10kΩ pull-up to 3.3V externally.      ║
 ║                                                              ║
 ║  LIBRARIES (install via Arduino Library Manager):            ║
 ║    1. Firebase ESP Client  → mobizt/Firebase ESP Client      ║
 ║    2. DHT sensor library   → Adafruit DHT                    ║
 ║    3. Adafruit Unified Sensor                                ║
 ║    4. Adafruit SSD1306                                       ║
 ║    5. Adafruit GFX Library                                   ║
 ╚══════════════════════════════════════════════════════════════╝
