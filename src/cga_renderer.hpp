#pragma once
#include "rom_data.hpp"
#include <raylib.h>
#include <array>
#include <vector>
#include <string>

namespace pcman {

// CGA Mode 4 Palette 0 High-Intensity colors
struct CgaPalette {
    Color colors[4];
};

inline constexpr CgaPalette CGA_PALETTE_NORMAL = {
    Color{ 0, 0, 0, 255 },       // 0: Black
    Color{ 85, 255, 85, 255 },   // 1: Light Green
    Color{ 255, 85, 85, 255 },   // 2: Bright Red
    Color{ 255, 255, 85, 255 }   // 3: Yellow
};

inline constexpr CgaPalette CGA_PALETTE_FLASH = {
    Color{ 0, 0, 0, 255 },       // 0: Black
    Color{ 255, 255, 255, 255 }, // 1: White
    Color{ 85, 85, 255, 255 },   // 2: Light Blue
    Color{ 255, 255, 255, 255 }  // 3: White
};

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

    void ClearRect(int x, int y, int width, int height, uint8_t colorIdx = 0);

    // Text rendering using classic CGA 8x8 font
    void DrawText8x8(int screenX, int screenY, const std::string& text, uint8_t colorIdx = 3);
    void DrawTextVertical8x8(int screenX, int startY, const std::string& text, uint8_t colorIdx = 3, int spacing = 8);

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
