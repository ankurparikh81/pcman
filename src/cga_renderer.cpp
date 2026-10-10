#include "cga_renderer.hpp"
#include <algorithm>
#include <cstring>

namespace pcman {

// Authentic IBM PC CGA Mode 4 / ROM BIOS 8x8 font (Code Page 437)
namespace {
    #include "font_data.inl"

    // Extract 2bpp CGA Mode 4 pixel p (0..3, left to right) from a packed byte
    constexpr uint8_t CgaPixel(uint8_t b, int p) {
        return static_cast<uint8_t>((b >> (6 - p * 2)) & 3);
    }

    // Look up the authentic IBM PC CGA / CP437 8x8 glyph for ch
    constexpr const uint8_t* GlyphFor(char ch) {
        uint8_t u = static_cast<uint8_t>(ch);
        return &FONT_8X8[u * 8];
    }

    static_assert(CgaPixel(0b11'10'01'00, 0) == 3 && CgaPixel(0b11'10'01'00, 3) == 0);
    static_assert(GlyphFor(' ')[0] == 0x00);
    static_assert(GlyphFor('\x01')[0] == 0x7E);                           // Authentic IBM CP437 smiley face
    static_assert(GlyphFor('0')[0] == 0x7C && GlyphFor('0')[2] == 0xCE); // Authentic IBM slashed zero
    static_assert(GlyphFor('A')[4] == 0xFC);                              // Crossbar of 'A'
}

CgaRenderer::CgaRenderer() {
    m_cgaBuffer.assign(CGA_WIDTH * CGA_HEIGHT, 0);
    m_pixelBuffer.assign(CGA_WIDTH * CGA_HEIGHT, BLACK);
}

CgaRenderer::~CgaRenderer() {
    Close();
}

void CgaRenderer::Init() {
    if (m_initialized) return;

    Image img = GenImageColor(CGA_WIDTH, CGA_HEIGHT, BLACK);
    m_screenTexture = LoadTextureFromImage(img);
    SetTextureFilter(m_screenTexture, TEXTURE_FILTER_POINT);
    UnloadImage(img);

    m_cgaBuffer.assign(CGA_WIDTH * CGA_HEIGHT, 0);
    m_pixelBuffer.assign(CGA_WIDTH * CGA_HEIGHT, BLACK);
    m_initialized = true;
}

void CgaRenderer::Close() {
    if (m_initialized) {
        UnloadTexture(m_screenTexture);
        m_initialized = false;
    }
}

void CgaRenderer::SetFlashMode(bool flash) {
    m_flash = flash;
}

void CgaRenderer::Clear(uint8_t colorIdx) {
    colorIdx &= 3;
    std::fill(m_cgaBuffer.begin(), m_cgaBuffer.end(), colorIdx);
}


void CgaRenderer::BlitTile4x4(int screenX, int screenY, uint8_t tileId) {
    auto tileBytes = RomData::GetMazeTile(tileId);
    for (int y = 0; y < TILE_HEIGHT; ++y) {
        int py = screenY + y;
        if (py < 0 || py >= CGA_HEIGHT) continue;

        uint8_t b = tileBytes[y];
        for (int p = 0; p < 4; ++p) {
            int px = screenX + p;
            if (px < 0 || px >= CGA_WIDTH) continue;
            uint8_t c = CgaPixel(b, p);
            m_cgaBuffer[py * CGA_WIDTH + px] = c;
        }
    }
}

void CgaRenderer::BlitDot8x8(int screenX, int screenY, uint8_t dotType) {
    auto dotBytes = RomData::GetDotBitmap(dotType);
    for (int y = 0; y < 8; ++y) {
        int py = screenY + y;
        if (py < 0 || py >= CGA_HEIGHT) continue;

        uint8_t b0 = dotBytes[y * 2];
        uint8_t b1 = dotBytes[y * 2 + 1];

        for (int p = 0; p < 4; ++p) {
            int px = screenX + p;
            if (px >= 0 && px < CGA_WIDTH) {
                uint8_t c = CgaPixel(b0, p);
                if (c != 0) m_cgaBuffer[py * CGA_WIDTH + px] = c;
            }
        }
        for (int p = 0; p < 4; ++p) {
            int px = screenX + 4 + p;
            if (px >= 0 && px < CGA_WIDTH) {
                uint8_t c = CgaPixel(b1, p);
                if (c != 0) m_cgaBuffer[py * CGA_WIDTH + px] = c;
            }
        }
    }
}

void CgaRenderer::BlitBitmap2bpp(int screenX, int screenY, int width, int height, const uint8_t* data, bool transparent) {
    if (!data) return;
    int bytesPerRow = width / 4;
    for (int y = 0; y < height; ++y) {
        int py = screenY + y;
        if (py < 0 || py >= CGA_HEIGHT) continue;

        for (int byteIdx = 0; byteIdx < bytesPerRow; ++byteIdx) {
            uint8_t b = data[y * bytesPerRow + byteIdx];
            for (int p = 0; p < 4; ++p) {
                int px = screenX + byteIdx * 4 + p;
                if (px < 0 || px >= CGA_WIDTH) continue;
                uint8_t c = CgaPixel(b, p);
                if (!transparent || c != 0) {
                    m_cgaBuffer[py * CGA_WIDTH + px] = c;
                }
            }
        }
    }
}

void CgaRenderer::BlitSpriteRaw(int screenX, int screenY, const uint8_t* sprite56, bool transparent) {
    BlitBitmap2bpp(screenX, screenY, SPRITE_WIDTH, SPRITE_HEIGHT, sprite56, transparent);
}

void CgaRenderer::BlitSprite16x14(int screenX, int screenY, int spriteIndex, bool transparent) {
    auto spriteBytes = RomData::GetSprite(spriteIndex);
    BlitSpriteRaw(screenX, screenY, spriteBytes.data(), transparent);
}

void CgaRenderer::BlitLogoLetter36x23(int screenX, int screenY, int letterIndex) {
    auto letterBytes = RomData::GetLogoLetter(letterIndex);
    BlitBitmap2bpp(screenX, screenY, LOGO_LETTER_WIDTH, LOGO_LETTER_HEIGHT, letterBytes.data(), false);
}

void CgaRenderer::BlitGhostScore16x8(int screenX, int screenY, int comboIndex, bool transparent) {
    auto spriteBytes = RomData::GetGhostScoreSprite(comboIndex);
    BlitBitmap2bpp(screenX, screenY, GHOST_SCORE_WIDTH, GHOST_SCORE_HEIGHT, spriteBytes.data(), transparent);
}

void CgaRenderer::BlitFruitScore16x20(int screenX, int screenY, int fruitIndex, bool transparent) {
    auto popupBytes = RomData::GetFruitScorePopup(fruitIndex);
    BlitBitmap2bpp(screenX, screenY, FRUIT_SCORE_POPUP_WIDTH, FRUIT_SCORE_POPUP_HEIGHT, popupBytes.data(), transparent);
}

void CgaRenderer::DrawText8x8(int screenX, int screenY, const std::string& text, uint8_t colorIdx) {
    colorIdx &= 3;
    int curX = screenX;
    int curY = screenY;

    for (char ch : text) {
        if (ch == '\n') {
            curX = screenX;
            curY += 8;
            continue;
        }
        if (ch == '\r') continue;

        const uint8_t* glyph = GlyphFor(ch);

        for (int y = 0; y < 8; ++y) {
            int py = curY + y;
            if (py < 0 || py >= CGA_HEIGHT) continue;
            uint8_t row = glyph[y];
            for (int x = 0; x < 8; ++x) {
                int px = curX + x;
                if (px < 0 || px >= CGA_WIDTH) continue;
                if ((row >> (7 - x)) & 1) {
                    m_cgaBuffer[py * CGA_WIDTH + px] = colorIdx;
                }
            }
        }
        curX += 8;
    }
}

void CgaRenderer::ClearRect(int x, int y, int width, int height, uint8_t colorIdx) {
    colorIdx &= 3;
    for (int cy = y; cy < y + height; ++cy) {
        if (cy < 0 || cy >= CGA_HEIGHT) continue;
        for (int cx = x; cx < x + width; ++cx) {
            if (cx < 0 || cx >= CGA_WIDTH) continue;
            m_cgaBuffer[cy * CGA_WIDTH + cx] = colorIdx;
        }
    }
}

void CgaRenderer::DrawTextVertical8x8(int screenX, int startY, const std::string& text, uint8_t colorIdx, int spacing) {
    colorIdx &= 3;
    int curY = startY;

    for (char ch : text) {
        if (ch == ' ') {
            curY += spacing;
            continue;
        }

        const uint8_t* glyph = GlyphFor(ch);

        for (int y = 0; y < 8; ++y) {
            int py = curY + y;
            if (py < 0 || py >= CGA_HEIGHT) continue;
            uint8_t row = glyph[y];
            for (int x = 0; x < 8; ++x) {
                int px = screenX + x;
                if (px < 0 || px >= CGA_WIDTH) continue;
                if ((row >> (7 - x)) & 1) {
                    m_cgaBuffer[py * CGA_WIDTH + px] = colorIdx;
                }
            }
        }
        curY += spacing;
    }
}

uint8_t CgaRenderer::GetPixel(int x, int y) const {
    if (x < 0 || x >= CGA_WIDTH || y < 0 || y >= CGA_HEIGHT) return 0;
    return m_cgaBuffer[y * CGA_WIDTH + x];
}

void CgaRenderer::UpdateTextureFromBuffer() {
    for (size_t i = 0; i < m_cgaBuffer.size(); ++i) {
        m_pixelBuffer[i] = CgaColor(m_cgaBuffer[i], m_flash);
    }
    UpdateTexture(m_screenTexture, m_pixelBuffer.data());
}

void CgaRenderer::Present(int windowWidth, int windowHeight) {
    UpdateTextureFromBuffer();

    constexpr float targetAspect = 4.0f / 3.0f;
    float winWidth = static_cast<float>(windowWidth);
    float winHeight = static_cast<float>(windowHeight);
    float winAspect = winWidth / winHeight;

    float destWidth, destHeight;
    float destX, destY;

    if (winAspect > targetAspect) {
        destHeight = winHeight;
        destWidth = destHeight * targetAspect;
        destX = (winWidth - destWidth) / 2.0f;
        destY = 0.0f;
    } else {
        destWidth = winWidth;
        destHeight = destWidth / targetAspect;
        destX = 0.0f;
        destY = (winHeight - destHeight) / 2.0f;
    }

    ClearBackground(BLACK);

    Rectangle srcRect{ 0.0f, 0.0f, static_cast<float>(CGA_WIDTH), static_cast<float>(CGA_HEIGHT) };
    Rectangle destRect{ destX, destY, destWidth, destHeight };
    DrawTexturePro(m_screenTexture, srcRect, destRect, Vector2{ 0.0f, 0.0f }, 0.0f, WHITE);
}

} // namespace pcman
