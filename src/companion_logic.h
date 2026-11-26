#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>

inline bool deadlineReached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

constexpr uint32_t GLASSES_ACTIVE_TIMEOUT_MS = 1500;
constexpr uint32_t SLEEPY_TIMEOUT_MS = 30000;
constexpr uint32_t SLEEP_DEEP_TIMEOUT_MS = 60000;
constexpr uint32_t DAZED_DURATION_MS = 7500;
constexpr uint32_t SIMULATION_DURATION_MS = 10000;

struct IdleClock {
    uint32_t idleMs = 0;
    uint32_t updatedAt = 0;
    void activity(uint32_t now) { idleMs = 0; updatedAt = now; }
    uint32_t tick(uint32_t now) {
        uint32_t delta = now - updatedAt;
        idleMs += delta < SLEEP_DEEP_TIMEOUT_MS - idleMs ? delta : SLEEP_DEEP_TIMEOUT_MS - idleMs;
        updatedAt = now;
        return idleMs;
    }
};

enum ProximityTier { PROX_DISTANT, PROX_NEAR, PROX_SUPER_CLOSE };

struct DetectionSample {
    ProximityTier tier = PROX_DISTANT;
    bool valid = false;
    bool testDevice = false;
    uint8_t address[7] = {}; // Six address bytes and the BLE address type.
    uint32_t lastSeenMs = 0;
    uint32_t sequence = 0;
    float rssi = -100;
    char name[64] = {};
    char reason[64] = {};
};

struct ProximityState {
    ProximityTier tier = PROX_DISTANT;

    // Called once for each received sample, never by the frame renderer.
    void observe(float rssi) {
        ProximityTier desired = tier;
        if (tier == PROX_SUPER_CLOSE) {
            if (rssi < -62) desired = rssi < -78 ? PROX_DISTANT : PROX_NEAR;
        } else if (tier == PROX_NEAR) {
            if (rssi >= -55) desired = PROX_SUPER_CLOSE;
            else if (rssi < -78) desired = PROX_DISTANT;
        } else {
            if (rssi >= -55) desired = PROX_SUPER_CLOSE;
            else if (rssi >= -73) desired = PROX_NEAR;
        }
        tier = desired;
    }
};

struct DetectionTracker {
    static constexpr size_t CAPACITY = 8;
    struct Device {
        DetectionSample sample;
        ProximityState proximity;
        int samples[3] = {};
        uint8_t nextSample = 0;
    };
    Device devices[CAPACITY] = {};
    IdleClock idle;
    uint32_t nextSequence = 0;

    void observe(const uint8_t address[7], int rssi, const char* name,
                 const char* reason, bool testDevice, uint32_t now) {
        if (rssi < -127 || rssi > 20) return;
        Device* record = nullptr;
        for (auto& device : devices) {
            if (device.sample.valid && memcmp(device.sample.address, address, 7) == 0) {
                record = &device;
                break;
            }
        }
        if (!record) {
            record = &devices[0];
            for (auto& device : devices) {
                if (!device.sample.valid) { record = &device; break; }
                if (now - device.sample.lastSeenMs > now - record->sample.lastSeenMs) record = &device;
            }
            *record = {};
        }
        DetectionSample* slot = &record->sample;
        bool continuing = slot->valid && now - slot->lastSeenMs < GLASSES_ACTIVE_TIMEOUT_MS;
        if (!continuing) {
            record->proximity = {};
            for (int& sample : record->samples) sample = rssi;
        }
        record->samples[record->nextSample] = rssi;
        record->nextSample = (record->nextSample + 1) % 3;
        // Reject one isolated spike without imposing a time-based proximity delay.
        int window[3];
        memcpy(window, record->samples, sizeof(window));
        std::sort(window, window + 3);
        slot->rssi = (float)window[1];
        slot->valid = true;
        slot->testDevice = testDevice;
        memcpy(slot->address, address, 7);
        slot->lastSeenMs = now;
        slot->sequence = ++nextSequence;
        snprintf(slot->name, sizeof(slot->name), "%s", name);
        snprintf(slot->reason, sizeof(slot->reason), "%s", reason);
        record->proximity.observe(slot->rssi);
        slot->tier = record->proximity.tier;
        idle.activity(now);
    }

    DetectionSample strongest(uint32_t now) const {
        DetectionSample best;
        for (const auto& record : devices) {
            const auto& device = record.sample;
            if (device.valid && now - device.lastSeenMs < GLASSES_ACTIVE_TIMEOUT_MS &&
                (!best.valid || device.rssi > best.rssi)) best = device;
        }
        return best;
    }

    void removeTestDevices() {
        for (auto& device : devices) if (device.sample.testDevice) device = {};
    }
};

enum HapticPattern { HAPTIC_NONE, HAPTIC_SINGLE, HAPTIC_DOUBLE, HAPTIC_RAGE };

struct HapticSequence {
    HapticPattern pattern = HAPTIC_NONE;
    uint8_t step = 0;
    uint32_t deadline = 0;
    bool output = false;

    uint32_t duration() const {
        static const uint16_t lengths[3][5] = {{120,0,0,0,0}, {75,65,75,0,0}, {60,40,60,40,90}};
        return lengths[pattern - 1][step];
    }
    void begin(HapticPattern next, uint32_t now) {
        pattern = next;
        step = 0;
        output = pattern != HAPTIC_NONE;
        if (output) deadline = now + duration();
    }
    void advance(uint32_t now) {
        static const uint8_t counts[] = {0,1,3,5};
        while (pattern != HAPTIC_NONE && deadlineReached(now, deadline)) {
            if (++step >= counts[pattern]) { pattern = HAPTIC_NONE; output = false; break; }
            output = (step % 2) == 0;
            deadline += duration();
        }
    }
};

inline uint16_t colorRGB(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}

inline uint16_t lerpColor565(uint16_t from, uint16_t to, float factor) {
    if (factor <= 0) return from;
    if (factor >= 1) return to;
    int result = 0;
    const int shifts[] = {11,5,0}, masks[] = {31,63,31};
    for (int i = 0; i < 3; ++i) {
        int a = (from >> shifts[i]) & masks[i], b = (to >> shifts[i]) & masks[i];
        int value = (int)roundf(a + (b - a) * factor);
        // Guarantee progress when RGB565 rounding would otherwise stop the fade.
        if (value == a && a != b) value += b > a ? 1 : -1;
        result |= value << shifts[i];
    }
    return (uint16_t)result;
}

struct CommandBuffer {
    char text[64] = {};
    size_t length = 0;
    bool overflow = false;
    bool push(char c) {
        if (c == '\r') return false;
        if (c == '\n') {
            text[length] = 0;
            bool ready = !overflow && length > 0;
            length = 0;
            overflow = false;
            return ready;
        }
        if (length < sizeof(text) - 1) text[length++] = c;
        else overflow = true;
        return false;
    }
};
