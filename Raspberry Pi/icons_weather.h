#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include "canvas.h"

// Each icon is 16x16 RGB (24 bit) stored tightly: size = 16*16*3 = 768 bytes
struct Icon16 {
    uint8_t pixels[16 * 16 * 3];
};

// Call once at startup. Returns true on success.
bool InitIcons();

// Draw a weather condition icon (falls back to "unknown")
// (x,y) is top-left on the target canvas. Safe if partially off-screen (clipped manually)
void DrawWeatherIcon(const std::string &condition,
                     rgb_matrix::Canvas *canvas,
                     int x, int y);

// Generic testing icons
void DrawPlayIcon(rgb_matrix::Canvas *canvas, int x, int y);
void DrawPauseIcon(rgb_matrix::Canvas *canvas, int x, int y);

// if you want custom composition with an actual icon, retrieve the raw icon pointer 
// returns nullptr if name unknown.
const Icon16* GetIconByName(const std::string &name);
