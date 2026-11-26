#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <TFT_eSPI.h>
#include "companion_logic.h"
#include "eye_renderer.h"
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
portMUX_TYPE g_sensorMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t g_sensorAttempts = 0;
uint32_t g_lastSensorAttemptMs = 0;
uint32_t g_lastSensorHealthMs = 0;

void IRAM_ATTR handleDoubleTapISR() {
    portENTER_CRITICAL_ISR(&g_sensorMux);
    g_doubleTapDetected = true;
    portEXIT_CRITICAL_ISR(&g_sensorMux);
}

bool adxlReadRegister(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)ADXL345_ADDR, (uint8_t)1) != 1) return false;
    value = (uint8_t)Wire.read();
    return true;
}

bool adxlWriteRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(reg);
    Wire.write(value);
    if (Wire.endTransmission() != 0) return false;
    uint8_t actual;
    return adxlReadRegister(reg, actual) && actual == value;
}

bool initADXL345() {
    g_adxlAvailable = false;
    g_lastSensorAttemptMs = millis();
    ++g_sensorAttempts;
    detachInterrupt(digitalPinToInterrupt(ADXL_INT1_PIN));
    uint8_t identity, source;
    bool configured = adxlReadRegister(0x00, identity) && identity == 0xE5 &&
        adxlWriteRegister(ADXL_INT_ENABLE, 0x00) &&
        adxlWriteRegister(ADXL_POWER_CTL, 0x00) &&
        adxlWriteRegister(ADXL_DATA_FORMAT, 0x0B) &&
        adxlWriteRegister(ADXL_THRESH_TAP, 0x20) &&
        adxlWriteRegister(ADXL_DUR, 0x10) &&
        adxlWriteRegister(ADXL_LATENT, 0x50) &&
        adxlWriteRegister(ADXL_WINDOW, 0xF0) &&
        adxlWriteRegister(ADXL_TAP_AXES, 0x07) &&
        adxlWriteRegister(ADXL_INT_MAP, 0x00) &&
        adxlReadRegister(ADXL_INT_SOURCE, source);
    if (configured) {
        pinMode(ADXL_INT1_PIN, INPUT);
        portENTER_CRITICAL(&g_sensorMux);
        g_doubleTapDetected = false;
        portEXIT_CRITICAL(&g_sensorMux);
        attachInterrupt(digitalPinToInterrupt(ADXL_INT1_PIN), handleDoubleTapISR, RISING);
        configured = adxlWriteRegister(ADXL_POWER_CTL, 0x08) && adxlWriteRegister(ADXL_INT_ENABLE, 0x20);
    }
    if (!configured) {
        detachInterrupt(digitalPinToInterrupt(ADXL_INT1_PIN));
        Serial.println("[ADXL345] Configuration failed; double tap unavailable. Use sensor to retry.");
        return false;
    }
    g_adxlAvailable = true;
    g_sensorAttempts = 0;
    g_lastSensorHealthMs = millis();
    Serial.println("[ADXL345] Device ID and tap configuration verified; sensor ready.");
    return true;
}

// -------------------------------------------------------------
// Vibration Motor Haptic Feedback Engine (GPIO 4)
// -------------------------------------------------------------
#define MOTOR_PIN 4

TaskHandle_t g_hapticTask = nullptr;
portMUX_TYPE g_motorMux = portMUX_INITIALIZER_UNLOCKED;
bool g_motorOn = false;
uint32_t g_motorStoppedMs = 0;
uint32_t g_hapticWorstLateMs = 0;

void hapticTask(void*) {
    HapticSequence sequence;
    uint32_t request = 0;
    for (;;) {
        uint32_t now = millis();
        uint32_t lateness = sequence.pattern != HAPTIC_NONE && deadlineReached(now, sequence.deadline) ? now - sequence.deadline : 0;
        sequence.advance(now);
        digitalWrite(MOTOR_PIN, sequence.output ? HIGH : LOW);
        portENTER_CRITICAL(&g_motorMux);
        if (lateness > g_hapticWorstLateMs) g_hapticWorstLateMs = lateness;
        if (g_motorOn && !sequence.output) g_motorStoppedMs = now;
        g_motorOn = sequence.output;
        portEXIT_CRITICAL(&g_motorMux);
        TickType_t wait = sequence.pattern == HAPTIC_NONE ? portMAX_DELAY :
                          pdMS_TO_TICKS(sequence.deadline - now);
        if (wait == 0) wait = 1;
        if (xTaskNotifyWait(0, UINT32_MAX, &request, wait) == pdTRUE) {
            sequence.begin((HapticPattern)request, millis());
        }
    }
}

void triggerHaptic(HapticPattern pattern) {
    if (g_hapticTask) xTaskNotify(g_hapticTask, (uint32_t)pattern, eSetValueWithOverwrite);
}

bool motorIsSettling(uint32_t now) {
    portENTER_CRITICAL(&g_motorMux);
    now = millis();
    bool settling = g_motorOn || now - g_motorStoppedMs < 150;
    portEXIT_CRITICAL(&g_motorMux);
    return settling;
}

// TFT_eSPI owns the physical SPI display and the software canvas.
TFT_eSPI lcd;
TFT_eSprite canvas(&lcd);

void pushFrame() {
    lcd.pushImage(0, 0, 240, 240, (uint16_t*)canvas.getPointer());
}

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
bool dazedActive = false;
bool simulationActive = false;
Emotion simulatedEmotion = EMOTE_HAPPY;
uint32_t simulationStartMs = 0;

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

// Procedural Blinking Engine
bool isBlinking = false;
uint32_t blinkStartTime = 0;
uint32_t nextBlinkTime = 0;

// Natural Saccade (organic glance micro-movements)
float saccadeX = 0, saccadeY = 0;
uint32_t nextSaccadeTime = 0;

uint32_t lastFrameTime = 0;

struct FrameTiming {
    uint32_t frames = 0;
    uint64_t totalUs = 0;
    uint64_t drawUs = 0;
    uint64_t pushUs = 0;
    uint32_t worstUs = 0;
    uint32_t overruns = 0;
} g_frameTiming;

void printFrameTiming() {
    if (g_frameTiming.frames == 0) return;
    float frames = (float)g_frameTiming.frames;
    Serial.printf("[PERF] %.1f FPS | draw %.2f ms | SPI %.2f ms | worst %.2f ms | late %u/%u | heap %u | min heap %u\n",
                  frames * 1000000.0 / g_frameTiming.totalUs,
                  g_frameTiming.drawUs / frames / 1000.0,
                  g_frameTiming.pushUs / frames / 1000.0,
                  g_frameTiming.worstUs / 1000.0,
                  (unsigned)g_frameTiming.overruns, (unsigned)g_frameTiming.frames,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
    g_frameTiming = {};
}

portMUX_TYPE g_detectionMux = portMUX_INITIALIZER_UNLOCKED;
DetectionTracker g_detection;
bool g_targetWasPresent = false;
ProximityTier g_lastProximityTier = PROX_DISTANT;
bool g_testDevicesEnabled = ENABLE_NOTHING_TEST_DEVICE;

DetectionSample detectionSnapshot() {
    portENTER_CRITICAL(&g_detectionMux);
    DetectionSample sample = g_detection.strongest(millis());
    portEXIT_CRITICAL(&g_detectionMux);
    return sample;
}

uint32_t idleDuration() {
    portENTER_CRITICAL(&g_detectionMux);
    uint32_t idle = g_detection.idle.tick(millis());
    portEXIT_CRITICAL(&g_detectionMux);
    return idle;
}

void markActivity(uint32_t now) {
    portENTER_CRITICAL(&g_detectionMux);
    g_detection.idle.activity(now);
    portEXIT_CRITICAL(&g_detectionMux);
}

void setEmotion(Emotion emo, bool force = false) {
    if (currentEmotion != emo || force) {

        currentEmotion = emo;
        saccadeX = saccadeY = 0;
        nextSaccadeTime = millis();

        switch (emo) {
            case EMOTE_RAGE: // Super close: Crimson background (#BA3636) with deep maroon eyes (#610000)
                tgtL = { CX - 35.0f, CY + 2.0f, 28.0f, 58.0f, 14.0f,  40.0f, colorRGB(97, 0, 0) };
                tgtR = { CX + 35.0f, CY + 2.0f, 28.0f, 58.0f, 14.0f, -40.0f, colorRGB(97, 0, 0) };
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
class GlassesAdvertisedDeviceCallbacks: public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        std::string name = advertisedDevice->haveName() ? advertisedDevice->getName() : "";
        std::string mfg = advertisedDevice->haveManufacturerData() ? advertisedDevice->getManufacturerData() : "";
        portENTER_CRITICAL(&g_detectionMux);
        bool testEnabled = g_testDevicesEnabled;
        portEXIT_CRITICAL(&g_detectionMux);
        DeviceMatch match = matchDevice(name.c_str(), (const uint8_t*)mfg.data(), mfg.size(), testEnabled);
        if (!match.matched) return;
        auto address = advertisedDevice->getAddress();
        uint8_t key[7];
        memcpy(key, address.getVal(), 6);
        key[6] = address.getType();
        uint32_t now = millis();
        portENTER_CRITICAL(&g_detectionMux);
        // A test-mode change can happen while this callback is classifying a packet.
        if (!match.testDevice || g_testDevicesEnabled) {
            g_detection.observe(key, advertisedDevice->getRSSI(), name.c_str(), match.reason, match.testDevice, now);
        }
        portEXIT_CRITICAL(&g_detectionMux);

    }
};

// -------------------------------------------------------------
// FreeRTOS Task for Background BLE Scanning (Pinned to Core 0)
// -------------------------------------------------------------
void bleScanTask(void*) {
    Serial.println("[BLE Task] Initializing NimBLE on Core 0...");
    bool initialized = false;
    for (int attempt = 0; attempt < 3 && !initialized; ++attempt) {
        initialized = NimBLEDevice::init("CompanionScanner");
        if (!initialized) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!initialized) {
        Serial.println("[BLE] Initialization failed after three attempts. Restart to retry.");
        vTaskDelete(nullptr);
        return;
    }
    NimBLEScan* pBLEScan = NimBLEDevice::getScan();
    static GlassesAdvertisedDeviceCallbacks callbacks;
    pBLEScan->setScanCallbacks(&callbacks, true); // wantDuplicates = true for real-time tracking
    pBLEScan->setActiveScan(true);  // Active scan for names & metadata
    pBLEScan->setInterval(100);     // Scan interval 100ms
    pBLEScan->setWindow(100);       // Scan window 100ms (100% continuous duty cycle = max range!)
    pBLEScan->setDuplicateFilter(false); // Process all advertisement packets
    pBLEScan->setMaxResults(0);     // Do not retain device list in RAM

    Serial.println("[BLE] Scanner ready. Company IDs alone are not product evidence.");

    while (true) {
        // Continuous scan in 1-second blocks; results cleared to prevent RAM leaks
        pBLEScan->getResults(1000, false);
        pBLEScan->clearResults();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// -------------------------------------------------------------
// Interactive Double-Tap Handler (ADXL345 & CLI)
// -------------------------------------------------------------
void triggerDoubleTap(const char* source = "Sensor") {
    if (currentEmotion != EMOTE_SLEEPING && currentEmotion != EMOTE_SLEEPY) return;
    uint32_t now = millis();
    simulationActive = false;
    dazedStartTime = now;
    dazedActive = true;
    markActivity(now);
    setEmotion(EMOTE_DAZED);
    isBlinking = false;
    Serial.printf("[TAP] Waking up (%s). Fresh proximity alerts can interrupt the wake sequence.\n", source);
}

void serviceADXL345(uint32_t now) {
    if (!g_adxlAvailable) {
        if (g_sensorAttempts < 3 && now - g_lastSensorAttemptMs >= 5000) initADXL345();
        return;
    }
    portENTER_CRITICAL(&g_sensorMux);
    bool pending = g_doubleTapDetected;
    g_doubleTapDetected = false;
    portEXIT_CRITICAL(&g_sensorMux);
    uint8_t source = 0, identity = 0;
    bool ok = true;
    if (pending) {
        ok = adxlReadRegister(ADXL_INT_SOURCE, source);
        if (ok && (source & 0x20) && !motorIsSettling(now)) triggerDoubleTap("ADXL345");
    }
    if (ok && now - g_lastSensorHealthMs >= 5000) {
        g_lastSensorHealthMs = now;
        ok = adxlReadRegister(0x00, identity) && identity == 0xE5;
    }
    if (!ok) {
        g_adxlAvailable = false;
        g_sensorAttempts = 0;
        g_lastSensorAttemptMs = now;
        detachInterrupt(digitalPinToInterrupt(ADXL_INT1_PIN));
        Serial.println("[ADXL345] Read failed; scheduling bounded recovery.");
    }
}

void beginSimulation(Emotion emotion, HapticPattern haptic) {
    simulationActive = true;
    simulatedEmotion = emotion;
    simulationStartMs = millis();
    markActivity(simulationStartMs);
    dazedActive = false;
    g_targetWasPresent = false;
    g_lastProximityTier = PROX_DISTANT;
    if (haptic != HAPTIC_NONE) triggerHaptic(haptic);
    Serial.printf("[SIM] Emotion %d for 10 seconds. Use live to resume detection.\n", (int)emotion);
}

void handleCommand(const char* text) {
    String cmd(text);
    cmd.trim();
    cmd.toLowerCase();
    struct Command { const char* name; Emotion emotion; HapticPattern haptic; };
    static const Command simulations[] = {
        {"rage", EMOTE_RAGE, HAPTIC_RAGE}, {"superclose", EMOTE_RAGE, HAPTIC_RAGE},
        {"glasses_rage", EMOTE_RAGE, HAPTIC_RAGE}, {"rage_manual", EMOTE_RAGE, HAPTIC_NONE},
        {"near", EMOTE_ANGRY, HAPTIC_DOUBLE}, {"glasses_near", EMOTE_ANGRY, HAPTIC_DOUBLE},
        {"angry", EMOTE_ANGRY, HAPTIC_NONE}, {"far", EMOTE_CURIOUS, HAPTIC_SINGLE},
        {"glasses_far", EMOTE_CURIOUS, HAPTIC_SINGLE}, {"curious", EMOTE_CURIOUS, HAPTIC_NONE},
        {"none", EMOTE_HAPPY, HAPTIC_NONE}, {"glasses_none", EMOTE_HAPPY, HAPTIC_NONE},
        {"happy", EMOTE_HAPPY, HAPTIC_NONE}, {"sleepy", EMOTE_SLEEPY, HAPTIC_NONE},
        {"sleep", EMOTE_SLEEPING, HAPTIC_NONE}, {"sleeping", EMOTE_SLEEPING, HAPTIC_NONE},
        {"confused", EMOTE_CONFUSED, HAPTIC_NONE}
    };
    for (const auto& simulation : simulations) {
        if (cmd == simulation.name) { beginSimulation(simulation.emotion, simulation.haptic); return; }
    }
    uint32_t now = millis();
    if (cmd == "live") {
        simulationActive = false;
        dazedActive = false;
        markActivity(now);
        g_targetWasPresent = false;
        g_lastProximityTier = PROX_DISTANT;
        Serial.println("[SIM] Live detection resumed.");
    } else if (cmd == "tap" || cmd == "doubletap" || cmd == "double_tap") {
        triggerDoubleTap("Serial");
    } else if (cmd == "buzz" || cmd == "vibe" || cmd == "motor") {
        triggerHaptic(HAPTIC_DOUBLE);
    } else if (cmd == "stop") {
        triggerHaptic(HAPTIC_NONE);
    } else if (cmd == "sensor" || cmd == "r") {
        g_sensorAttempts = 0;
        initADXL345();
    } else if (cmd == "test on" || cmd == "test off") {
        portENTER_CRITICAL(&g_detectionMux);
        g_testDevicesEnabled = cmd == "test on";
        if (!g_testDevicesEnabled) g_detection.removeTestDevices();
        bool enabled = g_testDevicesEnabled;
        portEXIT_CRITICAL(&g_detectionMux);
        g_targetWasPresent = false;
        g_lastProximityTier = PROX_DISTANT;
        Serial.printf("[TEST] Nothing Ear matching %s.\n", enabled ? "enabled" : "disabled");
    } else if (cmd == "perf") {
        printFrameTiming();
    } else if (cmd == "status") {
        DetectionSample sample = detectionSnapshot();
        portENTER_CRITICAL(&g_detectionMux);
        bool testEnabled = g_testDevicesEnabled;
        portEXIT_CRITICAL(&g_detectionMux);
        Serial.printf("Emotion: %d | mode: %s | idle: %lu ms | sensor: %s | Nothing test: %s\n",
                      (int)currentEmotion, simulationActive ? "simulation (10s)" : "live",
                      (unsigned long)idleDuration(), g_adxlAvailable ? "OK" : "UNAVAILABLE",
                      testEnabled ? "ON" : "OFF");
        Serial.printf("Target: %s | filtered RSSI: %.1f dBm | age: %lu ms | tier: %d\n",
                      sample.valid ? sample.name : "none", sample.rssi,
                      sample.valid ? (unsigned long)(millis() - sample.lastSeenMs) : 0UL, (int)sample.tier);
        if (sample.valid) Serial.printf("Reason: %s\n", sample.reason);
        Serial.printf("BLE initialized: %s\n", NimBLEDevice::isInitialized() ? "YES" : "NO");
        portENTER_CRITICAL(&g_motorMux);
        bool motorOn = g_motorOn;
        uint32_t late = g_hapticWorstLateMs;
        portEXIT_CRITICAL(&g_motorMux);
        Serial.printf("Motor: %s | worst haptic timing delay: %lu ms\n", motorOn ? "ON" : "OFF", (unsigned long)late);
        uint32_t ink = 0;
        auto pixels = (const uint16_t*)canvas.getPointer();
        for (int i = 0; i < 240 * 240; ++i) if (pixels[i]) ++ink;
        Serial.printf("Canvas nonblack pixels: %lu\n", (unsigned long)ink);
    } else {
        Serial.println("Commands: status, perf, live, happy, sleepy, sleep, tap, far, near, rage, confused, buzz, stop, sensor, test on/off");
    }
}

void processSerialCommands() {
    static CommandBuffer command;
    // Limit work per frame; incomplete and oversized lines never block rendering.
    for (int i = 0; i < 32 && Serial.available() > 0; ++i) {
        if (command.push((char)Serial.read())) { handleCommand(command.text); break; }
    }
}

void updateEmotionState() {
    uint32_t now = millis();
    if (simulationActive) {
        if (now - simulationStartMs < SIMULATION_DURATION_MS) {
            setEmotion(simulatedEmotion);
            return;
        }
        simulationActive = false;
    }
    DetectionSample sample = detectionSnapshot();
    if (sample.valid) {
        if (!g_targetWasPresent || sample.tier > g_lastProximityTier) {
            triggerHaptic(sample.tier == PROX_SUPER_CLOSE ? HAPTIC_RAGE :
                          sample.tier == PROX_NEAR ? HAPTIC_DOUBLE : HAPTIC_SINGLE);
        }
        g_targetWasPresent = true;
        g_lastProximityTier = sample.tier;
        dazedActive = false;
        setEmotion(sample.tier == PROX_SUPER_CLOSE ? EMOTE_RAGE :
                   sample.tier == PROX_NEAR ? EMOTE_ANGRY : EMOTE_CURIOUS);
    } else if (dazedActive && now - dazedStartTime < DAZED_DURATION_MS) {
        g_targetWasPresent = false;
        g_lastProximityTier = PROX_DISTANT;
        setEmotion(EMOTE_DAZED);
    } else {
        g_targetWasPresent = false;
        g_lastProximityTier = PROX_DISTANT;
        dazedActive = false;
        uint32_t idle = idleDuration();
        setEmotion(idle >= SLEEP_DEEP_TIMEOUT_MS ? EMOTE_SLEEPING : idle >= SLEEPY_TIMEOUT_MS ? EMOTE_SLEEPY : EMOTE_HAPPY);
    }
}

// -------------------------------------------------------------
// Anti-Aliased Sub-Pixel Rotated Eye Renderer
// -------------------------------------------------------------
void renderSmoothEye(float x, float y, float w, float h, float r, float angle, uint16_t color) {
    drawRoundedEye((uint16_t*)canvas.getPointer(), 240, 240, x, y, w, h, r, angle, color);
}

// -------------------------------------------------------------
// Vector Z-Letter and Zzz Sleep Animation Renderer
// -------------------------------------------------------------
void drawVectorZ(float cx, float cy, float s, uint16_t color) {
    float hw = s * 0.38f;
    float hh = s * 0.48f;
    canvas.drawWideLine(cx - hw, cy + hh, cx + hw, cy + hh, 4.0f, color);
    canvas.drawWideLine(cx + hw, cy + hh, cx - hw, cy - hh, 4.0f, color);
    canvas.drawWideLine(cx - hw, cy - hh, cx + hw, cy - hh, 4.0f, color);
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
        canvas.fillSprite(TFT_BLACK);

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

        renderSmoothEye(tgtL.x, tgtL.y, tgtL.w, curH, tgtL.r, tgtL.angle, TFT_WHITE);
        renderSmoothEye(tgtR.x, tgtR.y, tgtR.w, curH, tgtR.r, tgtR.angle, TFT_WHITE);

        canvas.drawCircle(CX, CY, 119, 0x18C3);

        pushFrame();
        delay(16);
    }
}

void fatalSetup(const char* message) {
    digitalWrite(MOTOR_PIN, LOW);
    Serial.printf("[FATAL] %s; motor held off.\n", message);
    for (;;) delay(1000);
}

void setup() {


    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);
    Serial.begin(115200);

    // Initialize the physical GC9A01 display from platformio.ini.
    lcd.init();
    lcd.setRotation(2);
    // Preserve the verified mounting orientation used by the previous GC9A01 driver.
    lcd.writecommand(TFT_MADCTL);
    lcd.writedata(TFT_MAD_MY | TFT_MAD_BGR);
    lcd.invertDisplay(true);
    lcd.setSwapBytes(false);
    lcd.fillScreen(TFT_BLACK);

    canvas.setColorDepth(16);
    canvas.setAttribute(PSRAM_ENABLE, false);
    // TFT_eSprite uses calloc, which otherwise routes this large allocation to slow PSRAM.
    // Other application tasks start only after the allocation policy is restored.
    heap_caps_malloc_extmem_enable(SIZE_MAX);
    void* frameBuffer = canvas.createSprite(240, 240);
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
    if (!frameBuffer) fatalSetup("Display buffer allocation failed");

    // Initial eye state (forced so tgtL and tgtR are populated instantly)
    setEmotion(EMOTE_HAPPY, true);
    curL = tgtL;
    curR = tgtR;

    // Play instant boot / wake-up animation
    playBootAnimation();

    Wire.begin(ADXL_SDA_PIN, ADXL_SCL_PIN);
    Wire.setClock(400000);
    Wire.setTimeOut(5);
    initADXL345();

    if (xTaskCreatePinnedToCore(hapticTask, "Haptics", 2048, nullptr, 2, &g_hapticTask, 1) != pdPASS) {
        fatalSetup("Haptic task allocation failed");
    }

    Serial.println("=================================================");
    Serial.println("  Smart Glasses Detecting Companion (30 FPS)    ");
    Serial.println("  - Red Eyes (Angry): Glasses Nearby (RSSI>=-73) ");
    Serial.println("  - Yellow Eyes (Curious): Distant Glasses      ");
    Serial.println("  - White Eyes (Happy): No Glasses (<30s)        ");
    Serial.println("  - White Eyes (Sleepy): No Glasses (>=30s)      ");
    Serial.println("  - Double Tap: Wakes up dazed -> Happy (Normal) ");
    Serial.println("  - Haptic Motor: Vibration on glasses (GPIO 4)  ");
    Serial.println("  - Nothing test mode: use test on / test off   ");
    Serial.println("=================================================");

    nextBlinkTime = millis() + random(2000, 4000);
    nextSaccadeTime = millis() + random(1500, 3000);
    lastFrameTime = millis();
    markActivity(millis());

    // Start background BLE scanner task on Core 0
    if (xTaskCreatePinnedToCore(
        bleScanTask,     // Task function
        "BLE_Scan_Task", // Name
        4096,            // ESP-IDF task stack size in bytes
        NULL,            // Parameters
        1,               // Priority
        NULL,            // Task handle
        0                // Core 0 (Graphics on Core 1)
    ) != pdPASS) fatalSetup("BLE task allocation failed");

    Serial.println("Companion engine ready! Commands: help, status, perf");
}


void loop() {
    uint32_t frameStartUs = micros();

    uint32_t now = millis();
    float dt = (now - lastFrameTime) * 0.001f;
    if (dt > 0.05f) dt = 0.05f; // Clamp delta time
    lastFrameTime = now;


    // Check for interactive Serial commands
    processSerialCommands();

    serviceADXL345(millis());

    // Update companion emotion based on smart glasses proximity
    updateEmotionState();
    now = millis();
    static float animationPhase = 0;
    animationPhase = fmodf(animationPhase + dt, 6.283185307f);

    // ---------------------------------------------------------
    // Procedural Organic Saccades & Dazed Looking Around Sequence
    // ---------------------------------------------------------
    if (currentEmotion == EMOTE_DAZED && dazedActive && now - dazedStartTime < DAZED_DURATION_MS) {
        uint32_t elapsed = now - dazedStartTime;

        // Choreographed searching sequence: Center flutter blinks -> Look Left -> Look Right -> Return Center
        if (elapsed < 2000) {
            // Stage 1: Just woke up, center flutter blinks and droopy tired eyes
            saccadeX = sinf(animationPhase * 6.0f) * 2.0f;
            saccadeY = -2.0f;
            tgtL.w = 48.0f; tgtL.h = 22.0f; tgtL.r = 11.0f; tgtL.angle = -3.0f;
            tgtR.w = 48.0f; tgtR.h = 22.0f; tgtR.r = 11.0f; tgtR.angle =  3.0f;
        } else if (elapsed < 3800) {
            // Stage 2: Both eyes look Left together ("Who was that on the left?")
            saccadeX = -26.0f + sinf(animationPhase * 4.0f) * 2.0f;
            saccadeY = 6.0f;
            tgtL.w = 48.0f; tgtL.h = 24.0f; tgtL.r = 12.0f; tgtL.angle = -12.0f;
            tgtR.w = 48.0f; tgtR.h = 24.0f; tgtR.r = 12.0f; tgtR.angle = -12.0f;
        } else if (elapsed < 5600) {
            // Stage 3: Both eyes look Right together ("Or was it on the right?")
            saccadeX = 26.0f + sinf(animationPhase * 4.0f) * 2.0f;
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
    else if (deadlineReached(now, nextSaccadeTime)) {
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
        else if (currentEmotion == EMOTE_ANGRY) {
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
    } else if (currentEmotion == EMOTE_DAZED && dazedActive && now - dazedStartTime < DAZED_DURATION_MS) {
        // Dedicated Groggy Wake-up Flutter Blinking Engine
        isBlinking = false;
        uint32_t elapsed = now - dazedStartTime;

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
        if (!isBlinking && deadlineReached(now, nextBlinkTime)) {
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
    float breathY = sinf(animationPhase * 3.0f) * 2.5f;
    if (currentEmotion == EMOTE_DAZED) {
        breathY += sinf(animationPhase * 6.0f) * 2.5f; // Drowsy sluggish head sway
    }

    // ---------------------------------------------------------
    // Rapid Rage Tremor & Shaking Engine
    // ---------------------------------------------------------
    float rageShakeX = 0;
    float rageShakeY = 0;
    float rageShakeAngle = 0;
    if (currentEmotion == EMOTE_RAGE) {
        // High-frequency procedural tremor (rapid violent eye shaking)
        rageShakeX = sinf(animationPhase * 55.0f) * 2.8f + (float)random(-2, 3);
        rageShakeY = cosf(animationPhase * 65.0f) * 2.2f + (float)random(-2, 3);
        rageShakeAngle = (float)random(-3, 4); // +/- 3 degrees micro-jitter
    }

    // ---------------------------------------------------------
    // Framerate-Independent Spring Interpolation & BG Color Fade
    // ---------------------------------------------------------
    float factor = 1.0f - expf(-12.0f * dt); // Smooth organic easing

    // Smooth background fade: Black <-> Crimson Red #BA3636 rgb(186, 54, 54)
    uint16_t tgtBgColor = (currentEmotion == EMOTE_RAGE) ? colorRGB(186, 54, 54) : TFT_BLACK;
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


    uint32_t drawStartUs = micros();
    // Clear canvas with smoothly fading dynamic background
    canvas.fillSprite(s_curBgColor);

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
    renderSmoothEye(renderXL, renderYL, curL.w, renderHL, curL.r, renderAngleL, curL.color);
    renderSmoothEye(renderXR, renderYR, curR.w, renderHR, curR.r, renderAngleR, curR.color);

    // Render floating Zzz animation during sleep mode
    renderZzzAnimation(now);

    // Subtle edge framing
    float backgroundBlend = (float)((s_curBgColor >> 11) & 31) / ((colorRGB(186, 54, 54) >> 11) & 31);
    uint16_t ringColor = lerpColor565(0x18C3, colorRGB(130, 25, 25), backgroundBlend);
    canvas.drawCircle(CX, CY, 119, ringColor);

    uint32_t pushStartUs = micros();
    // Transfer the completed frame over the verified standard SPI path.
    pushFrame();
    uint32_t workEndUs = micros();

    // ---------------------------------------------------------
    // Precise 30 FPS Frame Pacer (33.3ms per frame)
    // ---------------------------------------------------------
    const uint32_t TARGET_FRAME_TIME_US = 1000000 / 30;
    uint32_t frameElapsedUs = micros() - frameStartUs;
    if (frameElapsedUs + 1000 < TARGET_FRAME_TIME_US) {
        delay((TARGET_FRAME_TIME_US - frameElapsedUs - 1000) / 1000);
    }
    frameElapsedUs = micros() - frameStartUs;
    if (frameElapsedUs < TARGET_FRAME_TIME_US) {
        delayMicroseconds(TARGET_FRAME_TIME_US - frameElapsedUs);
    }
    uint32_t frameUs = micros() - frameStartUs;
    g_frameTiming.frames++;
    g_frameTiming.totalUs += frameUs;
    g_frameTiming.drawUs += pushStartUs - drawStartUs;
    g_frameTiming.pushUs += workEndUs - pushStartUs;
    if (frameUs > g_frameTiming.worstUs) g_frameTiming.worstUs = frameUs;
    if (workEndUs - frameStartUs > TARGET_FRAME_TIME_US) g_frameTiming.overruns++;
}
