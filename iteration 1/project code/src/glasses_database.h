#ifndef GLASSES_DATABASE_H
#define GLASSES_DATABASE_H

#include <Arduino.h>
#include <stdint.h>

struct GlassesCompanyID {
    uint16_t    id;
    const char* company;
    const char* product;
};

// Bluetooth SIG Assigned Company Identifiers
static const GlassesCompanyID TARGET_COMPANY_IDS[] = {
    // --- Smart Glasses & AR Eyewear ---
    { 0x01AB, "Meta Platforms",              "Ray-Ban Stories / Meta" },
    { 0x058E, "Meta Platforms Tech",         "Ray-Ban Meta / Quest" },
    { 0x0D53, "Luxottica Group",             "Ray-Ban Meta / Oakley" },
    { 0x03C2, "Snapchat Inc",                "Snap Spectacles" },
    { 0x060C, "Vuzix Corporation",           "Vuzix Blade / Shield" },
    { 0x0BC6, "TCL Communication",           "RayNeo X2 / Air" },
    { 0x00E0, "Google",                      "Google Glass" },
    { 0x018E, "Google LLC",                  "Google Glass EE2" },
    { 0x0171, "Amazon",                      "Echo Frames" },
    { 0x009E, "Bose Corporation",            "Bose Frames" },
    { 0x0444, "Razer Inc.",                  "Razer Anzu" },
    { 0x0A5C, "Brilliant Labs",              "Monocle / Frame" },

    // --- User Test Device: Nothing Ear series ---
    { 0x0CCA, "Nothing Technology",          "Nothing Ear (2) / Audio" },
};

static const size_t TARGET_COMPANY_COUNT = sizeof(TARGET_COMPANY_IDS) / sizeof(TARGET_COMPANY_IDS[0]);

// Case-insensitive substring patterns for advertised device names
static const char* TARGET_NAME_PATTERNS[] = {
    "ray-ban",
    "rayban",
    "stories",
    "meta view",
    "spectacles",
    "echo frames",
    "vuzix",
    "rayneo",
    "rokid",
    "xreal",
    "nreal",
    "even realities",
    "g1",
    "monocle",
    "brilliant",
    "solos",
    "airgo",
    "lucyd",
    "anzu",
    "smart glasses",
    "smart eyewear",
    "inmo",
    "vision pro",
    // Test device: Nothing Ear (2)
    "nothing ear",
    "ear (2)",
    "ear 2",
    "ear (1)",
    "ear (a)",
    "ear (open)",
    "ear (stick)"
};

static const size_t TARGET_NAME_PATTERN_COUNT = sizeof(TARGET_NAME_PATTERNS) / sizeof(TARGET_NAME_PATTERNS[0]);

#endif // GLASSES_DATABASE_H
