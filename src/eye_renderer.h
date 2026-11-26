#pragma once
#include <stdint.h>
#include <math.h>

inline uint16_t eyeSwap565(uint16_t value) { return (uint16_t)((value << 8) | (value >> 8)); }

// Draw directly into a byte-swapped RGB565 canvas, with a one-pixel anti-aliased edge.
inline void drawRoundedEye(uint16_t* pixels, int width, int height, float cx, float cy,
                           float w, float h, float radius, float degrees, uint16_t color) {
    if (!pixels || width <= 0 || height <= 0) return;
    w = fmaxf(3, w); h = fmaxf(3, h);
    radius = fmaxf(0, fminf(radius, fminf(w, h) * 0.5f));
    float radians = degrees * 0.01745329251994329577f;
    float c = cosf(radians), s = sinf(radians);
    float coreX = w * 0.5f - radius, coreY = h * 0.5f - radius;
    float extentX = fabsf(c) * coreX + fabsf(s) * coreY + radius + 1;
    float extentY = fabsf(s) * coreX + fabsf(c) * coreY + radius + 1;
    int x0 = (int)fmaxf(0, floorf(cx - extentX)), x1 = (int)fminf(width - 1, ceilf(cx + extentX));
    int y0 = (int)fmaxf(0, floorf(cy - extentY)), y1 = (int)fminf(height - 1, ceilf(cy + extentY));
    uint16_t solid = eyeSwap565(color);
    int red = (color >> 11) & 31, green = (color >> 5) & 63, blue = color & 31;
    for (int y = y0; y <= y1; ++y) {
        float dy = y + 0.5f - cy;
        for (int x = x0; x <= x1; ++x) {
            float dx = x + 0.5f - cx;
            float qx = fabsf(c * dx + s * dy) - coreX;
            float qy = fabsf(-s * dx + c * dy) - coreY;
            if (qx > radius + 0.5f || qy > radius + 0.5f) continue;
            float distance = (qx > 0 && qy > 0) ? sqrtf(qx * qx + qy * qy) : fmaxf(qx, qy);
            float coverage = 0.5f - (distance - radius);
            if (coverage <= 0) continue;
            uint16_t& pixel = pixels[y * width + x];
            if (coverage >= 1) { pixel = solid; continue; }
            uint16_t bg = eyeSwap565(pixel);
            int alpha = (int)(coverage * 255 + 0.5f), inverse = 255 - alpha;
            int r = (red * alpha + ((bg >> 11) & 31) * inverse + 127) / 255;
            int g = (green * alpha + ((bg >> 5) & 63) * inverse + 127) / 255;
            int b = (blue * alpha + (bg & 31) * inverse + 127) / 255;
            pixel = eyeSwap565((uint16_t)((r << 11) | (g << 5) | b));
        }
    }
}
