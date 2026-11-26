#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

struct GlassesCompanyID { uint16_t id; const char* company; };
// Company ownership from Bluetooth SIG Assigned Numbers; these are metadata, not product signatures.
static const GlassesCompanyID TARGET_COMPANY_IDS[] = {
    {0x01AB, "Meta Platforms"}, {0x058E, "Meta Platforms Technologies"},
    {0x0D53, "Luxottica"}, {0x03C2, "Snapchat"}, {0x060C, "Vuzix"},
    {0x0BC6, "TCL Communication"}, {0x00E0, "Google"}, {0x018E, "Google LLC"},
    {0x0171, "Amazon"}, {0x009E, "Bose"}, {0x0CCB, "Nothing Technology"}
};
static const char* TARGET_NAME_PATTERNS[] = {
    "ray-ban", "rayban", "spectacles", "echo frames", "vuzix", "rayneo",
    "rokid", "xreal", "nreal", "even realities", "brilliant frame", "brilliant monocle",
    "solos airgo", "lucyd", "razer anzu", "smart glasses", "smart eyewear", "inmo", "vision pro"
};
static const char* TEST_NAME_PATTERNS[] = {
    "nothing ear", "ear (2)", "ear 2", "ear (1)", "ear (a)", "ear (open)", "ear (stick)"
};

struct DeviceMatch { bool matched = false; bool testDevice = false; char reason[64] = {}; };

inline bool containsProductName(const char* name, const char* pattern) {
    for (const char* p = strstr(name, pattern); p; p = strstr(p + 1, pattern)) {
        size_t len = strlen(pattern);
        if ((p == name || !isalnum((unsigned char)p[-1])) && !isalnum((unsigned char)p[len])) return true;
    }
    return false;
}

inline DeviceMatch matchDevice(const char* name, const uint8_t* manufacturer, size_t size, bool allowTestDevices) {
    char lower[256];
    size_t n = 0;
    for (; name[n] && n < sizeof(lower) - 1; ++n) lower[n] = (char)tolower((unsigned char)name[n]);
    lower[n] = 0;
    DeviceMatch result;
    const char* pattern = nullptr;
    for (const char* candidate : TARGET_NAME_PATTERNS) {
        if (containsProductName(lower, candidate)) { pattern = candidate; break; }
    }
    if (!pattern && allowTestDevices) {
        for (const char* candidate : TEST_NAME_PATTERNS) {
            if (containsProductName(lower, candidate)) { pattern = candidate; result.testDevice = true; break; }
        }
    }
    if (!pattern) return result;
    result.matched = true;
    snprintf(result.reason, sizeof(result.reason), "%s name '%s'", result.testDevice ? "Test" : "Product", pattern);
    if (manufacturer && size >= 2) {
        uint16_t id = manufacturer[0] | ((uint16_t)manufacturer[1] << 8);
        for (const auto& company : TARGET_COMPANY_IDS) {
            if (company.id == id) {
                snprintf(result.reason, sizeof(result.reason), "%s name '%s'; mfg %04X %s",
                         result.testDevice ? "Test" : "Product", pattern, id, company.company);
                break;
            }
        }
    }
    return result;
}
