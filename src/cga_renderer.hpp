#pragma once
#include "rom_data.hpp"
#include <raylib.h>
#include <array>
#include <vector>
#include <string>

namespace pcman {

// Authentic IBM Color Graphics Adapter (CGA) 16-color digital RGBI palette (sRGB)
inline constexpr Color CGA_COLOR_BLACK         = {   0,   0,   0, 255 }; // 0: #000000
inline constexpr Color CGA_COLOR_BLUE          = {   0,   0, 170, 255 }; // 1: #0000AA
inline constexpr Color CGA_COLOR_GREEN         = {   0, 170,   0, 255 }; // 2: #00AA00
inline constexpr Color CGA_COLOR_CYAN          = {   0, 170, 170, 255 }; // 3: #00AAAA
inline constexpr Color CGA_COLOR_RED           = { 170,   0,   0, 255 }; // 4: #AA0000
inline constexpr Color CGA_COLOR_MAGENTA       = { 170,   0, 170, 255 }; // 5: #AA00AA
inline constexpr Color CGA_COLOR_BROWN         = { 170,  85,   0, 255 }; // 6: #AA5500 (IBM hardware brown fix)
inline constexpr Color CGA_COLOR_LIGHT_GRAY    = { 170, 170, 170, 255 }; // 7: #AAAAAA
inline constexpr Color CGA_COLOR_DARK_GRAY     = {  85,  85,  85, 255 }; // 8: #555555
inline constexpr Color CGA_COLOR_LIGHT_BLUE    = {  85,  85, 255, 255 }; // 9: #5555FF
inline constexpr Color CGA_COLOR_LIGHT_GREEN   = {  85, 255,  85, 255 }; // 10: #55FF55
inline constexpr Color CGA_COLOR_LIGHT_CYAN    = {  85, 255, 255, 255 }; // 11: #55FFFF
inline constexpr Color CGA_COLOR_LIGHT_RED     = { 255,  85,  85, 255 }; // 12: #FF5555
inline constexpr Color CGA_COLOR_LIGHT_MAGENTA = { 255,  85, 255, 255 }; // 13: #FF55FF
inline constexpr Color CGA_COLOR_YELLOW        = { 255, 255,  85, 255 }; // 14: #FFFF55
inline constexpr Color CGA_COLOR_WHITE         = { 255, 255, 255, 255 }; // 15: #FFFFFF

// CGA Mode 4 4-color palettes
struct CgaPalette {
    Color colors[4];
};

// Mode 4 Palette 0 High-Intensity (Port 0x3D9 = 0x10, standard gameplay)
inline constexpr CgaPalette CGA_PALETTE_0_HIGH = {
    CGA_COLOR_BLACK,        // 0: Black
    CGA_COLOR_LIGHT_GREEN,  // 1: Light Green
    CGA_COLOR_LIGHT_RED,    // 2: Light Red
    CGA_COLOR_YELLOW        // 3: Yellow
};

// Mode 4 Palette 1 Low-Intensity (Port 0x3D9 = 0x20, authentic level clear flash in v1.com)
inline constexpr CgaPalette CGA_PALETTE_1_LOW = {
    CGA_COLOR_BLACK,        // 0: Black
    CGA_COLOR_CYAN,         // 1: Cyan
    CGA_COLOR_MAGENTA,      // 2: Magenta
    CGA_COLOR_LIGHT_GRAY    // 3: Light Gray / White
};

inline constexpr CgaPalette CGA_PALETTE_NORMAL = CGA_PALETTE_0_HIGH;
inline constexpr CgaPalette CGA_PALETTE_FLASH  = CGA_PALETTE_1_LOW;

constexpr Color CgaColor(uint8_t colorIdx, bool flash = false) {
    const auto& pal = flash ? CGA_PALETTE_FLASH : CGA_PALETTE_NORMAL;
    return pal.colors[colorIdx & 3];
}

class CgaRenderer {
public:
    CgaRenderer();
    ~CgaRenderer();

    void Init();
    void Close();

    void SetFlashMode(bool flash);

    void Clear(uint8_t colorIdx = 0);

    // Direct 2-bpp CGA Mode 4 blitters
    void BlitTile4x4(int screenX, int screenY, uint8_t tileId);
    void BlitDot8x8(int screenX, int screenY, uint8_t dotType);
    void BlitSprite16x14(int screenX, int screenY, int spriteIndex, bool transparent = true);
    void BlitSpriteRaw(int screenX, int screenY, const uint8_t* sprite56, bool transparent = true);
    void BlitLogoLetter36x23(int screenX, int screenY, int letterIndex);
    void BlitBitmap2bpp(int screenX, int screenY, int width, int height, const uint8_t* data, bool transparent = true);
    void BlitGhostScore16x8(int screenX, int screenY, int comboIndex, bool transparent = true);
    void BlitFruitScore16x20(int screenX, int screenY, int fruitIndex, bool transparent = false);

    void ClearRect(int x, int y, int width, int height, uint8_t colorIdx = 0);

    // Text rendering using classic CGA 8x8 font
    void DrawText8x8(int screenX, int screenY, const std::string& text, uint8_t colorIdx = 3);
    void DrawTextVertical8x8(int screenX, int startY, const std::string& text, uint8_t colorIdx = 3, int spacing = 8);

    // Framebuffer access
    [[nodiscard]] uint8_t GetPixel(int x, int y) const;

    // Render frame to screen with 4:3 aspect ratio
    void Present(int windowWidth, int windowHeight);

private:
    std::vector<uint8_t> m_cgaBuffer;
    std::vector<Color> m_pixelBuffer;
    Texture2D m_screenTexture{};
    bool m_initialized = false;
    bool m_flash = false;

    void UpdateTextureFromBuffer();
};

} // namespace pcman
