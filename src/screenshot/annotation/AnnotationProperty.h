#pragma once

// Property keys for annotation items.
// Each property maps 1:1 to a single config value that can be read/written on an item.

enum class AnnotationProperty {
    // === Common (shared across most tools) ===
    Color = 0,
    ColorIndex,
    StackIndex,
    ColorAlpha,
    PenWidth,
    PenStyle,         // 0=Solid, 1=Dash, 2=Dot, 3=DashDot, 4=DashDotDot
    PathMode,         // 1=FreePath, 2=Rect, 3=Ellipse (tool-dependent)
    PathPoints,       // serialized POINT list for polyline/free path tools
    RectRoundRadius,
    Angle,
    Filling,

    // === Arrow ===
    LineShape,        // 1-8

    // === Marker ===
    MarkerBlendMode,  // 0=Multiply, 1=Translucent/SourceOver

    // === Mosaic ===
    MosaicMode,       // 0=Pixelate, 1=Blur
    MosaicStrength,   // 0-28 per evidence
    BlurStrength,

    // === BrokenLine ===
    BrokenLineMode,           // 0=Straight, 1=Curve
    BrokenLineArrowEnabled,
    BrokenLineStartArrowType, // 0-12 per ArrowHeadType enum
    BrokenLineEndArrowType,

    // === Magnifier ===
    MagnifierLinkType,       // MarkBar value: 0=Line, 1=DotLine, 2=Shape, 3=Hide
    MagnifierMagnification,  // integer, 150 = 1.5x
    MagnifierSourceX,
    MagnifierSourceY,
    MagnifierSourceLeft,
    MagnifierSourceTop,
    MagnifierSourceRight,
    MagnifierSourceBottom,
    MagnifierAntiAlias,
    MagnifierEraseMark,
    MagnifierShadow,

    // === HighLight ===
    HighLightOpacity,       // 0-100, maps to alphaF
    HighLightStroke,        // bool
    HighLightStrokeWidth,   // 1-50
    HighLightStrokeColor,

    // === Text ===
    TextBold,
    TextItalics,
    TextOutline,
    TextOutlineSize,    // 1-50
    TextOutlineColor,
    TextBackground,
    TextBackgroundColor,
    TextBackgroundOpacity,  // 0-100
    TextBackgroundRounded,  // 0-30
    TextBackgroundPadding,  // 0-50
    TextFontFamily,
    TextFontSize,

    // === Serial ===
    SerialIndex,
    SerialType,           // 0=1.2.3, 1=I.II.III, 2=a.b.c, 3=A.B.C, 4=CJK numerals
    SerialStyleType,      // 0=stroke, 1=filled
    EmbeddedTextFontSize, // 0.2-1.0 scaled
    EmbeddedTextOutline,
    EmbeddedTextFontFamily,

    // === Watermark ===
    WatermarkText,
    WatermarkColor,
    WatermarkOpacity,     // 0-100
    WatermarkFontSize,
    WatermarkGap,
    WatermarkAngle,       // Tile mode only
    WatermarkFontFamily,
    WatermarkPosition,    // 0=Tile, 1=BR, 2=BL, 3=TR, 4=TL, 5=TC, 6=BC, 7=Center

    _Count
};

// Value type categorization for serialization and UI control binding.
// S-D-1: kept as thin alias of AnnotationValueKind; sole schema body lives in
// GetAnnotationPropertyKind (AnnotationValue.cpp). Prefer AnnotationValueKind.
enum class PropertyValueType {
    Int,
    Bool,
    Double,
    Color,   // COLORREF or QColor equivalent
    String,
};

// Thin adapter over GetAnnotationPropertyKind (sole schema).
PropertyValueType GetPropertyType(AnnotationProperty prop);
