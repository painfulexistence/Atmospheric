#pragma once
#include "canvas_drawable.hpp"
#include "component.hpp"
#include "font_manager.hpp"
#include "globals.hpp"
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <string>

// Text alignment enums (matches CSB spec)
enum class TextHAlignment { Left = 0, Center = 1, Right = 2 };

enum class TextVAlignment { Top = 0, Center = 1, Bottom = 2 };

struct TextProps {
    std::string text = "";
    std::string fontPath = "";// Path to font file (for auto-loading)
    FontID fontID = 0;// Pre-loaded font ID (takes priority if non-zero)
    float fontSize = 24.0f;// Desired font size
    glm::vec2 size = glm::vec2(100.0f);// Bounding box size
    glm::vec2 pivot = glm::vec2(0.0f, 0.0f);// Default to top-left
    glm::vec4 color = glm::vec4(1.0f);
    TextHAlignment hAlign = TextHAlignment::Left;
    TextVAlignment vAlign = TextVAlignment::Top;
    CanvasLayer layer = CanvasLayer::LAYER_WORLD;
    int zOrder = 0;
};


class TextComponent : public CanvasDrawable {
public:
    TextComponent(GameObject* gameObject, const TextProps& props);

    std::string GetName() const override;

    void OnAttach() override;
    void OnDetach() override;

    void Draw(BatchRenderer2D* renderer) override;

    bool CanTick() const override {
        return false;
    }

    // Getters
    const std::string& GetText() const {
        return _text;
    }
    const std::string& GetFontPath() const {
        return _fontPath;
    }
    FontID GetFontID() const {
        return _fontID;
    }
    float GetFontSize() const {
        return _fontSize;
    }
    glm::vec2 GetSize() const {
        return _size;
    }
    glm::vec2 GetPivot() const {
        return _pivot;
    }
    glm::vec4 GetColor() const {
        return _color;
    }
    TextHAlignment GetHAlign() const {
        return _hAlign;
    }
    TextVAlignment GetVAlign() const {
        return _vAlign;
    }
    CanvasLayer GetLayer() const override {
        return _layer;
    }
    int GetZOrder() const {
        return _zOrder;
    }

    // Setters
    void SetText(const std::string& text) {
        _text = text;
    }
    void SetFontID(FontID fontID) {
        _fontID = fontID;
    }
    void SetFontSize(float fontSize) {
        _fontSize = fontSize;
    }
    void SetSize(const glm::vec2& size) {
        _size = size;
    }
    void SetPivot(const glm::vec2& pivot) {
        _pivot = pivot;
    }
    void SetColor(const glm::vec4& color) {
        _color = color;
    }
    void SetHAlign(TextHAlignment hAlign) {
        _hAlign = hAlign;
    }
    void SetVAlign(TextVAlignment vAlign) {
        _vAlign = vAlign;
    }
    void SetLayer(CanvasLayer layer) {
        _layer = layer;
    }
    void SetZOrder(int zOrder) {
        _zOrder = zOrder;
    }

private:
    std::string _text;
    std::string _fontPath;
    FontID _fontID = 0;
    float _fontSize = 24.0f;
    float _fontBaseSize = 48.0f;// Base size the font was loaded at
    glm::vec2 _size;
    glm::vec2 _pivot;
    glm::vec4 _color;
    TextHAlignment _hAlign = TextHAlignment::Left;
    TextVAlignment _vAlign = TextVAlignment::Top;
    CanvasLayer _layer;
    int _zOrder = 0;
};
