#pragma once
#include <cstdint>
#include <vector>

// A buddy color as the game stores it. Per the disassembly of 0x54b230
// (the default-color AddBuddy path), the game uses 0x437f0000 = 255.0f
// in the G channel. So colors are stored as 0..255 floats, not 0..1.
struct BuddyColor {
    float r;
    float g;
    float b;
};

// Pack an RGB triple (0..255 ints) into a 24-bit hex code 0xRRGGBB.
inline uint32_t packRGB(int r, int g, int b) {
    return (uint32_t)((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

// Get the preset color list configured in bfvplus.ini.
// Values are 24-bit 0xRRGGBB.
const std::vector<uint32_t>& getPresetColors();

// Install the AddBuddy button hook.
void installAddBuddyHook();
