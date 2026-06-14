#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "Atmospheric/font_manager.hpp"
#include "Atmospheric/gfx_factory.hpp"
#include <cmath>
#include <fmt/format.h>
#include <fstream>

FontManager::FontManager() {
}

FontManager::~FontManager() {
    for (auto& [id, font] : _fonts) {
        if (font.textureID == 0) continue;
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
        if (GfxFactory::GetBackend() == GfxBackend::WebGPU) continue; // released by GfxFactory::Shutdown
#endif
        glDeleteTextures(1, &font.textureID);
    }
}

FontID FontManager::LoadFont(const std::string& path, float baseSize, int firstChar, int numChars) {
    // Read font file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        fmt::print(stderr, "[FontManager] Failed to open font file: {}\n", path);
        return 0;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> fontData(fileSize);
    if (!file.read(reinterpret_cast<char*>(fontData.data()), fileSize)) {
        fmt::print(stderr, "[FontManager] Failed to read font file: {}\n", path);
        return 0;
    }

    // Create font entry
    FontID id = _nextFontID++;
    Font& font = _fonts[id];
    font.fontSize = baseSize;

    // Bake the font atlas
    if (!BakeFontAtlas(font, fontData.data(), baseSize, firstChar, numChars)) {
        _fonts.erase(id);
        return 0;
    }

    fmt::print(
      "[FontManager] Loaded font: {} (size: {}, texture: {}x{})\n",
      path,
      baseSize,
      font.textureWidth,
      font.textureHeight
    );

    return id;
}

void FontManager::UnloadFont(FontID id) {
    auto it = _fonts.find(id);
    if (it != _fonts.end()) {
        if (it->second.textureID != 0) {
            glDeleteTextures(1, &it->second.textureID);
        }
        _fonts.erase(it);
    }
}

Font* FontManager::GetFont(FontID id) {
    auto it = _fonts.find(id);
    return (it != _fonts.end()) ? &it->second : nullptr;
}

GLuint FontManager::GetFontTexture(FontID id) {
    auto it = _fonts.find(id);
    return (it != _fonts.end()) ? it->second.textureID : 0;
}

glm::vec2 FontManager::MeasureText(FontID id, const std::string& text, float scale) {
    Font* font = GetFont(id);
    if (!font) return glm::vec2(0.0f);

    float width = 0.0f;
    float maxHeight = 0.0f;

    for (char c : text) {
        auto it = font->glyphs.find(static_cast<int>(c));
        if (it != font->glyphs.end()) {
            width += it->second.advance * scale;
            float h = it->second.height * scale;
            if (h > maxHeight) maxHeight = h;
        }
    }

    return glm::vec2(width, maxHeight > 0 ? maxHeight : font->lineHeight * scale);
}

const Glyph* FontManager::GetGlyph(FontID id, int codepoint) {
    Font* font = GetFont(id);
    if (!font) return nullptr;

    auto it = font->glyphs.find(codepoint);
    return (it != font->glyphs.end()) ? &it->second : nullptr;
}

bool FontManager::BakeFontAtlas(
  Font& font, const unsigned char* fontData, float fontSize, int firstChar, int numChars
) {
    // Initialize stb_truetype
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontData, 0)) {
        fmt::print(stderr, "[FontManager] Failed to initialize font\n");
        return false;
    }

    // Get font metrics
    float scale = stbtt_ScaleForPixelHeight(&fontInfo, fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);

    font.ascent = ascent * scale;
    font.descent = descent * scale;
    font.lineHeight = (ascent - descent + lineGap) * scale;

    // Calculate atlas size (power of 2)
    // Estimate: each glyph is roughly fontSize x fontSize
    int glyphsPerRow = static_cast<int>(std::ceil(std::sqrt(numChars)));
    int atlasSize = 1;
    while (atlasSize < glyphsPerRow * static_cast<int>(fontSize * 1.5f)) {
        atlasSize *= 2;
    }
    atlasSize = std::min(atlasSize, 2048);// Cap at 2048

    font.textureWidth = atlasSize;
    font.textureHeight = atlasSize;

    // Allocate atlas bitmap
    std::vector<unsigned char> atlasBitmap(atlasSize * atlasSize, 0);

    // Pack glyphs
    int x = 1, y = 1;
    int rowHeight = 0;
    int padding = 2;

    for (int i = 0; i < numChars; i++) {
        int codepoint = firstChar + i;

        // Get glyph metrics
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&fontInfo, codepoint, &advance, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&fontInfo, codepoint, scale, scale, &x0, &y0, &x1, &y1);

        int glyphWidth = x1 - x0;
        int glyphHeight = y1 - y0;

        // Check if glyph fits in current row
        if (x + glyphWidth + padding > atlasSize) {
            x = 1;
            y += rowHeight + padding;
            rowHeight = 0;
        }

        // Check if atlas is full
        if (y + glyphHeight + padding > atlasSize) {
            fmt::print(stderr, "[FontManager] Atlas too small for all glyphs\n");
            break;
        }

        // Render glyph to atlas
        if (glyphWidth > 0 && glyphHeight > 0) {
            stbtt_MakeCodepointBitmap(
              &fontInfo, &atlasBitmap[y * atlasSize + x], glyphWidth, glyphHeight, atlasSize, scale, scale, codepoint
            );
        }

        // Store glyph info
        Glyph glyph;
        glyph.u0 = static_cast<float>(x) / atlasSize;
        glyph.v0 = static_cast<float>(y) / atlasSize;
        glyph.u1 = static_cast<float>(x + glyphWidth) / atlasSize;
        glyph.v1 = static_cast<float>(y + glyphHeight) / atlasSize;
        glyph.xOffset = static_cast<float>(x0);
        glyph.yOffset = static_cast<float>(y0);
        glyph.width = static_cast<float>(glyphWidth);
        glyph.height = static_cast<float>(glyphHeight);
        glyph.advance = advance * scale;

        font.glyphs[codepoint] = glyph;

        // Update position
        x += glyphWidth + padding;
        if (glyphHeight > rowHeight) rowHeight = glyphHeight;
    }

    // Convert single-channel to RGBA (white text with alpha)
    std::vector<unsigned char> rgbaData(atlasSize * atlasSize * 4);
    for (int i = 0; i < atlasSize * atlasSize; i++) {
        rgbaData[i * 4 + 0] = 255;
        rgbaData[i * 4 + 1] = 255;
        rgbaData[i * 4 + 2] = 255;
        rgbaData[i * 4 + 3] = atlasBitmap[i];
    }

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        font.textureID = GfxFactory::UploadTexture2D(rgbaData.data(), atlasSize, atlasSize);
    } else
#endif
    {
        // OpenGL path
        glGenTextures(1, &font.textureID);
        glBindTexture(GL_TEXTURE_2D, font.textureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasSize, atlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    return true;
}
