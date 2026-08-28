#pragma once

#include "screenshot/ScreenshotTypes.h"
#include "screenshot/annotation/AnnotationTypes.h"

#include <string>

// Stage 2 S-B seed: aggregate screenshot editor flags without HWND.
// OverlayWindow still owns vectors during migration; this clusters tool/selection.

// Dual-write tool style (pen widths + color + fill) without HWND/GDI ownership.
// Colors stored as packed 0x00BBGGRR (COLORREF layout) to avoid windows.h here.
struct ScreenshotEditorToolStyle {
    int geometryPenWidth = 4;
    int pencilPenWidth = 4;
    int markerPenWidth = 12;
    int arrowPenWidth = 24;
    int magnifierPenWidth = 4;
    int mosaicPenWidth = 12;
    int eraserPenWidth = 12;
    int serialPenWidth = 16;
    unsigned int customColor = 0x0063C3E3;  // RGB(227,195,99) as COLORREF
    bool usesCustomColor = false;
    int colorAlpha = 100;
    bool fillingEnabled = false;
};

// Dual-write geometry / path / arrow mode flags (Stage 2 S-B deepen).
struct ScreenshotEditorToolModes {
    bool geometryEllipse = false;
    int lineStyle = 1;
    int arrowShape = 4;
    bool markerPencilMode = false;
    bool mosaicPencilMode = false;
    bool eraserPencilMode = false;
    int brokenLineMode = 0;
    bool brokenLineArrow = true;
    int brokenLineStartArrowType = 0;
    int brokenLineEndArrowType = 1;
};

// Dual-write text annotation style (Stage 2 S-B OWN-67).
struct ScreenshotEditorTextStyle {
    bool bold = false;
    bool italics = false;
    bool outline = false;
    bool background = false;
    unsigned int outlineColor = 0x00FFFFFF;  // RGB(255,255,255)
    unsigned int backgroundColor = 0x00000000;
    std::wstring fontFamily = L"Microsoft YaHei";
    int fontSize = 27;
    double fontSizeF = 27.0;
    int outlineSize = 1;
    int backgroundOpacity = 100;
    int backgroundRounded = 0;
    int backgroundPadding = 0;
};

// Dual-write watermark style (Stage 2 S-B OWN-67).
struct ScreenshotEditorWatermarkStyle {
    std::wstring text = L"Watermark";
    unsigned int color = 0x000F03FA;  // RGB(250,3,15)
    bool bold = false;
    bool italics = false;
    int opacity = 50;
    int fontSize = 27;
    int gap = 20;
    int angle = 0;
    std::wstring fontFamily = L"Microsoft YaHei";
    int position = 1;
};

// Dual-write highlight style (Stage 2 S-B OWN-67).
struct ScreenshotEditorHighLightStyle {
    bool stroke = false;
    int opacity = 68;
    unsigned int strokeColor = 0x00000FFF;  // RGB(255,15,0)
};

// Dual-write magnifier style (Stage 2 S-B OWN-67).
struct ScreenshotEditorMagnifierStyle {
    bool ellipse = false;
    bool eraseMark = false;
    bool antiAlias = true;
    bool shadow = false;
    int linkType = 0;
    int roundedRadius = 18;
    int magnification = 150;
};

// Dual-write last-used tool per toolbar group (Stage 2 S-B OWN-67).
struct ScreenshotEditorToolGroupMemory {
    ScreenshotToolbarCommand geometryTool = ScreenshotToolbarCommand::ToolGeometry;
    ScreenshotToolbarCommand markerTool = ScreenshotToolbarCommand::ToolMarker;
    ScreenshotToolbarCommand arrowTool = ScreenshotToolbarCommand::ToolArrow;
    ScreenshotToolbarCommand textTool = ScreenshotToolbarCommand::ToolText;
    ScreenshotToolbarCommand mosaicTool = ScreenshotToolbarCommand::ToolMosaic;
};

// Dual-write post-process / rounded-corner session prefs (Stage 2 S-B OWN-67).
struct ScreenshotEditorPostProcessStyle {
    bool roundedCorners = false;
    int roundedCornerRadius = 18;
    bool enabled = false;
    bool enableEveryScreenshot = false;
    int mode = 1;
    int shadowSize = 10;
    unsigned int shadowColor = 0x00000000;
    int borderSize = 2;
    unsigned int borderColor = 0x00FFFFFF;
};

// Dual-write geometry / mosaic / serial effect knobs (Stage 2 S-B OWN-68).
struct ScreenshotEditorEffectStyle {
    int geometryRoundedRadius = 21;
    int markerBlendMode = 0;   // 0=Multiply, 1=Translucent
    int mosaicMode = 0;        // 0=Mosaic, 1=Blur
    int mosaicStrength = 14;
    int serialType = 0;
    int serialCounter = 1;
    bool autoMosaicSync = true;
};

// Dual-write crop-session prefs (Stage 2 S-B OWN-68 / OWN-88).
struct ScreenshotEditorCropPrefs {
    bool keepAspectRatio = false;
    double aspectRatio = 0.0;
};

// Dual-write hover-magnifier prefs (Stage 2 S-B OWN-68 / OWN-88).
struct ScreenshotEditorHoverMagnifierPrefs {
    bool enabled = false;
    int power = 11;
    int colorFormat = 3;
    bool showCoord = true;
    // OWN-88: M-key user toggle (legacy write authority).
    bool userEnabled = true;
};

// Dual-write color palette indices (Stage 2 S-B OWN-68).
// Custom COLORREF stays in toolStyle; these are preset-palette indices.
struct ScreenshotEditorColorIndices {
    int colorIndex = 0;
    int geometryColorIndex = 2;
    int markerColorIndex = 2;
};

// Dual-write function-area layout prefs (Stage 2 S-B OWN-68).
// Serialized as comma/token strings matching ScreenshotSettings.
struct ScreenshotEditorFunctionAreaPrefs {
    std::wstring alwaysShow;
    std::wstring morePanel;
    std::wstring alwaysHide;
};

// OWN-89: dual-write border/shadow chrome toggles + color-picker HSV/mode
// (Overlay write authority; pure header has no COLORREF dependency).
struct ScreenshotEditorChromeToggles {
    bool borderEnabled = false;
    bool shadowEnabled = false;
};

struct ScreenshotEditorColorPickerState {
    int mode = 0;          // 0..2 product picker mode
    int hue = 45;          // 0..359
    int saturation = 58;   // 0..100
    int value = 89;        // 0..100
};

struct ScreenshotEditorToolbarPanelState {
    bool morePanelOpen = false;
    ScreenshotToolbarCommand openToolGroup = ScreenshotToolbarCommand::Confirm;
    ScreenshotToolbarCommand openTertiary = ScreenshotToolbarCommand::Confirm;
};

struct ScreenshotEditorState {
    ScreenshotToolbarCommand activeTool = ScreenshotToolbarCommand::Confirm;
    AnnotationType activeAnnotationType = AnnotationType::None;
    AnnotationRole activeRole = AnnotationRole::Default;
    // S-E-CLOSE-5: selectedAnnotationIndex field deleted; id sole selection authority.
    // Empty = no selection. Document active syncs from this id after Host mutations.
    // Layout index = FindIndexById / ResolveSelectedIndex only (short-life).
    std::wstring selectedAnnotationId;
    int annotationCount = 0;  // dual-write mirror of Overlay vector size
    bool undoAvailable = false;
    bool redoAvailable = false;
    // Dual-write tool style + modes + specialized styles (Stage 2 S-B deepen).
    ScreenshotEditorToolStyle toolStyle;
    ScreenshotEditorToolModes toolModes;
    ScreenshotEditorTextStyle textStyle;
    ScreenshotEditorWatermarkStyle watermarkStyle;
    ScreenshotEditorHighLightStyle highLightStyle;
    ScreenshotEditorMagnifierStyle magnifierStyle;
    ScreenshotEditorToolGroupMemory toolGroupMemory;
    ScreenshotEditorPostProcessStyle postProcessStyle;
    // OWN-68 remaining dual-write aggregates.
    ScreenshotEditorEffectStyle effectStyle;
    ScreenshotEditorCropPrefs cropPrefs;
    ScreenshotEditorHoverMagnifierPrefs hoverMagnifierPrefs;
    ScreenshotEditorColorIndices colorIndices;
    ScreenshotEditorFunctionAreaPrefs functionAreaPrefs;
    // OWN-89: dual-write border/shadow chrome + color-picker HSV/mode.
    ScreenshotEditorChromeToggles chromeToggles;
    ScreenshotEditorColorPickerState colorPicker;
    // OWN-81: dual-write text edit caret / selection anchor (Overlay write authority).
    int textCaretIndex = 0;
    int textSelectionAnchor = -1;  // -1 = no selection
    // OWN-82 / S-E-CLOSE-5: editingTextIndex field deleted; id sole text-edit authority.
    // S-E-52: text-edit stable id sole. Layout index = ResolveTextEditingIndex only.
    std::wstring editingTextId;
    ScreenshotEditorToolbarPanelState toolbarPanels;
    // OWN-84: dual-write slider / color-picker drag interaction.
    bool isDraggingSlider = false;
    ScreenshotToolbarCommand draggingSlider = ScreenshotToolbarCommand::Confirm;
    // Drag rect stored as LTRB ints (no RECT dependency in pure header).
    int sliderDragLeft = 0;
    int sliderDragTop = 0;
    int sliderDragRight = 0;
    int sliderDragBottom = 0;
    bool isDraggingColorPicker = false;
    ScreenshotToolbarCommand draggingColorPicker = ScreenshotToolbarCommand::Confirm;
    int colorPickerDragLeft = 0;
    int colorPickerDragTop = 0;
    int colorPickerDragRight = 0;
    int colorPickerDragBottom = 0;
    // OWN-85: dual-write annotation draw / move / resize / rotate / hold interaction.
    bool isDrawingAnnotation = false;
    bool isDrawingBrokenLinePath = false;
    bool isMovingAnnotation = false;
    bool isResizingAnnotation = false;
    bool isRotatingAnnotation = false;
    bool isHoldingRefresh = false;
    // OWN-86: dual-write hover toolbar / side / tooltip / toast / active handle.
    ScreenshotToolbarCommand hoveredToolbarButton = ScreenshotToolbarCommand::Confirm;
    ScreenshotToolbarCommand hoveredSideButton = ScreenshotToolbarCommand::Confirm;
    bool toolbarTooltipVisible = false;
    std::wstring toastText;
    unsigned int toastStartTick = 0;
    ScreenshotAnnotationHandle activeAnnotationHandle = ScreenshotAnnotationHandle::None;
    int activeAnnotationPointIndex = -1;
    // OWN-87: dual-write drawing points + move/resize originals + tool-settings dirty.
    // Points stored as plain ints (no POINT/RECT dependency in pure header).
    int annotationStartX = 0;
    int annotationStartY = 0;
    int annotationCurrentX = 0;
    int annotationCurrentY = 0;
    int annotationMoveAnchorX = 0;
    int annotationMoveAnchorY = 0;
    int annotationOriginalStartX = 0;
    int annotationOriginalStartY = 0;
    int annotationOriginalEndX = 0;
    int annotationOriginalEndY = 0;
    int annotationOriginalAuxX = 0;
    int annotationOriginalAuxY = 0;
    int annotationOriginalSourceStartX = 0;
    int annotationOriginalSourceStartY = 0;
    int annotationOriginalSourceEndX = 0;
    int annotationOriginalSourceEndY = 0;
    int annotationResizeFixedX = 0;
    int annotationResizeFixedY = 0;
    int annotationOriginalRoundedRadius = 0;
    double annotationOriginalAngle = 0.0;
    double annotationOriginalTextFontSize = 0.0;
    double annotationRotateStartMouseAngle = 0.0;
    bool toolSettingsDirty = false;
    // OWN-88: dual-write last hover-magnifier cache (no POINT/RECT in pure header).
    int lastHoverMagnifierPointX = -1;
    int lastHoverMagnifierPointY = -1;
    int lastHoverMagnifierRectLeft = 0;
    int lastHoverMagnifierRectTop = 0;
    int lastHoverMagnifierRectRight = 0;
    int lastHoverMagnifierRectBottom = 0;
    unsigned int lastHoverMagnifierUpdateTick = 0;
    // OWN-90: dual-write pending text-create id + toolbar rect LTRB.
    std::wstring pendingTextAnnotationCreateId;
    int toolbarRectLeft = 0;
    int toolbarRectTop = 0;
    int toolbarRectRight = 0;
    int toolbarRectBottom = 0;
    // OWN-91: dual-write freehand/broken-line path counts + smart-hover flags.
    int freehandPointCount = 0;
    int brokenLinePointCount = 0;
    bool hasSmartRect = false;
    bool wheelSelectionLocked = false;
    bool needFullRedraw = true;
    // OWN-91: dual-write crop-drag session (no POINT; LTRB ints + ordinals).
    bool isCropDragging = false;
    int cropStartX = 0;
    int cropStartY = 0;
    int cropCurrentX = 0;
    int cropCurrentY = 0;
    int cropClickStartX = 0;
    int cropClickStartY = 0;
    int adjustActionOrdinal = 0;  // AdjustAction as int
    int lastSmartPointX = -1;
    int lastSmartPointY = -1;
    // OWN-91: dual-write smart-detection request cache + hovered toolbar label/rect.
    int lastSmartDetectionRequestX = -1;
    int lastSmartDetectionRequestY = -1;
    unsigned int lastSmartDetectionRequestTick = 0;
    int hoveredToolbarRectLeft = 0;
    int hoveredToolbarRectTop = 0;
    int hoveredToolbarRectRight = 0;
    int hoveredToolbarRectBottom = 0;
    std::wstring hoveredToolbarLabel;
    // OWN-92: dual-write crop/smart geometry rects + adjust/lastDrawn (LTRB ints).
    int cropRectLeft = 0;
    int cropRectTop = 0;
    int cropRectRight = 0;
    int cropRectBottom = 0;
    int smartRectLeft = 0;
    int smartRectTop = 0;
    int smartRectRight = 0;
    int smartRectBottom = 0;
    int adjustAnchorX = 0;
    int adjustAnchorY = 0;
    int adjustStartRectLeft = 0;
    int adjustStartRectTop = 0;
    int adjustStartRectRight = 0;
    int adjustStartRectBottom = 0;
    int lastDrawnRectLeft = 0;
    int lastDrawnRectTop = 0;
    int lastDrawnRectRight = 0;
    int lastDrawnRectBottom = 0;
    bool lastDrawnWasSmart = false;
    int smartSelectionSuppressedX = -1;
    int smartSelectionSuppressedY = -1;
    bool isScreenshotMode = false;
    // OWN-93: dual-write screen/target/hovered/pending geometry (LTRB ints; no HWND).
    int screenRectLeft = 0;
    int screenRectTop = 0;
    int screenRectRight = 0;
    int screenRectBottom = 0;
    int targetRectLeft = 0;
    int targetRectTop = 0;
    int targetRectRight = 0;
    int targetRectBottom = 0;
    int hoveredRectLeft = 0;
    int hoveredRectTop = 0;
    int hoveredRectRight = 0;
    int hoveredRectBottom = 0;
    int pendingCropRectLeft = 0;
    int pendingCropRectTop = 0;
    int pendingCropRectRight = 0;
    int pendingCropRectBottom = 0;
    bool hasHoveredWindow = false;
};

inline void ScreenshotEditorSelectTool(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand tool)
{
    state.activeTool = tool;
    state.activeAnnotationType = ToolCommandToAnnotationType(tool);
    // Role stays Default unless tool is a role-backed specialization applied by callers.
    if (state.activeAnnotationType == AnnotationType::None) {
        state.activeRole = AnnotationRole::Default;
    }
}

inline void ScreenshotEditorSetRole(
    ScreenshotEditorState& state,
    AnnotationRole role)
{
    state.activeRole = role;
}

// S-E-CLOSE-5: select by id sole. Index param is layout hint only (not stored).
// index < 0 → clear. index >= 0 + non-empty id → set id. index >= 0 + empty id → leave id.
inline void ScreenshotEditorSelectAnnotation(
    ScreenshotEditorState& state,
    int index,
    const std::wstring& id = L"")
{
    if (index < 0) {
        state.selectedAnnotationId.clear();
        return;
    }
    if (!id.empty()) {
        state.selectedAnnotationId = id;
    }
}

inline void ScreenshotEditorSelectAnnotationById(
    ScreenshotEditorState& state,
    int /*index*/,
    const std::wstring& id)
{
    // index ignored for storage (S-E-CLOSE-5); id sole. Empty id clears.
    state.selectedAnnotationId = id;
}

// S-E-CLOSE-5: clamp by count only — clear id when empty store; stale id cleared at Resolve.
inline void ScreenshotEditorClampSelection(
    ScreenshotEditorState& state,
    int annotationCount)
{
    if (annotationCount <= 0) {
        state.selectedAnnotationId.clear();
    }
    // Non-empty count: keep id; ResolveSelectedIndex maps id → layout or -1 if missing.
}

// Dual-write: mirror Overlay vector size into pure state and clamp selection.
inline void ScreenshotEditorSetAnnotationCount(
    ScreenshotEditorState& state,
    int annotationCount)
{
    state.annotationCount = annotationCount < 0 ? 0 : annotationCount;
    ScreenshotEditorClampSelection(state, state.annotationCount);
}

// S-E-2: sole select path (Host SetSelectedScreenshotAnnotationIndex deleted).
// S-E-8: optional stable id dual-write for Document active sync.
// Project annotation count, clamp requested index into [0, count), select.
inline void ScreenshotEditorSetAnnotationCountAndSelect(
    ScreenshotEditorState& state,
    int annotationCount,
    int requestedIndex,
    const std::wstring& selectedId = L"")
{
    ScreenshotEditorSetAnnotationCount(state, annotationCount);
    if (requestedIndex < 0 || state.annotationCount <= 0) {
        ScreenshotEditorSelectAnnotationById(state, -1, L"");
        return;
    }
    int index = requestedIndex;
    if (index >= state.annotationCount) {
        index = state.annotationCount - 1;
    }
    ScreenshotEditorSelectAnnotationById(state, index, selectedId);
}

inline const std::wstring& ScreenshotEditorSelectedAnnotationId(
    const ScreenshotEditorState& state)
{
    return state.selectedAnnotationId;
}

inline void ScreenshotEditorSetHistoryAvailability(
    ScreenshotEditorState& state,
    bool undoAvailable,
    bool redoAvailable)
{
    state.undoAvailable = undoAvailable;
    state.redoAvailable = redoAvailable;
}

// S-E-3: sole active-tool path (Host SetActiveScreenshotTool deleted).
// Select tool + project Host history availability into pure state.
inline void ScreenshotEditorSelectToolWithHistory(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand tool,
    bool undoAvailable,
    bool redoAvailable)
{
    ScreenshotEditorSelectTool(state, tool);
    ScreenshotEditorSetHistoryAvailability(state, undoAvailable, redoAvailable);
}

// S-E-CLOSE-5: selection presence = non-empty selectedAnnotationId only.
inline bool ScreenshotEditorHasSelection(const ScreenshotEditorState& state)
{
    return !state.selectedAnnotationId.empty();
}

// S-E-CLOSE-5: index field deleted. Getter always -1 (layout via ResolveSelectedIndex + Host).
// Kept for transitional call sites; prefer ResolveSelectedIndex.
inline int ScreenshotEditorSelectedAnnotationIndex(const ScreenshotEditorState& /*state*/)
{
    return -1;
}

inline ScreenshotToolbarCommand ScreenshotEditorActiveTool(const ScreenshotEditorState& state)
{
    return state.activeTool;
}

// OWN-108: pure read of active annotation type / role (legacy write via SelectTool/SetRole).
inline AnnotationType ScreenshotEditorActiveAnnotationType(const ScreenshotEditorState& state)
{
    return state.activeAnnotationType;
}

inline AnnotationRole ScreenshotEditorActiveRole(const ScreenshotEditorState& state)
{
    return state.activeRole;
}

inline int ScreenshotEditorAnnotationCount(const ScreenshotEditorState& state)
{
    return state.annotationCount;
}

inline bool ScreenshotEditorUndoAvailable(const ScreenshotEditorState& state)
{
    return state.undoAvailable;
}

inline bool ScreenshotEditorRedoAvailable(const ScreenshotEditorState& state)
{
    return state.redoAvailable;
}

// OWN-106: pure read of slider / color-picker drag LTRB (legacy write → Sync*Drag).
inline int ScreenshotEditorSliderDragLeft(const ScreenshotEditorState& state) { return state.sliderDragLeft; }
inline int ScreenshotEditorSliderDragTop(const ScreenshotEditorState& state) { return state.sliderDragTop; }
inline int ScreenshotEditorSliderDragRight(const ScreenshotEditorState& state) { return state.sliderDragRight; }
inline int ScreenshotEditorSliderDragBottom(const ScreenshotEditorState& state) { return state.sliderDragBottom; }
inline int ScreenshotEditorColorPickerDragLeft(const ScreenshotEditorState& state) { return state.colorPickerDragLeft; }
inline int ScreenshotEditorColorPickerDragTop(const ScreenshotEditorState& state) { return state.colorPickerDragTop; }
inline int ScreenshotEditorColorPickerDragRight(const ScreenshotEditorState& state) { return state.colorPickerDragRight; }
inline int ScreenshotEditorColorPickerDragBottom(const ScreenshotEditorState& state) { return state.colorPickerDragBottom; }

// Live-path consumers of pure state (Stage 2 S-B dual-write consume seam).
inline bool ScreenshotEditorIsActiveTool(
    const ScreenshotEditorState& state,
    ScreenshotToolbarCommand command)
{
    return state.activeTool == command;
}

// Drawing tools map to a non-None AnnotationType via ToolCommandToAnnotationType.
// Role-backed tools (HighLight/Watermark/Magnifier/AutoMosaic) still report None type;
// treat those via activeTool identity when command is the active tool itself.
inline bool ScreenshotEditorHasDrawingTool(const ScreenshotEditorState& state)
{
    if (state.activeAnnotationType != AnnotationType::None) return true;
    switch (state.activeTool) {
    case ScreenshotToolbarCommand::ToolHighLight:
    case ScreenshotToolbarCommand::ToolWatermark:
    case ScreenshotToolbarCommand::ToolMagnifier:
    case ScreenshotToolbarCommand::ToolAutoMosaic:
        return true;
    default:
        return false;
    }
}

// Dual-write: effect / crop / hover-magnifier / color indices (OWN-68).
inline void ScreenshotEditorSyncEffectStyle(
    ScreenshotEditorState& state,
    const ScreenshotEditorEffectStyle& style)
{
    state.effectStyle = style;
}

inline const ScreenshotEditorEffectStyle& ScreenshotEditorEffectStyleOf(
    const ScreenshotEditorState& state)
{
    return state.effectStyle;
}

inline void ScreenshotEditorSyncCropPrefs(
    ScreenshotEditorState& state,
    const ScreenshotEditorCropPrefs& prefs)
{
    state.cropPrefs = prefs;
}

inline const ScreenshotEditorCropPrefs& ScreenshotEditorCropPrefsOf(
    const ScreenshotEditorState& state)
{
    return state.cropPrefs;
}

inline bool ScreenshotEditorIsKeepAspectRatio(const ScreenshotEditorState& state)
{
    return state.cropPrefs.keepAspectRatio;
}

inline double ScreenshotEditorAspectRatio(const ScreenshotEditorState& state)
{
    return state.cropPrefs.aspectRatio;
}

// S-B-30: derive aspect ratio from pure cropRect (Host Sync method deleted).
inline void ScreenshotEditorSyncAspectRatioFromCropRect(ScreenshotEditorState& state)
{
    const int width = state.cropRectRight - state.cropRectLeft;
    const int height = state.cropRectBottom - state.cropRectTop;
    if (width > 0 && height > 0) {
        state.cropPrefs.aspectRatio = static_cast<double>(width) / static_cast<double>(height);
    }
}

inline void ScreenshotEditorSyncHoverMagnifierPrefs(
    ScreenshotEditorState& state,
    const ScreenshotEditorHoverMagnifierPrefs& prefs)
{
    state.hoverMagnifierPrefs = prefs;
}

inline const ScreenshotEditorHoverMagnifierPrefs& ScreenshotEditorHoverMagnifierPrefsOf(
    const ScreenshotEditorState& state)
{
    return state.hoverMagnifierPrefs;
}

inline bool ScreenshotEditorIsHoverMagnifierEnabled(const ScreenshotEditorState& state)
{
    return state.hoverMagnifierPrefs.enabled;
}

// OWN-88: M-key user toggle pure read (legacy write authority).
inline bool ScreenshotEditorIsHoverMagnifierUserEnabled(const ScreenshotEditorState& state)
{
    return state.hoverMagnifierPrefs.userEnabled;
}

// OWN-88: dual-write last hover-magnifier cache (point/rect/tick; no POINT/RECT).
inline void ScreenshotEditorSyncLastHoverMagnifierCache(
    ScreenshotEditorState& state,
    int pointX, int pointY,
    int rectLeft, int rectTop, int rectRight, int rectBottom,
    unsigned int updateTick)
{
    state.lastHoverMagnifierPointX = pointX;
    state.lastHoverMagnifierPointY = pointY;
    state.lastHoverMagnifierRectLeft = rectLeft;
    state.lastHoverMagnifierRectTop = rectTop;
    state.lastHoverMagnifierRectRight = rectRight;
    state.lastHoverMagnifierRectBottom = rectBottom;
    state.lastHoverMagnifierUpdateTick = updateTick;
}

inline int ScreenshotEditorLastHoverMagnifierPointX(const ScreenshotEditorState& state)
{
    return state.lastHoverMagnifierPointX;
}

inline int ScreenshotEditorLastHoverMagnifierPointY(const ScreenshotEditorState& state)
{
    return state.lastHoverMagnifierPointY;
}

inline unsigned int ScreenshotEditorLastHoverMagnifierUpdateTick(
    const ScreenshotEditorState& state)
{
    return state.lastHoverMagnifierUpdateTick;
}

inline int ScreenshotEditorLastHoverMagnifierRectLeft(const ScreenshotEditorState& state)
{
    return state.lastHoverMagnifierRectLeft;
}

inline int ScreenshotEditorLastHoverMagnifierRectTop(const ScreenshotEditorState& state)
{
    return state.lastHoverMagnifierRectTop;
}

inline int ScreenshotEditorLastHoverMagnifierRectRight(const ScreenshotEditorState& state)
{
    return state.lastHoverMagnifierRectRight;
}

inline int ScreenshotEditorLastHoverMagnifierRectBottom(const ScreenshotEditorState& state)
{
    return state.lastHoverMagnifierRectBottom;
}

// OWN-90: dual-write pending text-create id + toolbar rect LTRB (legacy write authority).
inline void ScreenshotEditorSyncPendingTextAnnotationCreateId(
    ScreenshotEditorState& state,
    const std::wstring& id)
{
    state.pendingTextAnnotationCreateId = id;
}

inline const std::wstring& ScreenshotEditorPendingTextAnnotationCreateId(
    const ScreenshotEditorState& state)
{
    return state.pendingTextAnnotationCreateId;
}

inline bool ScreenshotEditorHasPendingTextAnnotationCreate(
    const ScreenshotEditorState& state)
{
    return !state.pendingTextAnnotationCreateId.empty();
}

inline void ScreenshotEditorSyncToolbarRect(
    ScreenshotEditorState& state,
    int left, int top, int right, int bottom)
{
    state.toolbarRectLeft = left;
    state.toolbarRectTop = top;
    state.toolbarRectRight = right;
    state.toolbarRectBottom = bottom;
}

inline int ScreenshotEditorToolbarRectLeft(const ScreenshotEditorState& state)
{
    return state.toolbarRectLeft;
}

inline int ScreenshotEditorToolbarRectTop(const ScreenshotEditorState& state)
{
    return state.toolbarRectTop;
}

inline int ScreenshotEditorToolbarRectRight(const ScreenshotEditorState& state)
{
    return state.toolbarRectRight;
}

inline int ScreenshotEditorToolbarRectBottom(const ScreenshotEditorState& state)
{
    return state.toolbarRectBottom;
}

// OWN-91: dual-write freehand/broken-line path counts + smart-hover flags.
inline void ScreenshotEditorSyncPathPointCounts(
    ScreenshotEditorState& state,
    int freehandPointCount,
    int brokenLinePointCount)
{
    state.freehandPointCount = freehandPointCount;
    state.brokenLinePointCount = brokenLinePointCount;
}

inline int ScreenshotEditorFreehandPointCount(const ScreenshotEditorState& state)
{
    return state.freehandPointCount;
}

inline int ScreenshotEditorBrokenLinePointCount(const ScreenshotEditorState& state)
{
    return state.brokenLinePointCount;
}

inline bool ScreenshotEditorHasFreehandPoints(const ScreenshotEditorState& state)
{
    return state.freehandPointCount > 0;
}

inline bool ScreenshotEditorHasBrokenLinePoints(const ScreenshotEditorState& state)
{
    return state.brokenLinePointCount > 0;
}

inline void ScreenshotEditorSyncSmartHoverFlags(
    ScreenshotEditorState& state,
    bool hasSmartRect,
    bool wheelSelectionLocked,
    bool needFullRedraw)
{
    state.hasSmartRect = hasSmartRect;
    state.wheelSelectionLocked = wheelSelectionLocked;
    state.needFullRedraw = needFullRedraw;
}

inline bool ScreenshotEditorHasSmartRect(const ScreenshotEditorState& state)
{
    return state.hasSmartRect;
}

inline bool ScreenshotEditorIsWheelSelectionLocked(const ScreenshotEditorState& state)
{
    return state.wheelSelectionLocked;
}

inline bool ScreenshotEditorNeedsFullRedraw(const ScreenshotEditorState& state)
{
    return state.needFullRedraw;
}

// S-B-14: sole-authority writers (Host dual-write fields deleted).
inline void ScreenshotEditorSetHasSmartRect(ScreenshotEditorState& state, bool value)
{
    state.hasSmartRect = value;
}
inline void ScreenshotEditorSetWheelSelectionLocked(ScreenshotEditorState& state, bool value)
{
    state.wheelSelectionLocked = value;
}
inline void ScreenshotEditorSetNeedFullRedraw(ScreenshotEditorState& state, bool value)
{
    state.needFullRedraw = value;
}

// OWN-91: dual-write crop-drag session (legacy write authority).
inline void ScreenshotEditorSyncCropDragSession(
    ScreenshotEditorState& state,
    bool isCropDragging,
    int cropStartX, int cropStartY,
    int cropCurrentX, int cropCurrentY,
    int cropClickStartX, int cropClickStartY,
    int adjustActionOrdinal,
    int lastSmartPointX, int lastSmartPointY)
{
    state.isCropDragging = isCropDragging;
    state.cropStartX = cropStartX;
    state.cropStartY = cropStartY;
    state.cropCurrentX = cropCurrentX;
    state.cropCurrentY = cropCurrentY;
    state.cropClickStartX = cropClickStartX;
    state.cropClickStartY = cropClickStartY;
    state.adjustActionOrdinal = adjustActionOrdinal;
    state.lastSmartPointX = lastSmartPointX;
    state.lastSmartPointY = lastSmartPointY;
}

inline bool ScreenshotEditorIsCropDragging(const ScreenshotEditorState& state)
{
    return state.isCropDragging;
}

inline int ScreenshotEditorCropStartX(const ScreenshotEditorState& state) { return state.cropStartX; }
inline int ScreenshotEditorCropStartY(const ScreenshotEditorState& state) { return state.cropStartY; }
inline int ScreenshotEditorCropCurrentX(const ScreenshotEditorState& state) { return state.cropCurrentX; }
inline int ScreenshotEditorCropCurrentY(const ScreenshotEditorState& state) { return state.cropCurrentY; }
inline int ScreenshotEditorCropClickStartX(const ScreenshotEditorState& state) { return state.cropClickStartX; }
inline int ScreenshotEditorCropClickStartY(const ScreenshotEditorState& state) { return state.cropClickStartY; }
inline int ScreenshotEditorAdjustActionOrdinal(const ScreenshotEditorState& state) { return state.adjustActionOrdinal; }
inline int ScreenshotEditorLastSmartPointX(const ScreenshotEditorState& state) { return state.lastSmartPointX; }
inline int ScreenshotEditorLastSmartPointY(const ScreenshotEditorState& state) { return state.lastSmartPointY; }

// OWN-91: dual-write smart-detection request cache (legacy write authority).
inline void ScreenshotEditorSyncSmartDetectionRequest(
    ScreenshotEditorState& state,
    int requestX, int requestY,
    unsigned int requestTick)
{
    state.lastSmartDetectionRequestX = requestX;
    state.lastSmartDetectionRequestY = requestY;
    state.lastSmartDetectionRequestTick = requestTick;
}

inline int ScreenshotEditorLastSmartDetectionRequestX(const ScreenshotEditorState& state)
{
    return state.lastSmartDetectionRequestX;
}

inline int ScreenshotEditorLastSmartDetectionRequestY(const ScreenshotEditorState& state)
{
    return state.lastSmartDetectionRequestY;
}

inline unsigned int ScreenshotEditorLastSmartDetectionRequestTick(
    const ScreenshotEditorState& state)
{
    return state.lastSmartDetectionRequestTick;
}

// OWN-91: dual-write hovered toolbar label + rect LTRB (legacy write authority).
inline void ScreenshotEditorSyncHoveredToolbarChrome(
    ScreenshotEditorState& state,
    int rectLeft, int rectTop, int rectRight, int rectBottom,
    const std::wstring& label)
{
    state.hoveredToolbarRectLeft = rectLeft;
    state.hoveredToolbarRectTop = rectTop;
    state.hoveredToolbarRectRight = rectRight;
    state.hoveredToolbarRectBottom = rectBottom;
    state.hoveredToolbarLabel = label;
}

inline int ScreenshotEditorHoveredToolbarRectLeft(const ScreenshotEditorState& state)
{
    return state.hoveredToolbarRectLeft;
}

inline int ScreenshotEditorHoveredToolbarRectTop(const ScreenshotEditorState& state)
{
    return state.hoveredToolbarRectTop;
}

inline int ScreenshotEditorHoveredToolbarRectRight(const ScreenshotEditorState& state)
{
    return state.hoveredToolbarRectRight;
}

inline int ScreenshotEditorHoveredToolbarRectBottom(const ScreenshotEditorState& state)
{
    return state.hoveredToolbarRectBottom;
}

inline const std::wstring& ScreenshotEditorHoveredToolbarLabel(
    const ScreenshotEditorState& state)
{
    return state.hoveredToolbarLabel;
}

inline bool ScreenshotEditorHasHoveredToolbarLabel(const ScreenshotEditorState& state)
{
    return !state.hoveredToolbarLabel.empty();
}

// S-B-24: lastDrawn geometry sole on pure state.
inline void ScreenshotEditorSyncLastDrawn(
    ScreenshotEditorState& state,
    int lastDrawnL, int lastDrawnT, int lastDrawnR, int lastDrawnB,
    bool lastDrawnWasSmart)
{
    state.lastDrawnRectLeft = lastDrawnL;
    state.lastDrawnRectTop = lastDrawnT;
    state.lastDrawnRectRight = lastDrawnR;
    state.lastDrawnRectBottom = lastDrawnB;
    state.lastDrawnWasSmart = lastDrawnWasSmart;
}

// S-B-25: smart-selection suppressed point sole on pure state.
inline void ScreenshotEditorSyncSmartSelectionSuppressed(
    ScreenshotEditorState& state,
    int smartSuppressedX, int smartSuppressedY)
{
    state.smartSelectionSuppressedX = smartSuppressedX;
    state.smartSelectionSuppressedY = smartSuppressedY;
}

// S-B-26: smartRect sole on pure state.
inline void ScreenshotEditorSyncSmartRect(
    ScreenshotEditorState& state,
    int smartL, int smartT, int smartR, int smartB)
{
    state.smartRectLeft = smartL;
    state.smartRectTop = smartT;
    state.smartRectRight = smartR;
    state.smartRectBottom = smartB;
}

// S-B-27: adjust session (anchor + start rect) sole on pure state.
inline void ScreenshotEditorSyncAdjustSession(
    ScreenshotEditorState& state,
    int adjustAnchorX, int adjustAnchorY,
    int adjustStartL, int adjustStartT, int adjustStartR, int adjustStartB)
{
    state.adjustAnchorX = adjustAnchorX;
    state.adjustAnchorY = adjustAnchorY;
    state.adjustStartRectLeft = adjustStartL;
    state.adjustStartRectTop = adjustStartT;
    state.adjustStartRectRight = adjustStartR;
    state.adjustStartRectBottom = adjustStartB;
}

// S-B-28: cropRect sole on pure state (final OWN-92 dual field).
inline void ScreenshotEditorSyncCropRect(
    ScreenshotEditorState& state,
    int cropL, int cropT, int cropR, int cropB)
{
    state.cropRectLeft = cropL;
    state.cropRectTop = cropT;
    state.cropRectRight = cropR;
    state.cropRectBottom = cropB;
}

inline int ScreenshotEditorCropRectLeft(const ScreenshotEditorState& state) { return state.cropRectLeft; }
inline int ScreenshotEditorCropRectTop(const ScreenshotEditorState& state) { return state.cropRectTop; }
inline int ScreenshotEditorCropRectRight(const ScreenshotEditorState& state) { return state.cropRectRight; }
inline int ScreenshotEditorCropRectBottom(const ScreenshotEditorState& state) { return state.cropRectBottom; }
inline int ScreenshotEditorSmartRectLeft(const ScreenshotEditorState& state) { return state.smartRectLeft; }
inline int ScreenshotEditorSmartRectTop(const ScreenshotEditorState& state) { return state.smartRectTop; }
inline int ScreenshotEditorSmartRectRight(const ScreenshotEditorState& state) { return state.smartRectRight; }
inline int ScreenshotEditorSmartRectBottom(const ScreenshotEditorState& state) { return state.smartRectBottom; }
inline int ScreenshotEditorAdjustAnchorX(const ScreenshotEditorState& state) { return state.adjustAnchorX; }
inline int ScreenshotEditorAdjustAnchorY(const ScreenshotEditorState& state) { return state.adjustAnchorY; }
inline int ScreenshotEditorAdjustStartRectLeft(const ScreenshotEditorState& state) { return state.adjustStartRectLeft; }
inline int ScreenshotEditorAdjustStartRectTop(const ScreenshotEditorState& state) { return state.adjustStartRectTop; }
inline int ScreenshotEditorAdjustStartRectRight(const ScreenshotEditorState& state) { return state.adjustStartRectRight; }
inline int ScreenshotEditorAdjustStartRectBottom(const ScreenshotEditorState& state) { return state.adjustStartRectBottom; }
inline int ScreenshotEditorLastDrawnRectLeft(const ScreenshotEditorState& state) { return state.lastDrawnRectLeft; }
inline int ScreenshotEditorLastDrawnRectTop(const ScreenshotEditorState& state) { return state.lastDrawnRectTop; }
inline int ScreenshotEditorLastDrawnRectRight(const ScreenshotEditorState& state) { return state.lastDrawnRectRight; }
inline int ScreenshotEditorLastDrawnRectBottom(const ScreenshotEditorState& state) { return state.lastDrawnRectBottom; }
inline bool ScreenshotEditorLastDrawnWasSmart(const ScreenshotEditorState& state) { return state.lastDrawnWasSmart; }
inline int ScreenshotEditorSmartSelectionSuppressedX(const ScreenshotEditorState& state) { return state.smartSelectionSuppressedX; }
inline int ScreenshotEditorSmartSelectionSuppressedY(const ScreenshotEditorState& state) { return state.smartSelectionSuppressedY; }
inline bool ScreenshotEditorIsScreenshotMode(const ScreenshotEditorState& state) { return state.isScreenshotMode; }

// S-B-23: isScreenshotMode sole on pure state (no Host dual field).
inline void ScreenshotEditorSetIsScreenshotMode(ScreenshotEditorState& state, bool isScreenshotMode)
{
    state.isScreenshotMode = isScreenshotMode;
}

// S-B-21: pure geometry views for multi-TU Host cutover (no dual Host RECT fields).
// POD only — no windows.h. Host TUs that include windows.h get implicit RECT/POINT conversion.
struct ScreenshotEditorRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
#ifdef _WINDEF_
    operator RECT() const
    {
        return RECT{left, top, right, bottom};
    }
#endif
};

struct ScreenshotEditorPoint {
    int x = 0;
    int y = 0;
#ifdef _WINDEF_
    operator POINT() const
    {
        return POINT{x, y};
    }
#endif
};

inline ScreenshotEditorRect ScreenshotEditorCropRect(const ScreenshotEditorState& state)
{
    return ScreenshotEditorRect{
        state.cropRectLeft, state.cropRectTop, state.cropRectRight, state.cropRectBottom};
}
inline ScreenshotEditorRect ScreenshotEditorSmartRect(const ScreenshotEditorState& state)
{
    return ScreenshotEditorRect{
        state.smartRectLeft, state.smartRectTop, state.smartRectRight, state.smartRectBottom};
}
inline ScreenshotEditorPoint ScreenshotEditorAdjustAnchor(const ScreenshotEditorState& state)
{
    return ScreenshotEditorPoint{state.adjustAnchorX, state.adjustAnchorY};
}
inline ScreenshotEditorRect ScreenshotEditorAdjustStartRect(const ScreenshotEditorState& state)
{
    return ScreenshotEditorRect{
        state.adjustStartRectLeft, state.adjustStartRectTop,
        state.adjustStartRectRight, state.adjustStartRectBottom};
}
inline ScreenshotEditorRect ScreenshotEditorLastDrawnRect(const ScreenshotEditorState& state)
{
    return ScreenshotEditorRect{
        state.lastDrawnRectLeft, state.lastDrawnRectTop,
        state.lastDrawnRectRight, state.lastDrawnRectBottom};
}
inline ScreenshotEditorPoint ScreenshotEditorSmartSelectionSuppressedPoint(
    const ScreenshotEditorState& state)
{
    return ScreenshotEditorPoint{state.smartSelectionSuppressedX, state.smartSelectionSuppressedY};
}

// OWN-93 / S-B-21: screen/target/hovered/pending geometry sole on pure state.
// hasHoveredWindow mirrors HWND presence without storing HWND in pure state.
inline void ScreenshotEditorSyncScreenHoverGeometry(
    ScreenshotEditorState& state,
    int screenL, int screenT, int screenR, int screenB,
    int targetL, int targetT, int targetR, int targetB,
    int hoveredL, int hoveredT, int hoveredR, int hoveredB,
    int pendingL, int pendingT, int pendingR, int pendingB,
    bool hasHoveredWindow)
{
    state.screenRectLeft = screenL;
    state.screenRectTop = screenT;
    state.screenRectRight = screenR;
    state.screenRectBottom = screenB;
    state.targetRectLeft = targetL;
    state.targetRectTop = targetT;
    state.targetRectRight = targetR;
    state.targetRectBottom = targetB;
    state.hoveredRectLeft = hoveredL;
    state.hoveredRectTop = hoveredT;
    state.hoveredRectRight = hoveredR;
    state.hoveredRectBottom = hoveredB;
    state.pendingCropRectLeft = pendingL;
    state.pendingCropRectTop = pendingT;
    state.pendingCropRectRight = pendingR;
    state.pendingCropRectBottom = pendingB;
    state.hasHoveredWindow = hasHoveredWindow;
}

inline int ScreenshotEditorScreenRectLeft(const ScreenshotEditorState& state) { return state.screenRectLeft; }
inline int ScreenshotEditorScreenRectTop(const ScreenshotEditorState& state) { return state.screenRectTop; }
inline int ScreenshotEditorScreenRectRight(const ScreenshotEditorState& state) { return state.screenRectRight; }
inline int ScreenshotEditorScreenRectBottom(const ScreenshotEditorState& state) { return state.screenRectBottom; }
inline int ScreenshotEditorTargetRectLeft(const ScreenshotEditorState& state) { return state.targetRectLeft; }
inline int ScreenshotEditorTargetRectTop(const ScreenshotEditorState& state) { return state.targetRectTop; }
inline int ScreenshotEditorTargetRectRight(const ScreenshotEditorState& state) { return state.targetRectRight; }
inline int ScreenshotEditorTargetRectBottom(const ScreenshotEditorState& state) { return state.targetRectBottom; }
inline int ScreenshotEditorHoveredRectLeft(const ScreenshotEditorState& state) { return state.hoveredRectLeft; }
inline int ScreenshotEditorHoveredRectTop(const ScreenshotEditorState& state) { return state.hoveredRectTop; }
inline int ScreenshotEditorHoveredRectRight(const ScreenshotEditorState& state) { return state.hoveredRectRight; }
inline int ScreenshotEditorHoveredRectBottom(const ScreenshotEditorState& state) { return state.hoveredRectBottom; }
inline int ScreenshotEditorPendingCropRectLeft(const ScreenshotEditorState& state) { return state.pendingCropRectLeft; }
inline int ScreenshotEditorPendingCropRectTop(const ScreenshotEditorState& state) { return state.pendingCropRectTop; }
inline int ScreenshotEditorPendingCropRectRight(const ScreenshotEditorState& state) { return state.pendingCropRectRight; }
inline int ScreenshotEditorPendingCropRectBottom(const ScreenshotEditorState& state) { return state.pendingCropRectBottom; }
inline bool ScreenshotEditorHasHoveredWindow(const ScreenshotEditorState& state) { return state.hasHoveredWindow; }

// S-B-21: pure screen-hover geometry views (Host HWND remains).
// POD only — no windows.h. Host TUs that include windows.h get implicit RECT conversion.
inline ScreenshotEditorRect ScreenshotEditorScreenRect(const ScreenshotEditorState& state)
{
    return ScreenshotEditorRect{
        state.screenRectLeft, state.screenRectTop, state.screenRectRight, state.screenRectBottom};
}
inline ScreenshotEditorRect ScreenshotEditorTargetRect(const ScreenshotEditorState& state)
{
    return ScreenshotEditorRect{
        state.targetRectLeft, state.targetRectTop, state.targetRectRight, state.targetRectBottom};
}
inline ScreenshotEditorRect ScreenshotEditorHoveredRect(const ScreenshotEditorState& state)
{
    return ScreenshotEditorRect{
        state.hoveredRectLeft, state.hoveredRectTop, state.hoveredRectRight, state.hoveredRectBottom};
}
inline ScreenshotEditorRect ScreenshotEditorPendingCropRect(const ScreenshotEditorState& state)
{
    return ScreenshotEditorRect{
        state.pendingCropRectLeft, state.pendingCropRectTop,
        state.pendingCropRectRight, state.pendingCropRectBottom};
}

// S-E-6: pure sole crop geometry getters (Host GetCropRect/GetCropBounds deleted).
// Drag-normalized rect from cropStart/cropCurrent (drawing path).
inline ScreenshotEditorRect ScreenshotEditorCropDragRect(const ScreenshotEditorState& state)
{
    const int l = state.cropStartX < state.cropCurrentX ? state.cropStartX : state.cropCurrentX;
    const int r = state.cropStartX < state.cropCurrentX ? state.cropCurrentX : state.cropStartX;
    const int t = state.cropStartY < state.cropCurrentY ? state.cropStartY : state.cropCurrentY;
    const int b = state.cropStartY < state.cropCurrentY ? state.cropCurrentY : state.cropStartY;
    return ScreenshotEditorRect{l, t, r, b};
}

// Crop bounds: full screen in screenshot mode, else hovered window rect.
inline ScreenshotEditorRect ScreenshotEditorCropBounds(const ScreenshotEditorState& state)
{
    return ScreenshotEditorIsScreenshotMode(state)
        ? ScreenshotEditorScreenRect(state)
        : ScreenshotEditorHoveredRect(state);
}

inline void ScreenshotEditorSyncColorIndices(
    ScreenshotEditorState& state,
    const ScreenshotEditorColorIndices& indices)
{
    state.colorIndices = indices;
}

inline const ScreenshotEditorColorIndices& ScreenshotEditorColorIndicesOf(
    const ScreenshotEditorState& state)
{
    return state.colorIndices;
}

inline int ScreenshotEditorGeometryRoundedRadius(const ScreenshotEditorState& state)
{
    return state.effectStyle.geometryRoundedRadius;
}

inline int ScreenshotEditorMarkerBlendMode(const ScreenshotEditorState& state)
{
    return state.effectStyle.markerBlendMode;
}

inline int ScreenshotEditorMosaicMode(const ScreenshotEditorState& state)
{
    return state.effectStyle.mosaicMode;
}

inline int ScreenshotEditorMosaicStrength(const ScreenshotEditorState& state)
{
    return state.effectStyle.mosaicStrength;
}

inline int ScreenshotEditorSerialType(const ScreenshotEditorState& state)
{
    return state.effectStyle.serialType;
}

inline int ScreenshotEditorSerialCounter(const ScreenshotEditorState& state)
{
    return state.effectStyle.serialCounter;
}

inline bool ScreenshotEditorIsAutoMosaicSync(const ScreenshotEditorState& state)
{
    return state.effectStyle.autoMosaicSync;
}

inline void ScreenshotEditorSyncFunctionAreaPrefs(
    ScreenshotEditorState& state,
    const ScreenshotEditorFunctionAreaPrefs& prefs)
{
    state.functionAreaPrefs = prefs;
}

inline const ScreenshotEditorFunctionAreaPrefs& ScreenshotEditorFunctionAreaPrefsOf(
    const ScreenshotEditorState& state)
{
    return state.functionAreaPrefs;
}

// OWN-89: dual-write border/shadow chrome toggles (legacy write authority).
inline void ScreenshotEditorSyncChromeToggles(
    ScreenshotEditorState& state,
    bool borderEnabled,
    bool shadowEnabled)
{
    state.chromeToggles.borderEnabled = borderEnabled;
    state.chromeToggles.shadowEnabled = shadowEnabled;
}

inline bool ScreenshotEditorIsBorderEnabled(const ScreenshotEditorState& state)
{
    return state.chromeToggles.borderEnabled;
}

inline bool ScreenshotEditorIsShadowEnabled(const ScreenshotEditorState& state)
{
    return state.chromeToggles.shadowEnabled;
}

// OWN-89: dual-write color-picker HSV / mode (legacy write authority).
inline void ScreenshotEditorSyncColorPickerState(
    ScreenshotEditorState& state,
    int mode,
    int hue,
    int saturation,
    int value)
{
    state.colorPicker.mode = mode;
    state.colorPicker.hue = hue;
    state.colorPicker.saturation = saturation;
    state.colorPicker.value = value;
}

inline int ScreenshotEditorColorPickerMode(const ScreenshotEditorState& state)
{
    return state.colorPicker.mode;
}

inline int ScreenshotEditorColorPickerHue(const ScreenshotEditorState& state)
{
    return state.colorPicker.hue;
}

inline int ScreenshotEditorColorPickerSaturation(const ScreenshotEditorState& state)
{
    return state.colorPicker.saturation;
}

inline int ScreenshotEditorColorPickerValue(const ScreenshotEditorState& state)
{
    return state.colorPicker.value;
}

// OWN-81: dual-write text edit caret / selection anchor (legacy write authority).
inline void ScreenshotEditorSyncTextEditCaret(
    ScreenshotEditorState& state,
    int caretIndex,
    int selectionAnchor)
{
    state.textCaretIndex = caretIndex;
    state.textSelectionAnchor = selectionAnchor;
}

inline int ScreenshotEditorTextCaretIndex(const ScreenshotEditorState& state)
{
    return state.textCaretIndex;
}

inline int ScreenshotEditorTextSelectionAnchor(const ScreenshotEditorState& state)
{
    return state.textSelectionAnchor;
}

inline bool ScreenshotEditorHasTextSelection(const ScreenshotEditorState& state)
{
    return state.textSelectionAnchor >= 0;
}

// S-E-CLOSE-5: text-edit index field deleted. Index param clears id when < 0 only.
// Prefer ScreenshotEditorSyncTextEditingById when id known.
inline void ScreenshotEditorSyncTextEditingIndex(
    ScreenshotEditorState& state,
    int editingTextIndex)
{
    if (editingTextIndex < 0) {
        state.editingTextId.clear();
    }
    // index >= 0 without id: leave id (Host should pass id via SyncTextEditingById).
}

// S-E-CLOSE-5: text-edit by id sole — id is text-edit authority; index not stored.
// Empty id clears text edit.
inline void ScreenshotEditorSyncTextEditingById(
    ScreenshotEditorState& state,
    int /*editingTextIndex*/,
    const std::wstring& editingTextId)
{
    state.editingTextId = editingTextId;
}

// S-E-CLOSE-5: index field deleted. Getter always -1 (layout via ResolveTextEditingIndex).
inline int ScreenshotEditorTextEditingIndex(const ScreenshotEditorState& /*state*/)
{
    return -1;
}

inline const std::wstring& ScreenshotEditorTextEditingId(const ScreenshotEditorState& state)
{
    return state.editingTextId;
}

// S-E-CLOSE-5: text-edit presence = non-empty editingTextId only.
inline bool ScreenshotEditorIsEditingText(const ScreenshotEditorState& state)
{
    return !state.editingTextId.empty();
}

void ScreenshotEditorSetMorePanelOpen(
    ScreenshotEditorState& state,
    bool morePanelOpen);

void ScreenshotEditorCloseMorePanel(ScreenshotEditorState& state);

void ScreenshotEditorSetOpenToolbarPanels(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand openToolGroup,
    ScreenshotToolbarCommand openTertiary);

void ScreenshotEditorCloseAllToolbarPanels(ScreenshotEditorState& state);

void ScreenshotEditorCloseMoreKeepTertiary(ScreenshotEditorState& state);

void ScreenshotEditorCloseTertiaryPanel(ScreenshotEditorState& state);

void ScreenshotEditorToggleMorePanel(ScreenshotEditorState& state);

void ScreenshotEditorToggleTertiaryPanel(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand target);

void ScreenshotEditorToggleToolGroupPanel(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand target);

enum class ScreenshotToolbarToolSessionAction {
    NotHandled,
    GroupOpened,
    ToolDeactivated,
    ToolActivated,
    WatermarkActivated,
};

ScreenshotToolbarToolSessionAction ScreenshotApplyToolbarToolSession(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command,
    bool undoAvailable,
    bool redoAvailable);

inline bool ScreenshotEditorIsMorePanelOpen(const ScreenshotEditorState& state)
{
    return state.toolbarPanels.morePanelOpen;
}

inline ScreenshotToolbarCommand ScreenshotEditorOpenToolGroup(
    const ScreenshotEditorState& state)
{
    return state.toolbarPanels.openToolGroup;
}

inline ScreenshotToolbarCommand ScreenshotEditorOpenTertiary(
    const ScreenshotEditorState& state)
{
    return state.toolbarPanels.openTertiary;
}

inline bool ScreenshotEditorIsOpenToolGroup(
    const ScreenshotEditorState& state,
    ScreenshotToolbarCommand command)
{
    return state.toolbarPanels.openToolGroup == command;
}

inline bool ScreenshotEditorIsOpenTertiary(
    const ScreenshotEditorState& state,
    ScreenshotToolbarCommand command)
{
    return state.toolbarPanels.openTertiary == command;
}

// S-E-4: pure sole color-target predicates (Host Is*ColorTargetActive methods deleted).
inline bool ScreenshotEditorIsTextOutlineColorTargetActive(const ScreenshotEditorState& state)
{
    return ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolText) &&
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextOutline);
}

inline bool ScreenshotEditorIsTextBackgroundColorTargetActive(const ScreenshotEditorState& state)
{
    return ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolText) &&
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextBackground);
}

inline bool ScreenshotEditorIsTextStyleColorTargetActive(const ScreenshotEditorState& state)
{
    return ScreenshotEditorIsTextOutlineColorTargetActive(state) ||
        ScreenshotEditorIsTextBackgroundColorTargetActive(state);
}

inline bool ScreenshotEditorIsHighLightStrokeColorTargetActive(const ScreenshotEditorState& state)
{
    return ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolHighLight);
}

inline bool ScreenshotEditorIsWatermarkColorTargetActive(const ScreenshotEditorState& state)
{
    return ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolWatermark);
}

inline bool ScreenshotEditorIsIndependentColorTargetActive(const ScreenshotEditorState& state)
{
    return ScreenshotEditorIsTextStyleColorTargetActive(state) ||
        ScreenshotEditorIsHighLightStrokeColorTargetActive(state) ||
        ScreenshotEditorIsWatermarkColorTargetActive(state);
}

// OWN-84: dual-write slider / color-picker drag interaction (legacy write authority).
inline void ScreenshotEditorSyncSliderDrag(
    ScreenshotEditorState& state,
    bool isDragging,
    ScreenshotToolbarCommand draggingCommand,
    int left, int top, int right, int bottom)
{
    state.isDraggingSlider = isDragging;
    state.draggingSlider = draggingCommand;
    state.sliderDragLeft = left;
    state.sliderDragTop = top;
    state.sliderDragRight = right;
    state.sliderDragBottom = bottom;
}

inline void ScreenshotEditorSyncColorPickerDrag(
    ScreenshotEditorState& state,
    bool isDragging,
    ScreenshotToolbarCommand draggingCommand,
    int left, int top, int right, int bottom)
{
    state.isDraggingColorPicker = isDragging;
    state.draggingColorPicker = draggingCommand;
    state.colorPickerDragLeft = left;
    state.colorPickerDragTop = top;
    state.colorPickerDragRight = right;
    state.colorPickerDragBottom = bottom;
}

inline bool ScreenshotEditorIsDraggingSlider(const ScreenshotEditorState& state)
{
    return state.isDraggingSlider;
}

inline ScreenshotToolbarCommand ScreenshotEditorDraggingSlider(
    const ScreenshotEditorState& state)
{
    return state.draggingSlider;
}

inline bool ScreenshotEditorIsDraggingColorPicker(const ScreenshotEditorState& state)
{
    return state.isDraggingColorPicker;
}

inline ScreenshotToolbarCommand ScreenshotEditorDraggingColorPicker(
    const ScreenshotEditorState& state)
{
    return state.draggingColorPicker;
}

// OWN-85: dual-write annotation draw / move / resize / rotate / hold interaction.
inline void ScreenshotEditorSyncAnnotationInteraction(
    ScreenshotEditorState& state,
    bool isDrawingAnnotation,
    bool isDrawingBrokenLinePath,
    bool isMovingAnnotation,
    bool isResizingAnnotation,
    bool isRotatingAnnotation,
    bool isHoldingRefresh)
{
    state.isDrawingAnnotation = isDrawingAnnotation;
    state.isDrawingBrokenLinePath = isDrawingBrokenLinePath;
    state.isMovingAnnotation = isMovingAnnotation;
    state.isResizingAnnotation = isResizingAnnotation;
    state.isRotatingAnnotation = isRotatingAnnotation;
    state.isHoldingRefresh = isHoldingRefresh;
}

// S-B-10: sole-authority writers (Host dual-write fields deleted).
inline void ScreenshotEditorSetDrawingAnnotation(ScreenshotEditorState& state, bool value)
{
    state.isDrawingAnnotation = value;
}
inline void ScreenshotEditorSetDrawingBrokenLinePath(ScreenshotEditorState& state, bool value)
{
    state.isDrawingBrokenLinePath = value;
}
inline void ScreenshotEditorSetMovingAnnotation(ScreenshotEditorState& state, bool value)
{
    state.isMovingAnnotation = value;
}
inline void ScreenshotEditorSetResizingAnnotation(ScreenshotEditorState& state, bool value)
{
    state.isResizingAnnotation = value;
}
inline void ScreenshotEditorSetRotatingAnnotation(ScreenshotEditorState& state, bool value)
{
    state.isRotatingAnnotation = value;
}
inline void ScreenshotEditorSetHoldingRefresh(ScreenshotEditorState& state, bool value)
{
    state.isHoldingRefresh = value;
}

inline bool ScreenshotEditorIsDrawingAnnotation(const ScreenshotEditorState& state)
{
    return state.isDrawingAnnotation;
}

inline bool ScreenshotEditorIsDrawingBrokenLinePath(const ScreenshotEditorState& state)
{
    return state.isDrawingBrokenLinePath;
}

inline bool ScreenshotEditorIsMovingAnnotation(const ScreenshotEditorState& state)
{
    return state.isMovingAnnotation;
}

inline bool ScreenshotEditorIsResizingAnnotation(const ScreenshotEditorState& state)
{
    return state.isResizingAnnotation;
}

inline bool ScreenshotEditorIsRotatingAnnotation(const ScreenshotEditorState& state)
{
    return state.isRotatingAnnotation;
}

inline bool ScreenshotEditorIsHoldingRefresh(const ScreenshotEditorState& state)
{
    return state.isHoldingRefresh;
}

inline bool ScreenshotEditorIsAnnotationGestureActive(const ScreenshotEditorState& state)
{
    return state.isDrawingAnnotation
        || state.isDrawingBrokenLinePath
        || state.isMovingAnnotation
        || state.isResizingAnnotation
        || state.isRotatingAnnotation
        || state.isHoldingRefresh;
}

// OWN-86: dual-write hover toolbar / side / tooltip / toast / active handle.
inline void ScreenshotEditorSyncHoverToolbar(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand hoveredToolbarButton,
    ScreenshotToolbarCommand hoveredSideButton,
    bool toolbarTooltipVisible)
{
    state.hoveredToolbarButton = hoveredToolbarButton;
    state.hoveredSideButton = hoveredSideButton;
    state.toolbarTooltipVisible = toolbarTooltipVisible;
}

inline void ScreenshotEditorSyncToast(
    ScreenshotEditorState& state,
    const std::wstring& toastText,
    unsigned int toastStartTick)
{
    state.toastText = toastText;
    state.toastStartTick = toastStartTick;
}

inline void ScreenshotEditorSyncActiveAnnotationHandle(
    ScreenshotEditorState& state,
    ScreenshotAnnotationHandle activeHandle,
    int activePointIndex)
{
    state.activeAnnotationHandle = activeHandle;
    state.activeAnnotationPointIndex = activePointIndex;
}

inline ScreenshotToolbarCommand ScreenshotEditorHoveredToolbarButton(
    const ScreenshotEditorState& state)
{
    return state.hoveredToolbarButton;
}

inline ScreenshotToolbarCommand ScreenshotEditorHoveredSideButton(
    const ScreenshotEditorState& state)
{
    return state.hoveredSideButton;
}

inline bool ScreenshotEditorIsToolbarTooltipVisible(const ScreenshotEditorState& state)
{
    return state.toolbarTooltipVisible;
}

inline const std::wstring& ScreenshotEditorToastText(const ScreenshotEditorState& state)
{
    return state.toastText;
}

inline unsigned int ScreenshotEditorToastStartTick(const ScreenshotEditorState& state)
{
    return state.toastStartTick;
}

inline bool ScreenshotEditorHasToast(const ScreenshotEditorState& state)
{
    return !state.toastText.empty();
}

inline ScreenshotAnnotationHandle ScreenshotEditorActiveAnnotationHandle(
    const ScreenshotEditorState& state)
{
    return state.activeAnnotationHandle;
}

inline int ScreenshotEditorActiveAnnotationPointIndex(const ScreenshotEditorState& state)
{
    return state.activeAnnotationPointIndex;
}

inline bool ScreenshotEditorHasActiveAnnotationHandle(const ScreenshotEditorState& state)
{
    return state.activeAnnotationHandle != ScreenshotAnnotationHandle::None;
}

// OWN-87: dual-write drawing points + move/resize originals + tool-settings dirty.
inline void ScreenshotEditorSyncAnnotationGeometryScratch(
    ScreenshotEditorState& state,
    int startX, int startY,
    int currentX, int currentY,
    int moveAnchorX, int moveAnchorY,
    int originalStartX, int originalStartY,
    int originalEndX, int originalEndY,
    int originalAuxX, int originalAuxY,
    int originalSourceStartX, int originalSourceStartY,
    int originalSourceEndX, int originalSourceEndY,
    int resizeFixedX, int resizeFixedY,
    int originalRoundedRadius,
    double originalAngle,
    double originalTextFontSize,
    double rotateStartMouseAngle)
{
    state.annotationStartX = startX;
    state.annotationStartY = startY;
    state.annotationCurrentX = currentX;
    state.annotationCurrentY = currentY;
    state.annotationMoveAnchorX = moveAnchorX;
    state.annotationMoveAnchorY = moveAnchorY;
    state.annotationOriginalStartX = originalStartX;
    state.annotationOriginalStartY = originalStartY;
    state.annotationOriginalEndX = originalEndX;
    state.annotationOriginalEndY = originalEndY;
    state.annotationOriginalAuxX = originalAuxX;
    state.annotationOriginalAuxY = originalAuxY;
    state.annotationOriginalSourceStartX = originalSourceStartX;
    state.annotationOriginalSourceStartY = originalSourceStartY;
    state.annotationOriginalSourceEndX = originalSourceEndX;
    state.annotationOriginalSourceEndY = originalSourceEndY;
    state.annotationResizeFixedX = resizeFixedX;
    state.annotationResizeFixedY = resizeFixedY;
    state.annotationOriginalRoundedRadius = originalRoundedRadius;
    state.annotationOriginalAngle = originalAngle;
    state.annotationOriginalTextFontSize = originalTextFontSize;
    state.annotationRotateStartMouseAngle = rotateStartMouseAngle;
}

// S-H residual: thin field patches — delete full Sync re-read no-ops / 2-field dual bodies.
// Prefer these over SyncAnnotationGeometryScratch when only a few fields change.
inline void ScreenshotEditorSetAnnotationStart(
    ScreenshotEditorState& state, int x, int y)
{
    state.annotationStartX = x;
    state.annotationStartY = y;
}

inline void ScreenshotEditorSetAnnotationCurrent(
    ScreenshotEditorState& state, int x, int y)
{
    state.annotationCurrentX = x;
    state.annotationCurrentY = y;
}

inline void ScreenshotEditorSetAnnotationStartCurrent(
    ScreenshotEditorState& state, int startX, int startY, int currentX, int currentY)
{
    state.annotationStartX = startX;
    state.annotationStartY = startY;
    state.annotationCurrentX = currentX;
    state.annotationCurrentY = currentY;
}

inline void ScreenshotEditorSetAnnotationMoveAnchor(
    ScreenshotEditorState& state, int x, int y)
{
    state.annotationMoveAnchorX = x;
    state.annotationMoveAnchorY = y;
}

inline void ScreenshotEditorSetAnnotationOriginalRect(
    ScreenshotEditorState& state, int startX, int startY, int endX, int endY)
{
    state.annotationOriginalStartX = startX;
    state.annotationOriginalStartY = startY;
    state.annotationOriginalEndX = endX;
    state.annotationOriginalEndY = endY;
}

inline void ScreenshotEditorSetAnnotationOriginalAux(
    ScreenshotEditorState& state, int x, int y)
{
    state.annotationOriginalAuxX = x;
    state.annotationOriginalAuxY = y;
}

inline void ScreenshotEditorSetAnnotationOriginalSource(
    ScreenshotEditorState& state, int startX, int startY, int endX, int endY)
{
    state.annotationOriginalSourceStartX = startX;
    state.annotationOriginalSourceStartY = startY;
    state.annotationOriginalSourceEndX = endX;
    state.annotationOriginalSourceEndY = endY;
}

inline void ScreenshotEditorSetAnnotationResizeFixed(
    ScreenshotEditorState& state, int x, int y)
{
    state.annotationResizeFixedX = x;
    state.annotationResizeFixedY = y;
}

inline void ScreenshotEditorSetAnnotationOriginalRoundedRadius(
    ScreenshotEditorState& state, int radius)
{
    state.annotationOriginalRoundedRadius = radius;
}

inline void ScreenshotEditorSetAnnotationOriginalAngle(
    ScreenshotEditorState& state, double angle)
{
    state.annotationOriginalAngle = angle;
}

inline void ScreenshotEditorSetAnnotationOriginalTextFontSize(
    ScreenshotEditorState& state, double fontSize)
{
    state.annotationOriginalTextFontSize = fontSize;
}

inline void ScreenshotEditorSetAnnotationRotateStartMouseAngle(
    ScreenshotEditorState& state, double angle)
{
    state.annotationRotateStartMouseAngle = angle;
}

inline void ScreenshotEditorSyncToolSettingsDirty(
    ScreenshotEditorState& state,
    bool dirty)
{
    state.toolSettingsDirty = dirty;
}

inline bool ScreenshotEditorIsToolSettingsDirty(const ScreenshotEditorState& state)
{
    return state.toolSettingsDirty;
}

inline int ScreenshotEditorAnnotationStartX(const ScreenshotEditorState& state) { return state.annotationStartX; }
inline int ScreenshotEditorAnnotationStartY(const ScreenshotEditorState& state) { return state.annotationStartY; }
inline int ScreenshotEditorAnnotationCurrentX(const ScreenshotEditorState& state) { return state.annotationCurrentX; }
inline int ScreenshotEditorAnnotationCurrentY(const ScreenshotEditorState& state) { return state.annotationCurrentY; }
inline int ScreenshotEditorAnnotationMoveAnchorX(const ScreenshotEditorState& state) { return state.annotationMoveAnchorX; }
inline int ScreenshotEditorAnnotationMoveAnchorY(const ScreenshotEditorState& state) { return state.annotationMoveAnchorY; }
inline int ScreenshotEditorAnnotationOriginalStartX(const ScreenshotEditorState& state) { return state.annotationOriginalStartX; }
inline int ScreenshotEditorAnnotationOriginalStartY(const ScreenshotEditorState& state) { return state.annotationOriginalStartY; }
inline int ScreenshotEditorAnnotationOriginalEndX(const ScreenshotEditorState& state) { return state.annotationOriginalEndX; }
inline int ScreenshotEditorAnnotationOriginalEndY(const ScreenshotEditorState& state) { return state.annotationOriginalEndY; }
inline int ScreenshotEditorAnnotationOriginalAuxX(const ScreenshotEditorState& state) { return state.annotationOriginalAuxX; }
inline int ScreenshotEditorAnnotationOriginalAuxY(const ScreenshotEditorState& state) { return state.annotationOriginalAuxY; }
inline int ScreenshotEditorAnnotationOriginalSourceStartX(const ScreenshotEditorState& state) { return state.annotationOriginalSourceStartX; }
inline int ScreenshotEditorAnnotationOriginalSourceStartY(const ScreenshotEditorState& state) { return state.annotationOriginalSourceStartY; }
inline int ScreenshotEditorAnnotationOriginalSourceEndX(const ScreenshotEditorState& state) { return state.annotationOriginalSourceEndX; }
inline int ScreenshotEditorAnnotationOriginalSourceEndY(const ScreenshotEditorState& state) { return state.annotationOriginalSourceEndY; }
inline int ScreenshotEditorAnnotationResizeFixedX(const ScreenshotEditorState& state) { return state.annotationResizeFixedX; }
inline int ScreenshotEditorAnnotationResizeFixedY(const ScreenshotEditorState& state) { return state.annotationResizeFixedY; }
inline int ScreenshotEditorAnnotationOriginalRoundedRadius(const ScreenshotEditorState& state) { return state.annotationOriginalRoundedRadius; }
inline double ScreenshotEditorAnnotationOriginalAngle(const ScreenshotEditorState& state) { return state.annotationOriginalAngle; }
inline double ScreenshotEditorAnnotationOriginalTextFontSize(const ScreenshotEditorState& state) { return state.annotationOriginalTextFontSize; }
inline double ScreenshotEditorAnnotationRotateStartMouseAngle(const ScreenshotEditorState& state) { return state.annotationRotateStartMouseAngle; }
