#include <Arduino.h>
#include <Wire.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>

#include "glasses_database.h"

// -------------------------------------------------------------
// ADXL345 Accelerometer Configuration (ESP32-S3-Zero)
// -------------------------------------------------------------
#define ADXL345_ADDR    0x53
#define ADXL_SDA_PIN    1
#define ADXL_SCL_PIN    2
#define ADXL_INT1_PIN   3

// ADXL345 Registers
#define ADXL_THRESH_TAP  0x1D
#define ADXL_DUR         0x21
#define ADXL_LATENT      0x22
#define ADXL_WINDOW      0x23
#define ADXL_TAP_AXES    0x2A
#define ADXL_INT_ENABLE  0x2E
#define ADXL_INT_MAP     0x2F
#define ADXL_INT_SOURCE  0x30
#define ADXL_DATA_FORMAT 0x31
#define ADXL_POWER_CTL   0x2D

bool g_adxlAvailable = false;
volatile bool g_doubleTapDetected = false;

void IRAM_ATTR handleDoubleTapISR() {
    g_doubleTapDetected = true;
}

void adxlWriteRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t adxlReadRegister(uint8_t reg) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ADXL345_ADDR, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0;
}

void initADXL345() {
    Serial.println("[ADXL345] Initializing I2C bus (SDA: GPIO 1, SCL: GPIO 2)...");
    Wire.begin(ADXL_SDA_PIN, ADXL_SCL_PIN);
    Wire.setClock(400000);

    Wire.beginTransmission(ADXL345_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[ADXL345] Warning: ADXL345 not responding at 0x53. Double-tap detection disabled.");
        g_adxlAvailable = false;
        return;
    }

    g_adxlAvailable = true;
    Serial.println("[ADXL345] Sensor found at 0x53! Configuring double-tap detection...");

    // Measurement mode
    adxlWriteRegister(ADXL_POWER_CTL, 0x08);
    // +/- 16g, full resolution
    adxlWriteRegister(ADXL_DATA_FORMAT, 0x0B);
    // Tap threshold = 2g
    adxlWriteRegister(ADXL_THRESH_TAP, 0x20);
    // Max tap duration = 10ms (16 * 625us)
    adxlWriteRegister(ADXL_DUR, 0x10);
    // Latency between taps = 100ms (80 * 1.25ms)
    adxlWriteRegister(ADXL_LATENT, 0x50);
    // Max window for second tap = 300ms (240 * 1.25ms)
    adxlWriteRegister(ADXL_WINDOW, 0xF0);
    // Enable X, Y, Z axes for tap
    adxlWriteRegister(ADXL_TAP_AXES, 0x07);
    // Enable DOUBLE_TAP interrupt (Bit 5)
    adxlWriteRegister(ADXL_INT_ENABLE, 0x20);
    // Send DOUBLE_TAP to INT1 pin
    adxlWriteRegister(ADXL_INT_MAP, 0x00);
    // Clear any previous interrupt
    adxlReadRegister(ADXL_INT_SOURCE);

    pinMode(ADXL_INT1_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(ADXL_INT1_PIN), handleDoubleTapISR, RISING);

    Serial.println("[ADXL345] Double-tap detection initialized and listening on INT1 (GPIO 3).");
}

// -------------------------------------------------------------
// Vibration Motor Haptic Feedback Engine (GPIO 4)
// -------------------------------------------------------------
#define MOTOR_PIN 4

enum HapticPattern {
    HAPTIC_NONE,
    HAPTIC_SINGLE,      // Crisp single buzz (120ms)
    HAPTIC_DOUBLE,      // Alert double buzz (75ms ON, 65ms pause, 75ms ON)
    HAPTIC_RAGE         // Aggressive triple buzz (60ms ON, 40ms OFF, 60ms ON, 40ms OFF, 90ms ON)
};

struct HapticEngine {
    HapticPattern activePattern;
    uint8_t step;
    uint32_t stepEndTime;
} g_haptic = { HAPTIC_NONE, 0, 0 };

void triggerHaptic(HapticPattern pattern) {
    g_haptic.activePattern = pattern;
    g_haptic.step = 0;
    g_haptic.stepEndTime = millis();
}

void updateHaptic() {
    if (g_haptic.activePattern == HAPTIC_NONE) return;
    uint32_t now = millis();
    if (now < g_haptic.stepEndTime) return;

    if (g_haptic.activePattern == HAPTIC_SINGLE) {
        if (g_haptic.step == 0) {
            digitalWrite(MOTOR_PIN, HIGH);
            g_haptic.stepEndTime = now + 120; // 120ms buzz
            g_haptic.step = 1;
        } else {
            digitalWrite(MOTOR_PIN, LOW);
            g_haptic.activePattern = HAPTIC_NONE;
            g_haptic.step = 0;
        }
    }
    else if (g_haptic.activePattern == HAPTIC_DOUBLE) {
        if (g_haptic.step == 0) {
            digitalWrite(MOTOR_PIN, HIGH);
            g_haptic.stepEndTime = now + 75; // 75ms buzz
            g_haptic.step = 1;
        } else if (g_haptic.step == 1) {
            digitalWrite(MOTOR_PIN, LOW);
            g_haptic.stepEndTime = now + 65; // 65ms pause
            g_haptic.step = 2;
        } else if (g_haptic.step == 2) {
            digitalWrite(MOTOR_PIN, HIGH);
            g_haptic.stepEndTime = now + 75; // 75ms buzz
            g_haptic.step = 3;
        } else {
            digitalWrite(MOTOR_PIN, LOW);
            g_haptic.activePattern = HAPTIC_NONE;
            g_haptic.step = 0;
        }
    }
    else if (g_haptic.activePattern == HAPTIC_RAGE) {
        if (g_haptic.step == 0) {
            digitalWrite(MOTOR_PIN, HIGH);
            g_haptic.stepEndTime = now + 60; // 60ms pulse 1
            g_haptic.step = 1;
        } else if (g_haptic.step == 1) {
            digitalWrite(MOTOR_PIN, LOW);
            g_haptic.stepEndTime = now + 40; // 40ms pause
            g_haptic.step = 2;
        } else if (g_haptic.step == 2) {
            digitalWrite(MOTOR_PIN, HIGH);
            g_haptic.stepEndTime = now + 60; // 60ms pulse 2
            g_haptic.step = 3;
        } else if (g_haptic.step == 3) {
            digitalWrite(MOTOR_PIN, LOW);
            g_haptic.stepEndTime = now + 40; // 40ms pause
            g_haptic.step = 4;
        } else if (g_haptic.step == 4) {
            digitalWrite(MOTOR_PIN, HIGH);
            g_haptic.stepEndTime = now + 90; // 90ms pulse 3
            g_haptic.step = 5;
        } else {
            digitalWrite(MOTOR_PIN, LOW);
            g_haptic.activePattern = HAPTIC_NONE;
            g_haptic.step = 0;
        }
    }
}

// -------------------------------------------------------------
// LovyanGFX LCD Configuration for ESP32-S3-Zero (GC9A01 240x240)
// -------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01 _panel_instance;
    lgfx::Bus_SPI      _bus_instance;

public:
    LGFX() {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host    = SPI2_HOST; // FSPI on ESP32-S3
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;  // 40 MHz high-speed SPI
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO; // Hardware DMA
            cfg.pin_sclk    = 12;        // SCL (GPIO 12 verified)
            cfg.pin_mosi    = 11;        // SDA (GPIO 11)
            cfg.pin_miso    = -1;
            cfg.pin_dc      = 9;         // DC  (GPIO 9)
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = 10;   // CS  (GPIO 10)
            cfg.pin_rst          = 8;    // RST (GPIO 8)
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 240;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = false;
            cfg.invert           = true;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};

LGFX lcd;
LGFX_Sprite canvas(&lcd);      // Main 240x240 frame buffer
LGFX_Sprite leftEye(&canvas);   // Sub-pixel rotatable sprite
LGFX_Sprite rightEye(&canvas);  // Sub-pixel rotatable sprite

const int CX = 120;
const int CY = 120;

// -------------------------------------------------------------
// Emotion State Machine & Eye Geometry
// -------------------------------------------------------------
enum Emotion {
    EMOTE_HAPPY,      // Active / Normal (Level pill capsules, White)
    EMOTE_SLEEPY,     // Drowsy (30s-60s no glasses, droopy lids, slow blinks, NO Zzz)
    EMOTE_SLEEPING,   // Real Sleep Mode (60s+ no glasses, ultra-narrow slits, NO blinking, floating Zzz)
    EMOTE_CURIOUS,    // Distant glasses (Yellow eyes)
    EMOTE_ANGRY,      // Nearby glasses (Red eyes)
    EMOTE_CONFUSED,
    EMOTE_DAZED,      // Startled / Dazed wake-up (Asymmetric dizzy groggy eyes, pale cyan)
    EMOTE_RAGE,       // Super close glasses: Blood-red background, dark maroon eyes, rapid tremor
    EMOTE_COUNT
};

// Wake-up and Dazed Timing
uint32_t dazedStartTime = 0;
uint32_t dazedEndTime = 0;
uint32_t lastWakeActivityTime = 0;
uint32_t simulatedIdleMs = 0;

struct EyeParam {
    float x, y;       // Center coordinates
    float w, h;       // Width & Height
    float r;          // Corner radius
    float angle;      // Rotation angle in degrees
    uint16_t color;   // Eye color (RGB565)
};

// Current interpolated parameters
EyeParam curL, curR;
// Target parameters to lerp toward
EyeParam tgtL, tgtR;

Emotion currentEmotion = EMOTE_COUNT;
uint32_t lastEmotionChange = 0;

// Procedural Blinking Engine
bool isBlinking = false;
uint32_t blinkStartTime = 0;
uint32_t nextBlinkTime = 0;

// Natural Saccade (organic glance micro-movements)
float saccadeX = 0, saccadeY = 0;
uint32_t nextSaccadeTime = 0;

uint32_t lastFrameTime = 0;

// -------------------------------------------------------------
// Color Helper Functions for RGB565 Lerp & 8-bit RGB Conversion
// -------------------------------------------------------------
inline uint8_t getR565(uint16_t c) { return (c >> 11) & 0x1F; }
inline uint8_t getG565(uint16_t c) { return (c >> 5) & 0x3F; }
inline uint8_t getB565(uint16_t c) { return c & 0x1F; }

// Pack 5-bit R, 6-bit G, 5-bit B into 16-bit RGB565
inline uint16_t packRgb565(uint8_t r5, uint8_t g6, uint8_t b5) {
    return (((uint16_t)r5 & 0x1F) << 11) | (((uint16_t)g6 & 0x3F) << 5) | ((uint16_t)b5 & 0x1F);
}

// Convert standard 8-bit (0-255) R, G, B to 16-bit RGB565
inline uint16_t colorRGB(uint8_t r, uint8_t g, uint8_t b) {
    return (((uint16_t)(r >> 3) & 0x1F) << 11) |
           (((uint16_t)(g >> 2) & 0x3F) << 5)  |
           (((uint16_t)(b >> 3) & 0x1F));
}

// Alias for standard 8-bit RGB
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return colorRGB(r, g, b);
}

uint16_t lerpColor565(uint16_t c1, uint16_t c2, float factor) {
    if (c1 == c2) return c1;
    float r1 = getR565(c1), g1 = getG565(c1), b1 = getB565(c1);
    float r2 = getR565(c2), g2 = getG565(c2), b2 = getB565(c2);
    float dr = r2 - r1;
    float dg = g2 - g1;
    float db = b2 - b1;
    if (fabsf(dr) < 0.8f && fabsf(dg) < 0.8f && fabsf(db) < 0.8f) {
        return c2; // Snap cleanly to target color
    }
    uint8_t r = (uint8_t)roundf(r1 + dr * factor);
    uint8_t g = (uint8_t)roundf(g1 + dg * factor);
    uint8_t b = (uint8_t)roundf(b1 + db * factor);
    return packRgb565(r, g, b);
}

// -------------------------------------------------------------
// BLE Detection State & Thread Synchronization
// -------------------------------------------------------------
const int RSSI_RAGE_ENTER_THRESHOLD  = -55;        // dBm: >= -55 is super close (Rage Mode)
const int RSSI_RAGE_EXIT_THRESHOLD   = -62;        // dBm: < -62 drops from Rage back to Angry
const int RSSI_ANGRY_ENTER_THRESHOLD = -73;        // dBm: must reach >= -73 to trigger Angry (Red)
const int RSSI_ANGRY_EXIT_THRESHOLD  = -78;        // dBm: must drop < -78 to return to Curious (Yellow)
const uint32_t PROXIMITY_DEBOUNCE_MS = 1000;       // 1.0s delay to prevent rapid boundary switching
const uint32_t GLASSES_ACTIVE_TIMEOUT_MS = 1500;   // 1.5s without packet = device left (fast cooldown to normal)
const uint32_t SLEEPY_TIMEOUT_MS = 30000;          // 30s idle = Sleepy / Drowsy (blinking, no Zzz)
const uint32_t SLEEP_DEEP_TIMEOUT_MS = 60000;      // 60s (1 min) idle = Real Sleep (narrow slits, no blinking, Zzz)

struct DetectionState {
    portMUX_TYPE mux;
    uint32_t lastSeenMs;
    int lastRssi;
    char deviceName[64];
    char matchReason[64];
    bool active;
};

DetectionState g_detection = {
    .mux = portMUX_INITIALIZER_UNLOCKED,
    .lastSeenMs = 0,
    .lastRssi = -100,
    .deviceName = {0},
    .matchReason = {0},
    .active = false
};

void setEmotion(Emotion emo, bool force = false) {
    if (currentEmotion != emo || force) {

        lastEmotionChange = millis();
        currentEmotion = emo;

        switch (emo) {
            case EMOTE_RAGE: // Super close: Crimson background (#BA3636) with deep maroon eyes (#610000)
                tgtL = { CX - 35.0f, CY + 2.0f, 28.0f, 58.0f, 14.0f,  40.0f, rgb565(97, 0, 0) };
                tgtR = { CX + 35.0f, CY + 2.0f, 28.0f, 58.0f, 14.0f, -40.0f, rgb565(97, 0, 0) };
                break;

            case EMOTE_ANGRY: // Nearby glasses: Big fierce angled inward capsules \ / with RED eyes
                tgtL = { CX - 38.0f, CY + 6.0f, 30.0f, 60.0f, 15.0f,  45.0f, TFT_RED };
                tgtR = { CX + 38.0f, CY + 6.0f, 30.0f, 60.0f, 15.0f, -45.0f, TFT_RED };
                break;

            case EMOTE_CURIOUS: // Distant glasses: Big sleek diagonal capsule eyes with YELLOW eyes
                tgtL = { CX + 4.0f,  CY + 24.0f, 26.0f, 50.0f, 13.0f, -30.0f, TFT_YELLOW };
                tgtR = { CX + 52.0f, CY + 32.0f, 22.0f, 50.0f, 11.0f, -30.0f, TFT_YELLOW };
                break;

            case EMOTE_HAPPY: // Normal mode: Big, symmetrical level cute pill/capsule eyes in upper face (WHITE eyes)
                tgtL = { CX - 36.0f, CY + 14.0f, 32.0f, 64.0f, 16.0f, -5.0f, TFT_WHITE };
                tgtR = { CX + 36.0f, CY + 14.0f, 32.0f, 64.0f, 16.0f, -5.0f, TFT_WHITE };
                break;

            case EMOTE_SLEEPY: // 30s-60s no glasses: Big drowsy half-open eyes with slow blinking (no Zzz)
                tgtL = { CX - 40.0f, CY - 18.0f, 52.0f, 22.0f, 11.0f,  0.0f, TFT_WHITE };
                tgtR = { CX + 40.0f, CY - 18.0f, 52.0f, 22.0f, 11.0f,  0.0f, TFT_WHITE };
                break;

            case EMOTE_SLEEPING: // 60s+ no glasses: Real sleep mode - bold sleek peaceful closed slits, no blink, Zzz floating
                tgtL = { CX - 44.0f, CY - 28.0f, 64.0f,  8.0f,  4.0f,  0.0f, TFT_WHITE };
                tgtR = { CX + 44.0f, CY - 28.0f, 64.0f,  8.0f,  4.0f,  0.0f, TFT_WHITE };
                break;

            case EMOTE_CONFUSED: // Confused eyes
                tgtL = { CX - 40.0f, CY + 8.0f, 34.0f, 68.0f, 17.0f,   0.0f, TFT_CYAN };
                tgtR = { CX + 36.0f, CY - 4.0f, 30.0f, 52.0f, 15.0f,  55.0f, TFT_CYAN };
                break;

            case EMOTE_DAZED: // Startled / Dazed wake-up: Tired symmetrical droopy eyes (White eyes)
                tgtL = { CX - 40.0f, CY - 12.0f, 48.0f, 22.0f, 11.0f, -3.0f, TFT_WHITE };
                tgtR = { CX + 40.0f, CY - 12.0f, 48.0f, 22.0f, 11.0f,  3.0f, TFT_WHITE };
                break;

            default:
                break;
        }

    }
}



// -------------------------------------------------------------
// BLE Advertised Device Callback Engine
// -------------------------------------------------------------
class GlassesAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        bool matched = false;
        char matchInfo[64] = {0};
        int rssi = advertisedDevice->getRSSI();
        std::string nameStr = advertisedDevice->haveName() ? advertisedDevice->getName() : "";

        // 1. Check Manufacturer Specific Data (Company ID)
        if (advertisedDevice->haveManufacturerData()) {
            std::string mfg = advertisedDevice->getManufacturerData();
            if (mfg.length() >= 2) {
                uint16_t companyId = (uint8_t)mfg[0] | (((uint8_t)mfg[1]) << 8);
                for (size_t i = 0; i < TARGET_COMPANY_COUNT; i++) {
                    if (TARGET_COMPANY_IDS[i].id == companyId) {
                        matched = true;
                        snprintf(matchInfo, sizeof(matchInfo), "Mfg ID 0x%04X (%s - %s)",
                                 companyId, TARGET_COMPANY_IDS[i].company, TARGET_COMPANY_IDS[i].product);
                        break;
                    }
                }
            }
        }

        // 2. Check Device Name against target patterns
        if (!matched && !nameStr.empty()) {
            String lowerName = String(nameStr.c_str());
            lowerName.toLowerCase();
            for (size_t i = 0; i < TARGET_NAME_PATTERN_COUNT; i++) {
                if (lowerName.indexOf(TARGET_NAME_PATTERNS[i]) >= 0) {
                    matched = true;
                    snprintf(matchInfo, sizeof(matchInfo), "Name Match '%s'", TARGET_NAME_PATTERNS[i]);
                    break;
                }
            }
        }

        // If smart glasses or test device detected
        if (matched) {
            uint32_t now = millis();
            portENTER_CRITICAL(&g_detection.mux);
            g_detection.lastSeenMs = now;
            g_detection.lastRssi = rssi;
            g_detection.active = true;
            strncpy(g_detection.deviceName, nameStr.empty() ? "Unknown" : nameStr.c_str(), sizeof(g_detection.deviceName) - 1);
            strncpy(g_detection.matchReason, matchInfo, sizeof(g_detection.matchReason) - 1);
            portEXIT_CRITICAL(&g_detection.mux);

            Serial.printf("[BLE] TARGET DETECTED! %s | RSSI: %d dBm | %s | MAC: %s\n",
                          nameStr.empty() ? "Device" : nameStr.c_str(),
                          rssi, matchInfo, advertisedDevice->getAddress().toString().c_str());
        }
    }
};

// -------------------------------------------------------------
// FreeRTOS Task for Background BLE Scanning (Pinned to Core 0)
// -------------------------------------------------------------
void bleScanTask(void *pvParameters) {
    Serial.println("[BLE Task] Initializing NimBLE on Core 0...");
    NimBLEDevice::init("CompanionScanner");
    NimBLEScan* pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new GlassesAdvertisedDeviceCallbacks(), true); // wantDuplicates = true for real-time tracking
    pBLEScan->setActiveScan(true);  // Active scan for names & metadata
    pBLEScan->setInterval(100);     // Scan interval 100ms
    pBLEScan->setWindow(100);       // Scan window 100ms (100% continuous duty cycle = max range!)
    pBLEScan->setDuplicateFilter(false); // Process all advertisement packets
    pBLEScan->setMaxResults(0);     // Do not retain device list in RAM

    Serial.println("[BLE Task] Starting continuous background scan (100% duty cycle, max sensitivity)...");

    while (true) {
        // Continuous scan in 1-second blocks; results cleared to prevent RAM leaks
        pBLEScan->start(1, false);
        pBLEScan->clearResults();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// -------------------------------------------------------------
// Interactive Double-Tap Handler (ADXL345 & CLI)
// -------------------------------------------------------------
void triggerDoubleTap(const char* source = "Sensor") {
    bool isAsleep = (currentEmotion == EMOTE_SLEEPING || currentEmotion == EMOTE_SLEEPY);

    if (!isAsleep) {
        Serial.printf("[TAP] Double-tap ignored (%s) - companion is already awake (Emotion: %d)\n", source, (int)currentEmotion);
        return;
    }

    uint32_t now = millis();
    simulatedIdleMs = 0; // Clear any manual simulation override

    Serial.printf("[TAP] Double-tap detected (%s)! Waking up companion from sleep -> FLUTTER BLINKS & LOOKING AROUND (7.5s)\n", source);

    dazedStartTime = now;
    dazedEndTime = now + 7500; // 7.5 seconds total wake-up sequence
    lastWakeActivityTime = now;
    setEmotion(EMOTE_DAZED);
    isBlinking = false; // Choreographed flutter blinks handle eyelids
}

// -------------------------------------------------------------
// Interactive Serial Commands & Simulation
// -------------------------------------------------------------
void processSerialCommands() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toLowerCase();

        if (cmd == "rage" || cmd == "superclose" || cmd == "glasses_rage") {
            simulatedIdleMs = 0;
            triggerHaptic(HAPTIC_RAGE);
            portENTER_CRITICAL(&g_detection.mux);
            g_detection.lastSeenMs = millis();
            g_detection.lastRssi = -45; // Super close signal
            g_detection.active = true;
            strncpy(g_detection.deviceName, "Simulated Smart Glasses (Super Close)", sizeof(g_detection.deviceName) - 1);
            strncpy(g_detection.matchReason, "Manual Rage Sim", sizeof(g_detection.matchReason) - 1);
            portEXIT_CRITICAL(&g_detection.mux);
            Serial.println("[Sim] Glasses simulated SUPER CLOSE (-45 dBm) -> RAGE MODE (Crimson BG, Shaking Eyes) + Rage Buzz");
        }
        else if (cmd == "near" || cmd == "glasses_near") {
            simulatedIdleMs = 0;
            triggerHaptic(HAPTIC_DOUBLE);
            portENTER_CRITICAL(&g_detection.mux);
            g_detection.lastSeenMs = millis();
            g_detection.lastRssi = -68; // Nearby signal (-73 to -55 dBm)
            g_detection.active = true;
            strncpy(g_detection.deviceName, "Simulated Smart Glasses (Near)", sizeof(g_detection.deviceName) - 1);
            strncpy(g_detection.matchReason, "Manual Near Sim", sizeof(g_detection.matchReason) - 1);
            portEXIT_CRITICAL(&g_detection.mux);
            Serial.println("[Sim] Glasses simulated NEAR (-68 dBm) -> ANGRY (Red Eyes) + Double Haptic Buzz");
        }
        else if (cmd == "far" || cmd == "glasses_far") {
            simulatedIdleMs = 0;
            triggerHaptic(HAPTIC_SINGLE);
            portENTER_CRITICAL(&g_detection.mux);
            g_detection.lastSeenMs = millis();
            g_detection.lastRssi = -82; // Distant signal (< -78 dBm)
            g_detection.active = true;
            strncpy(g_detection.deviceName, "Simulated Smart Glasses (Far)", sizeof(g_detection.deviceName) - 1);
            strncpy(g_detection.matchReason, "Manual Far Sim", sizeof(g_detection.matchReason) - 1);
            portEXIT_CRITICAL(&g_detection.mux);
            Serial.println("[Sim] Glasses simulated FAR (-82 dBm) -> CURIOUS (Yellow Eyes) + Single Haptic Buzz");
        }
        else if (cmd == "none" || cmd == "happy" || cmd == "glasses_none") {
            simulatedIdleMs = 0;
            portENTER_CRITICAL(&g_detection.mux);
            g_detection.lastSeenMs = 0;
            g_detection.active = false;
            portEXIT_CRITICAL(&g_detection.mux);
            lastWakeActivityTime = millis();
            dazedStartTime = 0;
            dazedEndTime = 0;
            Serial.println("[Sim] No glasses around (<30s) -> HAPPY (White Eyes)");
        }
        else if (cmd == "sleepy") {
            simulatedIdleMs = SLEEPY_TIMEOUT_MS + 2000;
            portENTER_CRITICAL(&g_detection.mux);
            g_detection.lastSeenMs = 0;
            g_detection.active = false;
            portEXIT_CRITICAL(&g_detection.mux);
            lastWakeActivityTime = 0;
            dazedStartTime = 0;
            dazedEndTime = 0;
            Serial.println("[Sim] No glasses for 30s -> SLEEPY / Drowsy Mode (Droopy Eyes, Blinking, No Zzz)");
        }
        else if (cmd == "sleep" || cmd == "sleeping") {
            simulatedIdleMs = SLEEP_DEEP_TIMEOUT_MS + 2000;
            portENTER_CRITICAL(&g_detection.mux);
            g_detection.lastSeenMs = 0;
            g_detection.active = false;
            portEXIT_CRITICAL(&g_detection.mux);
            lastWakeActivityTime = 0;
            dazedStartTime = 0;
            dazedEndTime = 0;
            Serial.println("[Sim] No glasses for >60s (1 min) -> REAL SLEEP Mode (Narrow Slits, No Blinking, Zzz Animation)");
        }
        else if (cmd == "tap" || cmd == "doubletap" || cmd == "double_tap") {
            triggerDoubleTap("Serial CLI");
        }
        else if (cmd == "buzz" || cmd == "vibe" || cmd == "motor") {
            triggerHaptic(HAPTIC_DOUBLE);
            Serial.println("[Haptic] Motor test triggered (double buzz)!");
        }
        else if (cmd == "angry") {
            setEmotion(EMOTE_ANGRY);
            Serial.println("[Emote] Manual override -> ANGRY");
        }
        else if (cmd == "rage_manual") {
            setEmotion(EMOTE_RAGE);
            Serial.println("[Emote] Manual override -> RAGE");
        }
        else if (cmd == "curious") {
            setEmotion(EMOTE_CURIOUS);
            Serial.println("[Emote] Manual override -> CURIOUS");
        }
        else if (cmd == "confused") {
            setEmotion(EMOTE_CONFUSED);
            Serial.println("[Emote] Manual override -> CONFUSED");
        }
        else if (cmd == "status") {
            uint32_t now = millis();
            uint32_t lastSeen;
            int lastRssi;
            char devName[64];
            char reason[64];
            portENTER_CRITICAL(&g_detection.mux);
            lastSeen = g_detection.lastSeenMs;
            lastRssi = g_detection.lastRssi;
            strncpy(devName, g_detection.deviceName, sizeof(devName));
            strncpy(reason, g_detection.matchReason, sizeof(reason));
            portEXIT_CRITICAL(&g_detection.mux);

            uint32_t effIdle = (simulatedIdleMs > 0) ? simulatedIdleMs : ((lastWakeActivityTime > 0 && now >= lastWakeActivityTime) ? (now - lastWakeActivityTime) : 0);

            Serial.println("------------- STATUS -------------");
            Serial.printf("Current Emotion: %d\n", (int)currentEmotion);
            Serial.printf("Time since last glasses: %u ms\n", (now >= lastSeen && lastSeen > 0) ? (now - lastSeen) : 0);
            Serial.printf("Time since last wake/tap: %u ms\n", (now >= lastWakeActivityTime && lastWakeActivityTime > 0) ? (now - lastWakeActivityTime) : 0);
            Serial.printf("Effective Idle: %u ms\n", effIdle);
            Serial.printf("Dazed End Time: %u (now: %u)\n", dazedEndTime, now);
            Serial.printf("ADXL345 Available: %s\n", g_adxlAvailable ? "YES" : "NO");
            Serial.printf("Last Device: %s (RSSI: %d dBm)\n", devName, lastRssi);
            Serial.printf("Match Reason: %s\n", reason);
            Serial.println("----------------------------------");
        }
    }
}

// -------------------------------------------------------------
// Companion Emotion Update Engine based on BLE Detection
// -------------------------------------------------------------
void updateEmotionState() {
    uint32_t now = millis();

    // 1. If currently in dazed wake-up sequence
    if (dazedEndTime > 0) {
        if (now < dazedEndTime) {
            setEmotion(EMOTE_DAZED);
            return;
        } else {
            // Dazed duration just completed -> blink and transition to Happy
            dazedEndTime = 0;
            dazedStartTime = 0;
            isBlinking = true;
            blinkStartTime = now;
            setEmotion(EMOTE_HAPPY);
            return;
        }
    }

    uint32_t lastSeen;
    int lastRssi;

    portENTER_CRITICAL(&g_detection.mux);
    lastSeen = g_detection.lastSeenMs;
    lastRssi = g_detection.lastRssi;
    portEXIT_CRITICAL(&g_detection.mux);

    uint32_t timeSinceSeen = (lastSeen > 0 && now >= lastSeen) ? (now - lastSeen) : 0;
    bool glassesCurrentlyActive = (lastSeen > 0 && timeSinceSeen < GLASSES_ACTIVE_TIMEOUT_MS);
    
    enum ProximityTier {
        PROX_DISTANT,     // Curious (Yellow eyes)
        PROX_NEAR,        // Angry (Red eyes)
        PROX_SUPER_CLOSE  // Rage (Crimson BG, dark maroon eyes, rapid shaking)
    };
    static ProximityTier s_currentTier = PROX_DISTANT;
    static ProximityTier s_lastTier = PROX_DISTANT;
    static float s_filteredRssi = -100.0f;
    static uint32_t s_lastTierChangeTime = 0;
    static bool s_glassesWereActive = false;

    if (glassesCurrentlyActive) {
        // Smart glasses are actively present!
        simulatedIdleMs = 0;

        // Exponential Moving Average filter on RSSI to smooth out instantaneous radio spikes
        if (!s_glassesWereActive) {
            s_filteredRssi = (float)lastRssi; // Initialize on first packet
            if (s_filteredRssi >= (float)RSSI_RAGE_ENTER_THRESHOLD) {
                s_currentTier = PROX_SUPER_CLOSE;
            } else if (s_filteredRssi >= (float)RSSI_ANGRY_ENTER_THRESHOLD) {
                s_currentTier = PROX_NEAR;
            } else {
                s_currentTier = PROX_DISTANT;
            }
            s_lastTier = s_currentTier;
            s_lastTierChangeTime = now;

            // First detection haptic feedback
            if (s_currentTier == PROX_SUPER_CLOSE) {
                triggerHaptic(HAPTIC_RAGE);
            } else if (s_currentTier == PROX_NEAR) {
                triggerHaptic(HAPTIC_DOUBLE);
            } else {
                triggerHaptic(HAPTIC_SINGLE);
            }
        } else {
            s_filteredRssi = s_filteredRssi * 0.65f + ((float)lastRssi) * 0.35f;

            // Dual-threshold hysteresis with minimum debounce delay
            if (now - s_lastTierChangeTime >= PROXIMITY_DEBOUNCE_MS) {
                ProximityTier desiredTier = s_currentTier;

                switch (s_currentTier) {
                    case PROX_SUPER_CLOSE:
                        // Drop out of Rage only if signal falls below exit threshold (-62 dBm)
                        if (s_filteredRssi < (float)RSSI_RAGE_EXIT_THRESHOLD) {
                            desiredTier = (s_filteredRssi < (float)RSSI_ANGRY_EXIT_THRESHOLD) ? PROX_DISTANT : PROX_NEAR;
                        }
                        break;

                    case PROX_NEAR:
                        if (s_filteredRssi >= (float)RSSI_RAGE_ENTER_THRESHOLD) {
                            desiredTier = PROX_SUPER_CLOSE; // Escalates to Rage
                        } else if (s_filteredRssi < (float)RSSI_ANGRY_EXIT_THRESHOLD) {
                            desiredTier = PROX_DISTANT;     // Drops to Curious
                        }
                        break;

                    case PROX_DISTANT:
                        if (s_filteredRssi >= (float)RSSI_RAGE_ENTER_THRESHOLD) {
                            desiredTier = PROX_SUPER_CLOSE; // Instant close jump to Rage
                        } else if (s_filteredRssi >= (float)RSSI_ANGRY_ENTER_THRESHOLD) {
                            desiredTier = PROX_NEAR;        // Moves to Angry
                        }
                        break;
                }

                if (desiredTier != s_currentTier) {
                    s_lastTier = s_currentTier;
                    s_currentTier = desiredTier;
                    s_lastTierChangeTime = now;

                    // Haptic escalation triggers
                    if (s_currentTier == PROX_SUPER_CLOSE) {
                        triggerHaptic(HAPTIC_RAGE);   // Aggressive triple-buzz on Rage
                    } else if (s_currentTier == PROX_NEAR && s_lastTier == PROX_DISTANT) {
                        triggerHaptic(HAPTIC_DOUBLE); // Double-buzz moving from Curious to Angry
                    }
                }
            }
        }

        s_glassesWereActive = true;

        // Apply corresponding emotion
        switch (s_currentTier) {
            case PROX_SUPER_CLOSE:
                setEmotion(EMOTE_RAGE);
                break;
            case PROX_NEAR:
                setEmotion(EMOTE_ANGRY);
                break;
            case PROX_DISTANT:
            default:
                setEmotion(EMOTE_CURIOUS);
                break;
        }
    } else {
        s_glassesWereActive = false;
        s_currentTier = PROX_DISTANT;
        s_lastTier = PROX_DISTANT;
        s_filteredRssi = -100.0f;
        s_lastTierChangeTime = 0;
        // No glasses currently detected
        // Calculate inactivity from the most recent activity (glasses seen or manual wake)
        uint32_t lastActivity = 0;
        if (lastSeen > 0 && lastSeen >= lastWakeActivityTime) {
            lastActivity = lastSeen;
        } else {
            lastActivity = lastWakeActivityTime;
        }

        uint32_t idleTime = 0;
        if (simulatedIdleMs > 0) {
            idleTime = simulatedIdleMs;
        } else if (lastActivity > 0 && now >= lastActivity) {
            idleTime = now - lastActivity;
        } else {
            idleTime = 0;
        }

        if (idleTime >= SLEEP_DEEP_TIMEOUT_MS) {
            // 1 minute (60s+) without glasses/interaction -> Real Sleep Mode
            setEmotion(EMOTE_SLEEPING);
        } else if (idleTime >= SLEEPY_TIMEOUT_MS) {
            // 30s - 60s without glasses/interaction -> Sleepy / Drowsy Mode
            setEmotion(EMOTE_SLEEPY);
        } else {
            // < 30s without glasses/interaction -> Happy Mode (White eyes)
            setEmotion(EMOTE_HAPPY);
        }
    }
}



// -------------------------------------------------------------
// Anti-Aliased Sub-Pixel Rotated Eye Renderer
// -------------------------------------------------------------
void renderSmoothEye(LGFX_Sprite &spr, float targetX, float targetY, float w, float h, float r, float angle, uint16_t color, uint16_t bgColor = TFT_BLACK) {
    if (w < 3.0f) w = 3.0f;
    if (h < 3.0f) h = 3.0f;
    if (r > w / 2.0f) r = w / 2.0f;
    if (r > h / 2.0f) r = h / 2.0f;

    spr.fillSprite(bgColor);

    int lx = (120 - (int)w) / 2;
    int ly = (120 - (int)h) / 2;
    spr.fillRoundRect(lx, ly, (int)w, (int)h, (int)r, color);

    spr.setPivot(60, 60);
    // Sub-pixel hardware rotation with smooth edge anti-aliasing matching canvas background
    spr.pushRotateZoomWithAA(&canvas, targetX, targetY, angle, 1.0f, 1.0f, bgColor);
}


// -------------------------------------------------------------
// Vector Z-Letter and Zzz Sleep Animation Renderer
// -------------------------------------------------------------
void drawVectorZ(float cx, float cy, float s, uint16_t color) {
    float hw = s * 0.38f;
    float hh = s * 0.48f;
    canvas.drawWideLine(cx - hw, cy + hh, cx + hw, cy + hh, 2.0f, color);
    canvas.drawWideLine(cx + hw, cy + hh, cx - hw, cy - hh, 2.0f, color);
    canvas.drawWideLine(cx - hw, cy - hh, cx + hw, cy - hh, 2.0f, color);
}

void renderZzzAnimation(uint32_t now) {
    if (currentEmotion != EMOTE_SLEEPING) return;

    for (int i = 0; i < 3; i++) {
        uint32_t cycle = 2600;
        uint32_t offset = i * 860;
        float p = (float)((now + offset) % cycle) / (float)cycle;

        if (p < 0.04f) continue;

        // Floating trajectory drifting upwards into the open upper-right space
        float zX = (CX + 20.0f) + p * 38.0f + sinf(p * 5.0f + (float)i) * 5.0f;
        float zY = (CY - 12.0f) + p * 64.0f; // drifts upward towards physical top (+Y)
        float size = 8.0f + (float)i * 4.5f;

        float alpha = 1.0f;
        if (p < 0.2f) {
            alpha = p / 0.2f;
        } else if (p > 0.75f) {
            alpha = (1.0f - p) / 0.25f;
        }

        if (alpha > 0.08f) {
            uint8_t bright = (uint8_t)(255.0f * alpha);
            uint16_t zColor = colorRGB(bright, (uint8_t)(bright * 0.95f), bright);
            drawVectorZ(zX, zY, size, zColor);
        }
    }
}

// -------------------------------------------------------------
// Instant Boot / Wake-up Animation (Companion directly opens eyes)
// -------------------------------------------------------------
void playBootAnimation() {
    // 1. Clear physical screen to deep black instantly
    lcd.fillScreen(TFT_BLACK);

    // 2. Wake-up Eye Opening Animation (700ms)
    // Eyes smoothly expand from narrow sleeping slits into full, bright happy eyes
    uint32_t start = millis();
    while (millis() - start < 700) {
        float progress = (float)(millis() - start) / 700.0f;
        canvas.fillScreen(TFT_BLACK);

        float openProgress;
        if (progress < 0.35f) {
            // Slits gently waking
            openProgress = (progress / 0.35f) * 0.25f;
        } else if (progress < 0.5f) {
            // Drowsy blink
            float p = (progress - 0.35f) / 0.15f;
            openProgress = 0.25f - sinf(p * 3.14159f) * 0.15f;
        } else {
            // Pop wide open with smooth ease-out
            float p = (progress - 0.5f) / 0.5f;
            openProgress = 0.25f + 0.75f * (1.0f - powf(1.0f - p, 3.0f));
        }

        float curH = 4.0f + (tgtL.h - 4.0f) * openProgress;
        if (curH < 4.0f) curH = 4.0f;

        renderSmoothEye(leftEye, tgtL.x, tgtL.y, tgtL.w, curH, tgtL.r, tgtL.angle, TFT_WHITE);
        renderSmoothEye(rightEye, tgtR.x, tgtR.y, tgtR.w, curH, tgtR.r, tgtR.angle, TFT_WHITE);

        canvas.drawCircle(CX, CY, 119, 0x18C3);

        lcd.waitDMA();
        canvas.pushSprite(0, 0);
        delay(16);
    }
}

void setup() {


    Serial.begin(115200);

    // Initialize GC9A01 Display via LovyanGFX immediately
    lcd.init();
    lcd.setRotation(2); // Rotated 180 degrees
    lcd.setBrightness(255);
    lcd.fillScreen(TFT_BLACK);

    canvas.setColorDepth(16);
    canvas.createSprite(240, 240);

    leftEye.setColorDepth(16);
    leftEye.createSprite(120, 120);

    rightEye.setColorDepth(16);
    rightEye.createSprite(120, 120);

    // Initial eye state (forced so tgtL and tgtR are populated instantly)
    setEmotion(EMOTE_HAPPY, true);
    curL = tgtL;
    curR = tgtR;

    // Play instant boot / wake-up animation
    playBootAnimation();

    // Initialize ADXL345 Accelerometer for tap / double-tap detection
    initADXL345();

    // Initialize Vibration Motor for haptic feedback
    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);

    Serial.println("=================================================");
    Serial.println("  Smart Glasses Detecting Companion (30 FPS)    ");
    Serial.println("  - Red Eyes (Angry): Glasses Nearby (RSSI>=-75) ");
    Serial.println("  - Yellow Eyes (Curious): Distant Glasses      ");
    Serial.println("  - White Eyes (Happy): No Glasses (<30s)        ");
    Serial.println("  - White Eyes (Sleepy): No Glasses (>=30s)      ");
    Serial.println("  - Double Tap: Wakes up dazed -> Happy (Normal) ");
    Serial.println("  - Haptic Motor: Vibration on glasses (GPIO 4)  ");
    Serial.println("  - Test Device: Nothing Ear (2) Included       ");
    Serial.println("=================================================");

    nextBlinkTime = millis() + random(2000, 4000);
    nextSaccadeTime = millis() + random(1500, 3000);
    lastEmotionChange = millis();
    lastFrameTime = millis();
    lastWakeActivityTime = millis();

    // Start background BLE scanner task on Core 0
    xTaskCreatePinnedToCore(
        bleScanTask,     // Task function
        "BLE_Scan_Task", // Name
        4096,            // Stack size in words
        NULL,            // Parameters
        1,               // Priority
        NULL,            // Task handle
        0                // Core 0 (Graphics on Core 1)
    );

    Serial.println("Companion engine ready!");
}


void loop() {
    uint32_t frameStart = millis();

    uint32_t now = millis();
    float dt = (now - lastFrameTime) * 0.001f;
    if (dt > 0.05f) dt = 0.05f; // Clamp delta time
    lastFrameTime = now;

    // Update non-blocking haptic motor pulses
    updateHaptic();

    // Check for interactive Serial commands
    processSerialCommands();

    // Check for physical double-tap interrupt from ADXL345
    if (g_doubleTapDetected) {
        g_doubleTapDetected = false;
        if (g_adxlAvailable) {
            // Read INT_SOURCE register to clear ADXL345 latch and verify double-tap bit
            uint8_t source = adxlReadRegister(ADXL_INT_SOURCE);
            if (source & 0x20) { // Bit 5 = DOUBLE_TAP
                triggerDoubleTap("ADXL345 Interrupt");
            }
        } else {
            triggerDoubleTap("Hardware Interrupt");
        }
    }

    // Update companion emotion based on smart glasses proximity
    updateEmotionState();

    // ---------------------------------------------------------
    // Procedural Organic Saccades & Dazed Looking Around Sequence
    // ---------------------------------------------------------
    if (currentEmotion == EMOTE_DAZED && dazedEndTime > now) {
        uint32_t elapsed = (now >= dazedStartTime) ? (now - dazedStartTime) : 0;

        // Choreographed searching sequence: Center flutter blinks -> Look Left -> Look Right -> Return Center
        if (elapsed < 2000) {
            // Stage 1: Just woke up, center flutter blinks and droopy tired eyes
            saccadeX = sinf(now * 0.006f) * 2.0f;
            saccadeY = -2.0f;
            tgtL.w = 48.0f; tgtL.h = 22.0f; tgtL.r = 11.0f; tgtL.angle = -3.0f;
            tgtR.w = 48.0f; tgtR.h = 22.0f; tgtR.r = 11.0f; tgtR.angle =  3.0f;
        } else if (elapsed < 3800) {
            // Stage 2: Both eyes look Left together ("Who was that on the left?")
            saccadeX = -26.0f + sinf(now * 0.004f) * 2.0f;
            saccadeY = 6.0f;
            tgtL.w = 48.0f; tgtL.h = 24.0f; tgtL.r = 12.0f; tgtL.angle = -12.0f;
            tgtR.w = 48.0f; tgtR.h = 24.0f; tgtR.r = 12.0f; tgtR.angle = -12.0f;
        } else if (elapsed < 5600) {
            // Stage 3: Both eyes look Right together ("Or was it on the right?")
            saccadeX = 26.0f + sinf(now * 0.004f) * 2.0f;
            saccadeY = 6.0f;
            tgtL.w = 48.0f; tgtL.h = 24.0f; tgtL.r = 12.0f; tgtL.angle = 12.0f;
            tgtR.w = 48.0f; tgtR.h = 24.0f; tgtR.r = 12.0f; tgtR.angle = 12.0f;
        } else {
            // Stage 4: Return to center, symmetrical widening as alertness returns
            saccadeX = 0.0f;
            saccadeY = 2.0f;
            tgtL.w = 38.0f; tgtL.h = 42.0f; tgtL.r = 16.0f; tgtL.angle = -5.0f;
            tgtR.w = 38.0f; tgtR.h = 42.0f; tgtR.r = 16.0f; tgtR.angle = -5.0f;
        }
    }
    else if (now > nextSaccadeTime) {
        if (currentEmotion == EMOTE_CURIOUS) {
            // Curious multi-directional glancing with stable, solid capsule eyes
            int dir = random(0, 8);
            float offX = 0, offY = 0;
            float angle = -30.0f;

            switch (dir) {
                case 0: // Up-Right (Exact reference pose)
                    offX = 0.0f; offY = 0.0f; angle = -30.0f;
                    break;
                case 1: // Up-Left (Mirrored gaze)
                    offX = -56.0f; offY = 0.0f; angle = 30.0f;
                    break;
                case 2: // Right
                    offX = 6.0f; offY = -18.0f; angle = -20.0f;
                    break;
                case 3: // Left
                    offX = -56.0f; offY = -18.0f; angle = 20.0f;
                    break;
                case 4: // Up-Center
                    offX = -28.0f; offY = 8.0f; angle = 0.0f;
                    break;
                case 5: // Down-Right
                    offX = 6.0f; offY = -36.0f; angle = -15.0f;
                    break;
                case 6: // Down-Left
                    offX = -56.0f; offY = -36.0f; angle = 15.0f;
                    break;
                default: // Inquisitive Center
                    offX = -28.0f; offY = -14.0f; angle = -15.0f;
                    break;
            }
            saccadeX = offX;
            saccadeY = offY;
            tgtL.angle = angle;
            tgtR.angle = angle;
            nextSaccadeTime = now + random(1000, 2200);
        }
        else if (currentEmotion == EMOTE_HAPPY) {
            // Normal mode (WHITE eyes): Center micro-glances with occasional curious looks around
            int r = random(0, 10);
            if (r < 5) {
                // Occasional curious look around with matching directional tilt (White eyes)
                int dir = random(0, 6);
                float angle = -5.0f;
                switch (dir) {
                    case 0: saccadeX =  15.0f; saccadeY =  10.0f; angle = -16.0f; break; // Look Up-Right
                    case 1: saccadeX = -15.0f; saccadeY =  10.0f; angle =  16.0f; break; // Look Up-Left
                    case 2: saccadeX =  16.0f; saccadeY =   0.0f; angle = -10.0f; break; // Look Right
                    case 3: saccadeX = -16.0f; saccadeY =   0.0f; angle =  10.0f; break; // Look Left
                    case 4: saccadeX =   0.0f; saccadeY =  12.0f; angle =   0.0f; break; // Look Up
                    case 5: saccadeX =   0.0f; saccadeY =  -8.0f; angle =   0.0f; break; // Look Down
                }
                tgtL.angle = angle;
                tgtR.angle = angle;
                nextSaccadeTime = now + random(1400, 2600);
            } else {
                // Subtle center micro-glance
                saccadeX = random(-5, 6);
                saccadeY = random(-3, 4);
                tgtL.angle = -5.0f;
                tgtR.angle = -5.0f;
                nextSaccadeTime = now + random(1800, 3600);
            }
        }
        else if (currentEmotion == EMOTE_SLEEPY || currentEmotion == EMOTE_SLEEPING) {
            saccadeX = 0;
            saccadeY = 0;
            tgtL.angle = 0.0f;
            tgtR.angle = 0.0f;
            nextSaccadeTime = now + 4000;
        }
        else if (currentEmotion == EMOTE_RAGE) {
            saccadeX = 0;
            saccadeY = 0;
            tgtL.angle = 40.0f;
            tgtR.angle = -40.0f;
            nextSaccadeTime = now + 1000;
        }
        else { // ANGRY / other
            saccadeX = random(-3, 4);
            saccadeY = random(-2, 3);
            tgtL.angle = 45.0f;
            tgtR.angle = -45.0f;
            nextSaccadeTime = now + random(1500, 3000);
        }
    }

    // ---------------------------------------------------------
    // Procedural Blinking Engine
    // ---------------------------------------------------------
    float blinkScale = 1.0f;
    if (currentEmotion == EMOTE_SLEEPING) {
        // Deep sleep mode -> Eyes are peaceful closed slits, zero blinking
        isBlinking = false;
        blinkScale = 1.0f;
    } else if (currentEmotion == EMOTE_DAZED && dazedEndTime > now) {
        // Dedicated Groggy Wake-up Flutter Blinking Engine
        isBlinking = false;
        uint32_t elapsed = (now >= dazedStartTime) ? (now - dazedStartTime) : 0;

        if (elapsed < 120) {
            // Closed slit, registering wakeup
            blinkScale = 0.05f;
        } else if (elapsed < 310) {
            // Flutter Blink #1: Groggy peek opening (peak 0.45)
            float t = (float)(elapsed - 120) / 190.0f;
            blinkScale = 0.05f + 0.40f * sinf(t * 1.570796f);
        } else if (elapsed < 500) {
            // Flutter Blink #1: Eyelid droop closing
            float t = (float)(elapsed - 310) / 190.0f;
            blinkScale = 0.05f + 0.40f * cosf(t * 1.570796f);
        } else if (elapsed < 700) {
            // Groggy closed pause (eyes closed, registering wakeup)
            blinkScale = 0.05f;
        } else if (elapsed < 950) {
            // Flutter Blink #2: Wider groggy flutter (peak 0.75)
            float t = (float)(elapsed - 700) / 250.0f;
            blinkScale = 0.05f + 0.70f * sinf(t * 1.570796f);
        } else if (elapsed < 1200) {
            // Flutter Blink #2: Eyelid droop closing
            float t = (float)(elapsed - 950) / 250.0f;
            blinkScale = 0.05f + 0.70f * cosf(t * 1.570796f);
        } else if (elapsed < 1400) {
            // Brief closed pause
            blinkScale = 0.05f;
        } else if (elapsed < 2000) {
            // Eyelids smoothly open to 100% into droopy tired eyes
            float t = (float)(elapsed - 1400) / 600.0f;
            blinkScale = 0.05f + 0.95f * sinf(t * 1.570796f);
        } else if (elapsed >= 2850 && elapsed < 3150) {
            // Slow natural blink while looking Left
            float t = (float)(elapsed - 2850) / 300.0f;
            blinkScale = 0.5f + 0.5f * cosf(t * 6.283185f);
            if (blinkScale < 0.05f) blinkScale = 0.05f;
        } else if (elapsed >= 4650 && elapsed < 4950) {
            // Slow natural blink while looking Right
            float t = (float)(elapsed - 4650) / 300.0f;
            blinkScale = 0.5f + 0.5f * cosf(t * 6.283185f);
            if (blinkScale < 0.05f) blinkScale = 0.05f;
        } else if (elapsed >= 6400 && elapsed < 6700) {
            // Clearing blink before fully opening
            float t = (float)(elapsed - 6400) / 300.0f;
            blinkScale = 0.5f + 0.5f * cosf(t * 6.283185f);
            if (blinkScale < 0.05f) blinkScale = 0.05f;
        } else {
            blinkScale = 1.0f;
        }
    } else {
        if (!isBlinking && now > nextBlinkTime) {
            isBlinking = true;
            blinkStartTime = now;
        }

        if (isBlinking) {
            uint32_t closeTime = (currentEmotion == EMOTE_SLEEPY) ? 140 : 85;
            uint32_t openTime  = (currentEmotion == EMOTE_SLEEPY) ? 280 : 170;
            uint32_t blinkProgress = now - blinkStartTime;

            if (blinkProgress < closeTime) {
                // Eyelids closing
                blinkScale = 1.0f - ((float)blinkProgress / (float)closeTime);
            } else if (blinkProgress < openTime) {
                // Eyelids opening
                blinkScale = ((float)(blinkProgress - closeTime) / (float)(openTime - closeTime));
            } else {
                // Blink complete
                isBlinking = false;
                nextBlinkTime = now + ((currentEmotion == EMOTE_SLEEPY) ? random(1200, 2500) : random(2500, 5500));
                blinkScale = 1.0f;
            }
        }
    }


    // Organic idle breathing / gentle head-bob
    float breathY = sin(now * 0.003f) * 2.5f;
    if (currentEmotion == EMOTE_DAZED) {
        breathY += sinf(now * 0.006f) * 2.5f; // Drowsy sluggish head sway
    }

    // ---------------------------------------------------------
    // Rapid Rage Tremor & Shaking Engine
    // ---------------------------------------------------------
    float rageShakeX = 0;
    float rageShakeY = 0;
    float rageShakeAngle = 0;
    if (currentEmotion == EMOTE_RAGE) {
        // High-frequency procedural tremor (rapid violent eye shaking)
        rageShakeX = sinf(now * 0.055f) * 2.8f + (float)random(-2, 3);
        rageShakeY = cosf(now * 0.065f) * 2.2f + (float)random(-2, 3);
        rageShakeAngle = (float)random(-3, 4); // +/- 3 degrees micro-jitter
    }

    // ---------------------------------------------------------
    // Framerate-Independent Spring Interpolation & BG Color Fade
    // ---------------------------------------------------------
    float factor = 1.0f - expf(-12.0f * dt); // Smooth organic easing

    // Smooth background fade: Black <-> Crimson Red #BA3636 rgb(186, 54, 54)
    uint16_t tgtBgColor = (currentEmotion == EMOTE_RAGE) ? rgb565(186, 54, 54) : TFT_BLACK;
    static uint16_t s_curBgColor = TFT_BLACK;
    s_curBgColor = lerpColor565(s_curBgColor, tgtBgColor, factor * 0.8f);

    curL.x += ((tgtL.x + saccadeX) - curL.x) * factor;
    curL.y += ((tgtL.y + saccadeY + breathY) - curL.y) * factor;
    curL.w += (tgtL.w - curL.w) * factor;
    curL.h += (tgtL.h - curL.h) * factor;
    curL.r += (tgtL.r - curL.r) * factor;
    curL.angle += (tgtL.angle - curL.angle) * factor;
    curL.color = lerpColor565(curL.color, tgtL.color, factor * 1.5f);

    curR.x += ((tgtR.x + saccadeX) - curR.x) * factor;
    curR.y += ((tgtR.y + saccadeY + breathY) - curR.y) * factor;
    curR.w += (tgtR.w - curR.w) * factor;
    curR.h += (tgtR.h - curR.h) * factor;
    curR.r += (tgtR.r - curR.r) * factor;
    curR.angle += (tgtR.angle - curR.angle) * factor;
    curR.color = lerpColor565(curR.color, tgtR.color, factor * 1.5f);

    // Wait for DMA transfer to complete before rendering next frame
    lcd.waitDMA();

    // Clear canvas with smoothly fading dynamic background
    canvas.fillScreen(s_curBgColor);

    // Apply blink scaling
    float renderHL = curL.h * blinkScale;
    float renderHR = curR.h * blinkScale;
    if (renderHL < 3.0f) renderHL = 3.0f;
    if (renderHR < 3.0f) renderHR = 3.0f;

    // Apply rage tremor directly to rendered positions for crisp high-frequency vibration
    float renderXL = curL.x + rageShakeX;
    float renderYL = curL.y + rageShakeY;
    float renderXR = curR.x + rageShakeX;
    float renderYR = curR.y + rageShakeY;
    float renderAngleL = curL.angle + rageShakeAngle;
    float renderAngleR = curR.angle - rageShakeAngle;

    // Render Left and Right Eyes with smooth sub-pixel rotation & anti-aliasing matching background
    renderSmoothEye(leftEye, renderXL, renderYL, curL.w, renderHL, curL.r, renderAngleL, curL.color, s_curBgColor);
    renderSmoothEye(rightEye, renderXR, renderYR, curR.w, renderHR, curR.r, renderAngleR, curR.color, s_curBgColor);

    // Render floating Zzz animation during sleep mode
    renderZzzAnimation(now);

    // Subtle edge framing
    uint16_t ringColor = (s_curBgColor == TFT_BLACK) ? 0x18C3 : lerpColor565(0x18C3, rgb565(130, 25, 25), 0.8f);
    canvas.drawCircle(CX, CY, 119, ringColor);

    // Push full frame over 40MHz DMA instantly
    canvas.pushSprite(0, 0);

    // ---------------------------------------------------------
    // Precise 30 FPS Frame Pacer (33.3ms per frame)
    // ---------------------------------------------------------
    const uint32_t TARGET_FRAME_TIME_MS = 33;
    uint32_t frameElapsed = millis() - frameStart;
    if (frameElapsed < TARGET_FRAME_TIME_MS) {
        delay(TARGET_FRAME_TIME_MS - frameElapsed);
    }
}
