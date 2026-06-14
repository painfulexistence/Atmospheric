#pragma once

#ifndef __EMSCRIPTEN__
#include <glad/glad.h>
#else
#include <GLES3/gl3.h>
#endif
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

/// Font glyph information
struct Glyph {
    float u0, v0, u1, v1;  // Texture coordinates
    float xOffset, yOffset; // Offset from cursor position
    float width, height;    // Size in pixels
    float advance;          // Horizontal advance
};

/// Font information
struct Font {
    GLuint textureID = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    float fontSize = 0;           // Base font size (design size)
    float lineHeight = 0;         // Line height in pixels
    float ascent = 0;             // Distance from baseline to top
    float descent = 0;            // Distance from baseline to bottom
    std::unordered_map<int, Glyph> glyphs;  // Codepoint -> Glyph
};

using FontID = uint32_t;

/// FontManager - Manages font loading and text rendering
///
/// Supports resolution-independent text rendering by:
/// 1. Baking fonts at a high base resolution (default 48px)
/// 2. Scaling when drawing to achieve desired size
/// 3. Using linear texture filtering for quality scaling
///
/// Usage:
///   FontID font = fontManager.LoadFont("assets/fonts/arial.ttf", 48.0f);
///   fontManager.DrawText(font, "Hello", 100, 100, 1.0f);  // Scale 1.0 = 48px
///   fontManager.DrawText(font, "Small", 100, 150, 0.5f); // Scale 0.5 = 24px
///
class FontManager {
public:
    FontManager();
    ~FontManager();

    /// Load a TTF font file and create a font atlas
    /// @param path Path to the TTF file
    /// @param baseSize Base font size for rendering (higher = better quality when scaled up)
    /// @param firstChar First character to include (default 32 = space)
    /// @param numChars Number of characters to include (default 95 = ASCII printable)
    /// @return Font ID, or 0 on failure
    FontID LoadFont(const std::string& path, float baseSize = 48.0f,
                    int firstChar = 32, int numChars = 95);

    /// Unload a font and free its resources
    void UnloadFont(FontID id);

    /// Get font information
    Font* GetFont(FontID id);

    /// Get the texture ID for a font (for custom rendering)
    GLuint GetFontTexture(FontID id);

    /// Measure text dimensions
    /// @param id Font ID
    /// @param text Text to measure
    /// @param scale Scale factor (1.0 = base size)
    /// @return Width and height as vec2
    glm::vec2 MeasureText(FontID id, const std::string& text, float scale = 1.0f);

    /// Get individual glyph info (for custom rendering)
    const Glyph* GetGlyph(FontID id, int codepoint);

private:
    std::unordered_map<FontID, Font> _fonts;
    FontID _nextFontID = 1;

    /// Bake font glyphs into a texture atlas
    bool BakeFontAtlas(Font& font, const unsigned char* fontData,
                       float fontSize, int firstChar, int numChars);
};
