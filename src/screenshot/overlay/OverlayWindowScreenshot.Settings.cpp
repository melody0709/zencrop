#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "core/Settings.h"
#include "core/WideStringUtils.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotActiveColor.h"
#include "screenshot/editor/ScreenshotCommandKind.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"
#include "screenshot/editor/ScreenshotToolSettingsMap.h"
#include "screenshot/editor/ScreenshotToolbarCommandGroups.h"

#include <algorithm>
#include <windows.h>

// S-H-CLOSE-5: real translation unit (was OverlayWindowScreenshot.Settings.inl).
// Class-method residual → Host method TU. No product semantic change.

// Local mosaic strength max (was umbrella static constexpr; same value as ScreenshotImageUtils.cpp).
static constexpr int kScreenshotMosaicStrengthMaxLocal = 28;
// S-C-5: tool-settings-id pure mappers sole in ScreenshotToolSettingsMap.h.
// Host static *Local duals deleted (SettingId / FromSettingId / NormalizeToolGroup).

// S-B-31: SyncScreenshotEditorState deleted. Call sites project Host-owned
// annotation vector size + history into pure state and re-clamp selection inline.

// S-B-17: SyncScreenshotLastHoverMagnifierCacheMirror deleted; last hover-magnifier cache sole on editorState.

// S-B-8: pending text create id sole on editorState; Sync deleted.

// S-B-13: SyncScreenshotToolbarRectMirror deleted; toolbarRect sole on editorState.

// S-B-18: SyncScreenshotPathPointCountsMirror deleted; counts sole on editorState.

// S-B-14: SyncScreenshotSmartHoverFlagsMirror deleted; smart-hover flags sole on editorState.

// S-B-20: SyncScreenshotCropDragSessionMirror deleted; crop-drag session sole on editorState.

// S-B-16: SyncScreenshotSmartDetectionRequestMirror deleted; smart-detection request sole on editorState.

// S-B-15: SyncScreenshotHoveredToolbarChromeMirror deleted; hovered toolbar chrome sole on editorState.

// S-B-28: SyncScreenshotCropGeometryMirror deleted; cropRect sole on editorState (OWN-92 complete).

// S-B-21: SyncScreenshotScreenHoverGeometryMirror deleted; screen-hover geometry sole on editorState.

// S-B-8: text caret/selection sole on editorState; Sync deleted.

// S-B-8: text editing index sole on editorState; Sync deleted.

// S-B-7: SyncScreenshotMorePanelOpenMirror deleted; morePanelOpen sole on editorState.

// S-B-7: SyncScreenshotOpenToolbarPanelsMirror deleted; open panels sole on editorState.

// S-B-9: SyncScreenshotSliderDragMirror deleted; slider drag sole on editorState.

// S-B-9: SyncScreenshotColorPickerDragMirror deleted; color-picker drag sole on editorState.

// S-B-10: SyncScreenshotAnnotationInteractionMirror deleted; interaction flags sole on editorState.

// S-B-11: SyncScreenshotHoverToolbarMirror deleted; sole on editorState.

// S-B-11: SyncScreenshotToastMirror deleted; sole on editorState.

// S-B-11: SyncScreenshotActiveAnnotationHandleMirror deleted; sole on editorState.

// S-B-19: SyncScreenshotAnnotationGeometryScratchMirror deleted; geometry scratch sole on editorState.

// S-B-12: SyncScreenshotToolSettingsDirtyMirror deleted; toolSettingsDirty sole on editorState.

// S-B-29: SyncScreenshotHistoryFlags deleted; call sites project Host history into pure state.

// S-E-3: SetActiveScreenshotTool deleted; pure
// ScreenshotEditorSelectToolWithHistory sole.
// S-E-2: SetSelectedScreenshotAnnotationIndex deleted; pure
// ScreenshotEditorSetAnnotationCountAndSelect sole.

void OverlayWindow::LoadScreenshotToolSettings() {
    ScreenshotSettings settings = LoadScreenshotSettings();

    ScreenshotEditorSelectToolWithHistory(m_editorState, ScreenshotToolbarCommand::Confirm, m_annotationHistory.canUndo(), m_annotationHistory.canRedo());
    m_editorState.toolGroupMemory.geometryTool = ScreenshotNormalizeToolGroup(
        ScreenshotToolFromSettingId(settings.annotationGeometryTool, ScreenshotToolbarCommand::ToolGeometry),
        ScreenshotToolbarCommand::ToolGeometry,
        ScreenshotToolbarCommand::ToolGeometry, ScreenshotToolbarCommand::ToolHighLight);
    m_editorState.toolGroupMemory.markerTool = ScreenshotNormalizeToolGroup(
        ScreenshotToolFromSettingId(settings.annotationMarkerTool, ScreenshotToolbarCommand::ToolMarker),
        ScreenshotToolbarCommand::ToolMarker,
        ScreenshotToolbarCommand::ToolPencil, ScreenshotToolbarCommand::ToolMarker);
    m_editorState.toolGroupMemory.arrowTool = ScreenshotNormalizeToolGroup(
        ScreenshotToolFromSettingId(settings.annotationArrowTool, ScreenshotToolbarCommand::ToolArrow),
        ScreenshotToolbarCommand::ToolArrow,
        ScreenshotToolbarCommand::ToolArrow, ScreenshotToolbarCommand::ToolBrokenLine,
        ScreenshotToolbarCommand::ToolMagnifier);
    m_editorState.toolGroupMemory.textTool = ScreenshotNormalizeToolGroup(
        ScreenshotToolFromSettingId(settings.annotationTextTool, ScreenshotToolbarCommand::ToolText),
        ScreenshotToolbarCommand::ToolText,
        ScreenshotToolbarCommand::ToolText, ScreenshotToolbarCommand::ToolWatermark);
    m_editorState.toolGroupMemory.mosaicTool = ScreenshotNormalizeToolGroup(
        ScreenshotToolFromSettingId(settings.annotationMosaicTool, ScreenshotToolbarCommand::ToolMosaic),
        ScreenshotToolbarCommand::ToolMosaic,
        ScreenshotToolbarCommand::ToolMosaic, ScreenshotToolbarCommand::ToolMosaic);
    if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolAutoMosaic)) {
        ScreenshotEditorSelectToolWithHistory(m_editorState, ScreenshotToolbarCommand::ToolMosaic, m_annotationHistory.canUndo(), m_annotationHistory.canRedo());
    }
    const ScreenshotToolbarCommand activeTool = ScreenshotEditorActiveTool(m_editorState);
    if (activeTool == ScreenshotToolbarCommand::ToolGeometry ||
        activeTool == ScreenshotToolbarCommand::ToolHighLight) {
        m_editorState.toolGroupMemory.geometryTool = activeTool;
    } else if (activeTool == ScreenshotToolbarCommand::ToolPencil ||
        activeTool == ScreenshotToolbarCommand::ToolMarker) {
        m_editorState.toolGroupMemory.markerTool = activeTool;
    } else if (activeTool == ScreenshotToolbarCommand::ToolArrow ||
        activeTool == ScreenshotToolbarCommand::ToolBrokenLine ||
        activeTool == ScreenshotToolbarCommand::ToolMagnifier) {
        m_editorState.toolGroupMemory.arrowTool = activeTool;
    } else if (activeTool == ScreenshotToolbarCommand::ToolText ||
        activeTool == ScreenshotToolbarCommand::ToolWatermark) {
        m_editorState.toolGroupMemory.textTool = activeTool;
    } else if (activeTool == ScreenshotToolbarCommand::ToolMosaic) {
        m_editorState.toolGroupMemory.mosaicTool = activeTool;
    }

    m_editorState.colorIndices.colorIndex = settings.annotationColorIndex;
    m_editorState.colorIndices.geometryColorIndex = settings.annotationGeometryColorIndex;
    m_editorState.colorIndices.markerColorIndex = settings.annotationMarkerColorIndex;
    m_editorState.toolStyle.usesCustomColor = settings.annotationUsesCustomColor;
    m_editorState.toolStyle.customColor = static_cast<unsigned int>(settings.annotationCustomColor);
    m_editorState.toolStyle.colorAlpha = settings.annotationColorAlpha;
    m_editorState.colorPicker.mode = (std::min)((std::max)(settings.annotationColorPickerMode, 0), 2);
    m_editorState.toolModes.lineStyle = settings.annotationLineStyle;
    m_editorState.toolStyle.geometryPenWidth = settings.annotationGeometryPenWidth;
    m_editorState.effectStyle.geometryRoundedRadius = settings.annotationGeometryRoundedRadius;
    m_editorState.toolStyle.pencilPenWidth = settings.annotationPencilPenWidth;
    m_editorState.toolStyle.markerPenWidth = settings.annotationMarkerPenWidth;
    m_editorState.toolStyle.arrowPenWidth = settings.annotationArrowPenWidth;
    m_editorState.toolModes.arrowShape = settings.annotationArrowShape;
    m_editorState.toolModes.brokenLineMode = (std::min)((std::max)(settings.annotationBrokenLineMode, 0), 1);
    m_editorState.toolModes.brokenLineArrow = settings.annotationBrokenLineArrow;
    m_editorState.toolModes.brokenLineStartArrowType = settings.annotationBrokenLineStartArrowType;
    m_editorState.toolModes.brokenLineEndArrowType = settings.annotationBrokenLineEndArrowType;
    m_editorState.toolStyle.magnifierPenWidth = settings.annotationMagnifierPenWidth;
    m_editorState.magnifierStyle.roundedRadius = settings.annotationMagnifierRoundedRadius;
    m_editorState.magnifierStyle.ellipse = settings.annotationMagnifierEllipse;
    m_editorState.magnifierStyle.eraseMark = settings.annotationMagnifierEraseMark;
    m_editorState.magnifierStyle.antiAlias = settings.annotationMagnifierAntiAlias;
    m_editorState.magnifierStyle.shadow = settings.annotationMagnifierShadow;
    m_editorState.magnifierStyle.linkType = settings.annotationMagnifierLinkType;
    m_editorState.magnifierStyle.magnification = settings.annotationMagnifierMagnification;
    m_editorState.toolStyle.mosaicPenWidth = settings.annotationMosaicPenWidth;
    m_editorState.toolStyle.eraserPenWidth = settings.annotationEraserPenWidth;
    m_editorState.toolStyle.serialPenWidth = settings.annotationSerialPenWidth;
    m_editorState.effectStyle.mosaicStrength = settings.annotationMosaicStrength;
    if (m_editorState.effectStyle.mosaicStrength > kScreenshotMosaicStrengthMaxLocal) {
        m_editorState.effectStyle.mosaicStrength = MulDiv(m_editorState.effectStyle.mosaicStrength, kScreenshotMosaicStrengthMaxLocal, 100);
    }
    m_editorState.effectStyle.mosaicStrength = (std::min)((std::max)(m_editorState.effectStyle.mosaicStrength, 0), kScreenshotMosaicStrengthMaxLocal);
    m_editorState.effectStyle.markerBlendMode = settings.annotationMarkerBlendMode;
    m_editorState.effectStyle.mosaicMode = (std::min)((std::max)(settings.annotationMosaicMode, 0), 1);
    m_editorState.effectStyle.serialType = settings.annotationSerialType;
    m_editorState.highLightStyle.stroke = settings.annotationHighLightStroke;
    m_editorState.highLightStyle.opacity = settings.annotationHighLightOpacity;
    m_editorState.highLightStyle.strokeColor = static_cast<unsigned int>(settings.annotationHighLightStrokeColor);
    m_editorState.effectStyle.autoMosaicSync = settings.annotationAutoMosaicSync;
    m_editorState.textStyle.outline = settings.annotationTextOutline;
    m_editorState.textStyle.outlineSize = settings.annotationTextOutlineSize;
    m_editorState.textStyle.outlineColor = static_cast<unsigned int>(settings.annotationTextOutlineColor);
    m_editorState.textStyle.background = settings.annotationTextBackground;
    m_editorState.textStyle.backgroundColor = static_cast<unsigned int>(settings.annotationTextBackgroundColor);
    m_editorState.textStyle.backgroundOpacity = settings.annotationTextBackgroundOpacity;
    m_editorState.textStyle.backgroundRounded = settings.annotationTextBackgroundRounded;
    m_editorState.textStyle.backgroundPadding = settings.annotationTextBackgroundPadding;
    m_editorState.textStyle.bold = settings.annotationTextBold;
    m_editorState.textStyle.italics = settings.annotationTextItalics;
    m_editorState.textStyle.fontFamily = settings.annotationTextFontFamily.empty() ? L"Microsoft YaHei" : settings.annotationTextFontFamily;
    m_editorState.textStyle.fontSize = (std::max)(settings.annotationTextFontSize, 8);
    m_editorState.textStyle.fontSizeF = (double)m_editorState.textStyle.fontSize;
    m_editorState.watermarkStyle.text = settings.annotationWatermarkText;
    m_editorState.watermarkStyle.color = static_cast<unsigned int>(settings.annotationWatermarkColor);
    m_editorState.watermarkStyle.bold = settings.annotationWatermarkBold;
    m_editorState.watermarkStyle.italics = settings.annotationWatermarkItalics;
    m_editorState.watermarkStyle.opacity = (std::min)((std::max)(settings.annotationWatermarkOpacity, 10), 100);
    m_editorState.watermarkStyle.fontSize = (std::min)((std::max)(settings.annotationWatermarkFontSize, 8), 80);
    m_editorState.watermarkStyle.gap = (std::min)((std::max)(settings.annotationWatermarkGap, 0), 100);
    m_editorState.watermarkStyle.angle = (std::min)((std::max)(settings.annotationWatermarkAngle, -90), 90);
    m_editorState.watermarkStyle.fontFamily = settings.annotationWatermarkFontFamily.empty() ? L"Microsoft YaHei" : settings.annotationWatermarkFontFamily;
    m_editorState.watermarkStyle.position = (std::min)((std::max)(settings.annotationWatermarkPosition, 0), 7);
    m_editorState.postProcessStyle.roundedCorners = false;
    m_editorState.postProcessStyle.roundedCornerRadius = (std::min)((std::max)(settings.roundedCornerRadius, 0), 0x3c);
    m_editorState.cropPrefs.keepAspectRatio = false;
    m_editorState.cropPrefs.aspectRatio = 0.0;
    m_editorState.postProcessStyle.enableEveryScreenshot = settings.postProcessEnabledEveryScreenshot;
    m_editorState.postProcessStyle.mode = (std::min)((std::max)(settings.postProcessMode, 1), 2);
    m_editorState.postProcessStyle.shadowSize = (std::min)((std::max)(settings.postProcessShadowSize, 0), 100);
    m_editorState.postProcessStyle.shadowColor = static_cast<unsigned int>(settings.postProcessShadowColor);
    m_editorState.postProcessStyle.borderSize = (std::min)((std::max)(settings.postProcessBorderSize, 0), 100);
    m_editorState.postProcessStyle.borderColor = static_cast<unsigned int>(settings.postProcessBorderColor);
    m_editorState.postProcessStyle.enabled = m_editorState.postProcessStyle.enableEveryScreenshot;
    m_editorState.functionAreaPrefs.alwaysShow = settings.functionAreaAlwaysShow;
    m_editorState.functionAreaPrefs.morePanel = settings.functionAreaMorePanel;
    m_editorState.functionAreaPrefs.alwaysHide = settings.functionAreaAlwaysHide;

    COLORREF color = m_editorState.toolStyle.usesCustomColor
        ? static_cast<COLORREF>(m_editorState.toolStyle.customColor)
        : ScreenshotPresetColorLocal(ScreenshotEditorActiveColorIndex(m_editorState));
    ScreenshotRgbToHsvLocal(color, m_editorState.colorPicker.hue, m_editorState.colorPicker.saturation, m_editorState.colorPicker.value);
    m_editorState.hoverMagnifierPrefs.enabled = settings.hoverMagnifierEnabled;
    m_editorState.hoverMagnifierPrefs.power = (std::min)((std::max)(settings.hoverMagnifierPower, 1), 100);
    m_editorState.hoverMagnifierPrefs.colorFormat = (std::min)((std::max)(settings.hoverMagnifierColorFormat, 0), 5);
    m_editorState.hoverMagnifierPrefs.showCoord = settings.hoverMagnifierShowCoord;
    m_hoverMagnifier.SetPower(m_editorState.hoverMagnifierPrefs.power);
    m_hoverMagnifier.SetFormatIndex(m_editorState.hoverMagnifierPrefs.colorFormat);
    m_hoverMagnifier.SetShowCoord(m_editorState.hoverMagnifierPrefs.showCoord);
    // S-B-12: toolSettingsDirty sole on m_editorState.
    ScreenshotEditorSyncToolSettingsDirty(m_editorState, false);
    // S-B-31: project Host-owned annotation count + history into pure state.
    // S-E-48: re-project select by id sole (index short-life layout only).
    ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, ScreenshotEditorSelectedAnnotationId(m_editorState));
    ScreenshotEditorSetHistoryAvailability(
        m_editorState,
        m_annotationHistory.canUndo(),
        m_annotationHistory.canRedo());
    // S-E-EXIT E3: Document is sole store; no SyncFromLegacy Host→Document recovery.
}

void OverlayWindow::SaveScreenshotToolSettings() {
    ScreenshotSettings settings = LoadScreenshotSettings();

    settings.annotationActiveTool = 0;
    settings.annotationGeometryTool = ScreenshotToolSettingId(m_editorState.toolGroupMemory.geometryTool);
    settings.annotationMarkerTool = ScreenshotToolSettingId(m_editorState.toolGroupMemory.markerTool);
    settings.annotationArrowTool = ScreenshotToolSettingId(m_editorState.toolGroupMemory.arrowTool);
    settings.annotationTextTool = ScreenshotToolSettingId(m_editorState.toolGroupMemory.textTool);
    settings.annotationMosaicTool = ScreenshotToolSettingId(m_editorState.toolGroupMemory.mosaicTool);
    settings.annotationColorIndex = m_editorState.colorIndices.colorIndex;
    settings.annotationGeometryColorIndex = m_editorState.colorIndices.geometryColorIndex;
    settings.annotationMarkerColorIndex = m_editorState.colorIndices.markerColorIndex;
    settings.annotationUsesCustomColor = m_editorState.toolStyle.usesCustomColor;
    settings.annotationCustomColor = static_cast<COLORREF>(m_editorState.toolStyle.customColor);
    settings.annotationColorAlpha = m_editorState.toolStyle.colorAlpha;
    settings.annotationColorPickerMode = (std::min)((std::max)(ScreenshotEditorColorPickerMode(m_editorState), 0), 2);
    settings.annotationLineStyle = m_editorState.toolModes.lineStyle;
    settings.annotationGeometryPenWidth = m_editorState.toolStyle.geometryPenWidth;
    settings.annotationGeometryRoundedRadius = m_editorState.effectStyle.geometryRoundedRadius;
    settings.annotationPencilPenWidth = m_editorState.toolStyle.pencilPenWidth;
    settings.annotationMarkerPenWidth = m_editorState.toolStyle.markerPenWidth;
    settings.annotationArrowPenWidth = m_editorState.toolStyle.arrowPenWidth;
    settings.annotationArrowShape = m_editorState.toolModes.arrowShape;
    settings.annotationBrokenLineMode = (std::min)((std::max)(m_editorState.toolModes.brokenLineMode, 0), 1);
    settings.annotationBrokenLineArrow = m_editorState.toolModes.brokenLineArrow;
    settings.annotationBrokenLineStartArrowType = m_editorState.toolModes.brokenLineStartArrowType;
    settings.annotationBrokenLineEndArrowType = m_editorState.toolModes.brokenLineEndArrowType;
    settings.annotationMagnifierPenWidth = m_editorState.toolStyle.magnifierPenWidth;
    settings.annotationMagnifierRoundedRadius = m_editorState.magnifierStyle.roundedRadius;
    settings.annotationMagnifierEllipse = m_editorState.magnifierStyle.ellipse;
    settings.annotationMagnifierEraseMark = m_editorState.magnifierStyle.eraseMark;
    settings.annotationMagnifierAntiAlias = m_editorState.magnifierStyle.antiAlias;
    settings.annotationMagnifierShadow = m_editorState.magnifierStyle.shadow;
    settings.annotationMagnifierLinkType = m_editorState.magnifierStyle.linkType;
    settings.annotationMagnifierMagnification = m_editorState.magnifierStyle.magnification;
    settings.annotationMosaicPenWidth = m_editorState.toolStyle.mosaicPenWidth;
    settings.annotationEraserPenWidth = m_editorState.toolStyle.eraserPenWidth;
    settings.annotationSerialPenWidth = m_editorState.toolStyle.serialPenWidth;
    settings.annotationMosaicStrength = m_editorState.effectStyle.mosaicStrength;
    settings.annotationMarkerBlendMode = m_editorState.effectStyle.markerBlendMode;
    settings.annotationMosaicMode = m_editorState.effectStyle.mosaicMode;
    settings.annotationSerialType = m_editorState.effectStyle.serialType;
    settings.annotationHighLightStroke = m_editorState.highLightStyle.stroke;
    settings.annotationHighLightOpacity = m_editorState.highLightStyle.opacity;
    settings.annotationHighLightStrokeColor = static_cast<COLORREF>(m_editorState.highLightStyle.strokeColor);
    settings.annotationAutoMosaicSync = m_editorState.effectStyle.autoMosaicSync;
    settings.annotationTextOutline = m_editorState.textStyle.outline;
    settings.annotationTextOutlineSize = m_editorState.textStyle.outlineSize;
    settings.annotationTextOutlineColor = static_cast<COLORREF>(m_editorState.textStyle.outlineColor);
    settings.annotationTextBackground = m_editorState.textStyle.background;
    settings.annotationTextBackgroundColor = static_cast<COLORREF>(m_editorState.textStyle.backgroundColor);
    settings.annotationTextBackgroundOpacity = m_editorState.textStyle.backgroundOpacity;
    settings.annotationTextBackgroundRounded = m_editorState.textStyle.backgroundRounded;
    settings.annotationTextBackgroundPadding = m_editorState.textStyle.backgroundPadding;
    settings.annotationTextBold = m_editorState.textStyle.bold;
    settings.annotationTextItalics = m_editorState.textStyle.italics;
    settings.annotationTextFontFamily = m_editorState.textStyle.fontFamily.empty() ? L"Microsoft YaHei" : m_editorState.textStyle.fontFamily;
    settings.annotationTextFontSize = (std::max)(m_editorState.textStyle.fontSize, 8);
    settings.annotationWatermarkText = m_editorState.watermarkStyle.text;
    settings.annotationWatermarkColor = static_cast<COLORREF>(m_editorState.watermarkStyle.color);
    settings.annotationWatermarkBold = m_editorState.watermarkStyle.bold;
    settings.annotationWatermarkItalics = m_editorState.watermarkStyle.italics;
    settings.annotationWatermarkOpacity = (std::min)((std::max)(m_editorState.watermarkStyle.opacity, 10), 100);
    settings.annotationWatermarkFontSize = (std::min)((std::max)(m_editorState.watermarkStyle.fontSize, 8), 80);
    settings.annotationWatermarkGap = (std::min)((std::max)(m_editorState.watermarkStyle.gap, 0), 100);
    settings.annotationWatermarkAngle = (std::min)((std::max)(m_editorState.watermarkStyle.angle, -90), 90);
    settings.annotationWatermarkFontFamily = m_editorState.watermarkStyle.fontFamily.empty() ? L"Microsoft YaHei" : m_editorState.watermarkStyle.fontFamily;
    settings.annotationWatermarkPosition = (std::min)((std::max)(m_editorState.watermarkStyle.position, 0), 7);
    settings.postProcessEnabledEveryScreenshot = m_editorState.postProcessStyle.enableEveryScreenshot;
    settings.postProcessMode = (std::min)((std::max)(m_editorState.postProcessStyle.mode, 1), 2);
    settings.roundedCornerRadius = (std::min)((std::max)(m_editorState.postProcessStyle.roundedCornerRadius, 0), 0x3c);
    settings.postProcessShadowSize = (std::min)((std::max)(m_editorState.postProcessStyle.shadowSize, 0), 100);
    settings.postProcessShadowColor = static_cast<COLORREF>(m_editorState.postProcessStyle.shadowColor);
    settings.postProcessBorderSize = (std::min)((std::max)(m_editorState.postProcessStyle.borderSize, 0), 100);
    settings.postProcessBorderColor = static_cast<COLORREF>(m_editorState.postProcessStyle.borderColor);
    settings.functionAreaAlwaysShow = m_editorState.functionAreaPrefs.alwaysShow;
    settings.functionAreaMorePanel = m_editorState.functionAreaPrefs.morePanel;
    settings.functionAreaAlwaysHide = m_editorState.functionAreaPrefs.alwaysHide;
    settings.hoverMagnifierEnabled = m_editorState.hoverMagnifierPrefs.enabled;
    settings.hoverMagnifierPower = m_editorState.hoverMagnifierPrefs.power;
    settings.hoverMagnifierColorFormat = m_editorState.hoverMagnifierPrefs.colorFormat;
    settings.hoverMagnifierShowCoord = m_editorState.hoverMagnifierPrefs.showCoord;

    SaveScreenshotSettings(settings);
    // S-B-12: toolSettingsDirty sole on m_editorState.
    ScreenshotEditorSyncToolSettingsDirty(m_editorState, false);
}

// S-E-4: MarkScreenshotToolSettingsDirty deleted; pure
// ScreenshotEditorSyncToolSettingsDirty(state, true) sole.
// S-E-4: Host Is*ColorTargetActive methods deleted; pure
// ScreenshotEditorIs*ColorTargetActive sole.

void OverlayWindow::FlushScreenshotToolSettingsIfDirty() {
    // OWN-87: pure dirty dual-write is read authority on flush gate.
    if (!ScreenshotEditorIsToolSettingsDirty(m_editorState)) return;
    SaveScreenshotToolSettings();
}

bool OverlayWindow::HandleScreenshotMouseWheel(POINT pt, int delta) {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState) || m_state != OverlayState::Adjust || delta == 0) return false;

    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;

    auto clampStep = [&](int& value, int minValue, int maxValue, int step) -> bool {
        int oldValue = value;
        int nextValue = value + notches * step;
        value = (std::min)((std::max)(nextValue, minValue), maxValue);
        return value != oldValue;
    };

    auto applyStyleChange = [&]() {
        ApplyActiveScreenshotStyleToSelection();
        ScreenshotEditorSyncToolSettingsDirty(m_editorState, true);
        UpdateCursorForPoint(pt);
        UpdateOverlay();
    };

    auto adjustActiveToolPrimaryValue = [&]() -> bool {
        bool changed = false;
        switch (ScreenshotEditorActiveTool(m_editorState)) {
        case ScreenshotToolbarCommand::ToolGeometry:
            changed = clampStep(m_editorState.toolStyle.geometryPenWidth, 1, 32, 1);
            break;
        case ScreenshotToolbarCommand::ToolPencil:
        case ScreenshotToolbarCommand::ToolBrokenLine:
            changed = clampStep(m_editorState.toolStyle.pencilPenWidth, 1, 32, 1);
            break;
        case ScreenshotToolbarCommand::ToolMarker:
            changed = clampStep(m_editorState.toolStyle.markerPenWidth, 1, 32, 1);
            break;
        case ScreenshotToolbarCommand::ToolHighLight:
            changed = clampStep(m_editorState.highLightStyle.opacity, 0, 100, 5);
            break;
        case ScreenshotToolbarCommand::ToolArrow:
            changed = clampStep(m_editorState.toolStyle.arrowPenWidth, 1, 32, 1);
            break;
        case ScreenshotToolbarCommand::ToolMagnifier:
            changed = clampStep(m_editorState.magnifierStyle.magnification, 100, 400, 10);
            break;
        case ScreenshotToolbarCommand::ToolMosaic:
        case ScreenshotToolbarCommand::ToolAutoMosaic:
            changed = clampStep(m_editorState.toolStyle.mosaicPenWidth, 1, 32, 1);
            break;
        case ScreenshotToolbarCommand::ToolEraser:
            changed = clampStep(m_editorState.toolStyle.eraserPenWidth, 1, 32, 1);
            break;
        case ScreenshotToolbarCommand::ToolSerial:
            changed = clampStep(m_editorState.toolStyle.serialPenWidth, 1, 32, 1);
            break;
        case ScreenshotToolbarCommand::ToolText:
        {
            const auto& pureTextWheel = ScreenshotEditorTextStyleOf(m_editorState);
            const double oldValue = pureTextWheel.fontSizeF > 0.0
                ? pureTextWheel.fontSizeF
                : (double)pureTextWheel.fontSize;
            m_editorState.textStyle.fontSizeF = (std::max)(oldValue + (double)notches, 8.0);
            m_editorState.textStyle.fontSize = (std::max)((int)std::lround(m_editorState.textStyle.fontSizeF), 8);
            changed = std::abs(m_editorState.textStyle.fontSizeF - oldValue) > 0.0001;
            break;
        }
        case ScreenshotToolbarCommand::ToolWatermark:
            changed = clampStep(m_editorState.watermarkStyle.fontSize, 8, 80, 1);
            break;
        default:
            break;
        }
        if (changed) {
            // Dual-write pen widths + specialized styles touched by wheel primary adjust.
            if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolHighLight)) {
            } else if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolMagnifier)) {
            } else if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolText)) {
            } else if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolWatermark)) {
            }
            applyStyleChange();
        }
        return changed;
    };

    auto adjustConfigValue = [&](ScreenshotToolbarCommand command) -> bool {
        bool changed = false;
        switch (command) {
        case ScreenshotToolbarCommand::ConfigPenWidth:
        case ScreenshotToolbarCommand::ConfigPenWidthSet:
            changed = adjustActiveToolPrimaryValue();
            break;
        case ScreenshotToolbarCommand::ScreenshotSideShadowBorder:
        case ScreenshotToolbarCommand::ScreenshotPostProcessStrengthSet:
            if (m_editorState.postProcessStyle.mode == 2) {
                changed = clampStep(m_editorState.postProcessStyle.borderSize, 0, 100, 1);
            } else {
                changed = clampStep(m_editorState.postProcessStyle.shadowSize, 0, 100, 1);
            }
            break;
        case ScreenshotToolbarCommand::ScreenshotSideRounded:
        case ScreenshotToolbarCommand::ScreenshotRoundedRadiusSet:
            changed = clampStep(m_editorState.postProcessStyle.roundedCornerRadius, 0, 0x3c, 1);
            if (changed) {
                m_editorState.postProcessStyle.roundedCorners = true;
            }
            break;
        case ScreenshotToolbarCommand::ConfigRoundedRadius:
        case ScreenshotToolbarCommand::ConfigRoundedRadiusSet:
            changed = clampStep(m_editorState.effectStyle.geometryRoundedRadius, 0, 0x32, 1);
            break;
        case ScreenshotToolbarCommand::ConfigMosaicStrength:
        case ScreenshotToolbarCommand::ConfigMosaicStrengthSet:
            changed = clampStep(m_editorState.effectStyle.mosaicStrength, 0, kScreenshotMosaicStrengthMaxLocal, 1);
            break;
        case ScreenshotToolbarCommand::ConfigMagnifierMagnification:
        case ScreenshotToolbarCommand::ConfigMagnifierMagnificationSet:
            changed = clampStep(m_editorState.magnifierStyle.magnification, 100, 400, 10);
            break;
        case ScreenshotToolbarCommand::ConfigTextOutline:
        case ScreenshotToolbarCommand::ConfigTextOutlineSizeSet:
            changed = clampStep(m_editorState.textStyle.outlineSize, 1, 0x32, 1);
            break;
        case ScreenshotToolbarCommand::ConfigTextBackground:
        case ScreenshotToolbarCommand::ConfigTextBackgroundOpacitySet:
            changed = clampStep(m_editorState.textStyle.backgroundOpacity, 0, 100, 5);
            break;
        case ScreenshotToolbarCommand::ConfigTextBackgroundRoundedSet:
            changed = clampStep(m_editorState.textStyle.backgroundRounded, 0, 0x1e, 1);
            break;
        case ScreenshotToolbarCommand::ConfigTextBackgroundPaddingSet:
            changed = clampStep(m_editorState.textStyle.backgroundPadding, 0, 0x32, 1);
            break;
        case ScreenshotToolbarCommand::ConfigHighLightOpacitySet:
            changed = clampStep(m_editorState.highLightStyle.opacity, 0, 100, 5);
            break;
        case ScreenshotToolbarCommand::ConfigWatermarkOpacitySet:
            changed = clampStep(m_editorState.watermarkStyle.opacity, 10, 100, 5);
            break;
        case ScreenshotToolbarCommand::ConfigWatermarkStyle:
        case ScreenshotToolbarCommand::ConfigWatermarkFontSizeSet:
            changed = clampStep(m_editorState.watermarkStyle.fontSize, 8, 80, 1);
            break;
        case ScreenshotToolbarCommand::ConfigWatermarkGapSet:
            changed = clampStep(m_editorState.watermarkStyle.gap, 0, 100, 1);
            break;
        case ScreenshotToolbarCommand::ConfigWatermarkAngleSet:
            changed = clampStep(m_editorState.watermarkStyle.angle, -90, 90, 1);
            break;
        default:
            break;
        }
        if (changed &&
            command != ScreenshotToolbarCommand::ConfigPenWidth &&
            command != ScreenshotToolbarCommand::ConfigPenWidthSet &&
            command != ScreenshotToolbarCommand::ScreenshotSideShadowBorder &&
            command != ScreenshotToolbarCommand::ScreenshotPostProcessStrengthSet &&
            command != ScreenshotToolbarCommand::ScreenshotSideRounded &&
            command != ScreenshotToolbarCommand::ScreenshotRoundedRadiusSet) {
            // Dual-write specialized style fields mutated by wheel config adjust.
            if (command == ScreenshotToolbarCommand::ConfigMagnifierMagnification ||
                command == ScreenshotToolbarCommand::ConfigMagnifierMagnificationSet) {
            } else if (command == ScreenshotToolbarCommand::ConfigTextOutline ||
                command == ScreenshotToolbarCommand::ConfigTextOutlineSizeSet ||
                command == ScreenshotToolbarCommand::ConfigTextBackground ||
                command == ScreenshotToolbarCommand::ConfigTextBackgroundOpacitySet ||
                command == ScreenshotToolbarCommand::ConfigTextBackgroundRoundedSet ||
                command == ScreenshotToolbarCommand::ConfigTextBackgroundPaddingSet) {
            } else if (command == ScreenshotToolbarCommand::ConfigHighLightOpacitySet) {
            } else if (command == ScreenshotToolbarCommand::ConfigWatermarkOpacitySet ||
                command == ScreenshotToolbarCommand::ConfigWatermarkStyle ||
                command == ScreenshotToolbarCommand::ConfigWatermarkFontSizeSet ||
                command == ScreenshotToolbarCommand::ConfigWatermarkGapSet ||
                command == ScreenshotToolbarCommand::ConfigWatermarkAngleSet) {
            } else if (command == ScreenshotToolbarCommand::ConfigRoundedRadius ||
                command == ScreenshotToolbarCommand::ConfigRoundedRadiusSet ||
                command == ScreenshotToolbarCommand::ConfigMosaicStrength ||
                command == ScreenshotToolbarCommand::ConfigMosaicStrengthSet) {
            } else {
            }
            applyStyleChange();
        } else if (changed &&
            (command == ScreenshotToolbarCommand::ScreenshotSideShadowBorder ||
                command == ScreenshotToolbarCommand::ScreenshotPostProcessStrengthSet ||
                command == ScreenshotToolbarCommand::ScreenshotSideRounded ||
                command == ScreenshotToolbarCommand::ScreenshotRoundedRadiusSet)) {
            ScreenshotEditorSyncToolSettingsDirty(m_editorState, true);
            UpdateCursorForPoint(pt);
            UpdateOverlay();
        }
        return changed;
    };

    auto adjustOpenSliderValue = [&]() -> bool {
        return adjustConfigValue(ScreenshotEditorOpenTertiary(m_editorState) /* OWN-95 pure */);
    };

    ScreenshotToolbarCommand toolbarCommand = ScreenshotToolbarCommand::Confirm;
    if (HitTestScreenshotToolbar(pt, toolbarCommand)) {
        if (adjustConfigValue(toolbarCommand) || adjustOpenSliderValue()) {
            return true;
        }
        return true;
    }

    // S-E-EXIT E3: Document-order hit-test + pure selected id (no Host projection).
    const std::vector<ScreenshotAnnotation> ordered =
        ScreenshotAnnotationDocumentProjectOrdered(m_annotationDocument);
    const int orderedHit = ScreenshotAnnotationHitTestLocal(
        ordered, pt, ScreenshotEditorCropRect(m_editorState));
    bool hitSelectedHandle = false;
    std::wstring selectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
    if (selectedId.empty()) {
        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
            selectedId = active->id();
        }
    }
    if (!selectedId.empty()) {
        // S-E-CLOSE-10 / E3: mid-edit geometry authority is EditSession draft-as-ann.
        const bool isLiveGeometryEdit =
            ScreenshotEditorIsMovingAnnotation(m_editorState) ||
            ScreenshotEditorIsResizingAnnotation(m_editorState) ||
            ScreenshotEditorIsRotatingAnnotation(m_editorState);
        const bool hasLiveDraft =
            isLiveGeometryEdit && AnnotationEditSessionHasDraft(m_annotationEditSession)
            && AnnotationEditSessionDraft(m_annotationEditSession).id == selectedId;
        ScreenshotAnnotation liveAnnStorage;
        const ScreenshotAnnotation* liveAnnPtr = nullptr;
        if (hasLiveDraft) {
            liveAnnPtr = &AnnotationEditSessionDraft(m_annotationEditSession);
        } else if (ScreenshotAnnotationDocumentTryLegacyById(
                m_annotationDocument, selectedId, liveAnnStorage)) {
            liveAnnPtr = &liveAnnStorage;
        }
        if (liveAnnPtr) {
            const auto layout = ScreenshotAnnotationResolveGeometryLayout(
                m_annotationDocument, *liveAnnPtr, /*preferAnnLayout=*/hasLiveDraft);
            const ScreenshotAnnotation selectedAnn = ScreenshotAnnotationWithResolvedGeometry(
                *liveAnnPtr, layout);
            hitSelectedHandle =
                ScreenshotAnnotationHitTestHandleLocal(
                    selectedAnn, pt,
                    ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth) !=
                ScreenshotAnnotationHandle::None;
        }
    }

    if (orderedHit >= 0 || hitSelectedHandle) {
        std::wstring targetId;
        if (orderedHit >= 0 && orderedHit < static_cast<int>(ordered.size())) {
            targetId = ordered[static_cast<size_t>(orderedHit)].id;
        } else {
            targetId = selectedId;
        }
        if (!targetId.empty()) {
            if (ScreenshotEditorSelectedAnnotationId(m_editorState) != targetId) {
                ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, targetId);
            }
            LoadScreenshotStyleFromSelection();
            if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolHighLight)) {
                adjustActiveToolPrimaryValue();
            } else if (!adjustOpenSliderValue()) {
                adjustActiveToolPrimaryValue();
            }
            return true;
        }
    }

    return false;
}

void OverlayWindow::ApplyActiveScreenshotStyleToSelection() {
    // S-E-EXIT E3: watermark ensure from Document ephemeral view (no Host projection).
    std::wstring selectedIdForWm = ScreenshotEditorSelectedAnnotationId(m_editorState);
    if (selectedIdForWm.empty()) {
        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
            selectedIdForWm = active->id();
        }
    }
    ScreenshotAnnotation selectedWmAnn;
    const bool selectedIsWatermark =
        ScreenshotAnnotationDocumentTryLegacyById(
            m_annotationDocument, selectedIdForWm, selectedWmAnn) &&
        selectedWmAnn.type == ScreenshotToolbarCommand::ToolWatermark;
    if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolWatermark) &&
        !selectedIsWatermark) {
        bool hadWatermark = false;
        const auto ordered = ScreenshotAnnotationDocumentProjectOrdered(m_annotationDocument);
        for (const auto& existing : ordered) {
            if (existing.type == ScreenshotToolbarCommand::ToolWatermark) {
                hadWatermark = true;
                break;
            }
        }
        EnsureWatermarkAnnotationSelected(true);
        if (!hadWatermark) return;
    }

    // S-E-EXIT E2: selected id pure; BeginModify seed from Document/draft (no projection seed).
    std::wstring selectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
    if (selectedId.empty()) {
        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
            selectedId = active->id();
        }
    }
    if (selectedId.empty()) {
        return;
    }

    // S-E-CLOSE-4: style-apply transaction via EditSession draft (not projection mid-mutate).
    // If session already active for same id (text mid-edit), reuse draft — do not clobber
    // and do not commit/clear (mid-edit session stays open until text commit).
    bool reusedMidEditSession = false;
    ScreenshotAnnotation seedAnn;
    {
        const ScreenshotAnnotation* draftPtr =
            AnnotationEditSessionHasDraft(m_annotationEditSession)
            ? &AnnotationEditSessionDraft(m_annotationEditSession)
            : nullptr;
        if (draftPtr && draftPtr->id == selectedId) {
            reusedMidEditSession = true;
        } else if (ScreenshotAnnotationDocumentResolveLiveAnn(
                m_annotationDocument, selectedId, draftPtr, seedAnn)) {
            AnnotationEditSessionBeginModify(
                m_annotationEditSession,
                ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                    m_annotationDocument, seedAnn, -1),
                &seedAnn);
        } else {
            return;
        }
    }
    if (!AnnotationEditSessionHasDraft(m_annotationEditSession)) {
        return;
    }
    auto& ann = AnnotationEditSessionDraft(m_annotationEditSession);

    auto setAnnotationCommonColor = [&](COLORREF color) {
        int presetIndex = ScreenshotPresetColorIndexFromColorLocal(color);
        ann.colorIndex = presetIndex;
        ann.hasCustomColor = ScreenshotPresetColorLocal(presetIndex) != color;
        ann.customColor = color;
        ann.colorAlpha = 100;
    };
    if (ann.type == ScreenshotToolbarCommand::ToolWatermark) {
        setAnnotationCommonColor(static_cast<COLORREF>(ScreenshotEditorWatermarkStyleOf(m_editorState).color));
    } else if (!(ann.type == ScreenshotToolbarCommand::ToolText && ScreenshotEditorIsTextStyleColorTargetActive(m_editorState))) {
        // S-H residual: pure sole active color (Host dual 4-line body deleted).
        ScreenshotAnnotationApplyActiveColor(ann, m_editorState);
    }

    // S-H residual: pure sole pen width by tool (Host dual per-tool assign deleted).
    const int solePenWidth = ScreenshotEditorPenWidthForTool(m_editorState, ann.type);
    if (ann.type == ScreenshotToolbarCommand::ToolGeometry) {
        ann.ellipse = ScreenshotEditorIsGeometryEllipse(m_editorState);
        ann.filling = ScreenshotEditorIsFillingEnabled(m_editorState);
        ann.lineStyle = ScreenshotEditorLineStyle(m_editorState);
        ann.penWidth = solePenWidth;
        ann.roundedRadius = ScreenshotEditorGeometryRoundedRadius(m_editorState);
    } else if (ann.type == ScreenshotToolbarCommand::ToolArrow) {
        ann.lineStyle = ScreenshotEditorLineStyle(m_editorState);
        ann.arrowShape = ScreenshotEditorArrowShape(m_editorState);
        ann.penWidth = solePenWidth;
    } else if (ann.type == ScreenshotToolbarCommand::ToolBrokenLine) {
        ann.lineStyle = ScreenshotEditorLineStyle(m_editorState);
        ann.penWidth = solePenWidth;
        // S-H residual: pure sole broken-line style (Host dual body deleted).
        ScreenshotAnnotationApplyBrokenLineStyle(ann, m_editorState);
    } else if (ann.type == ScreenshotToolbarCommand::ToolPencil) {
        ann.lineStyle = ScreenshotEditorLineStyle(m_editorState);
        ann.penWidth = solePenWidth;
    } else if (ann.type == ScreenshotToolbarCommand::ToolMarker ||
        ann.type == ScreenshotToolbarCommand::ToolHighLight) {
        ann.penWidth = solePenWidth;
        if (ann.type == ScreenshotToolbarCommand::ToolMarker) {
            ann.pathMode = ScreenshotEditorIsMarkerPencilMode(m_editorState) ? 1 : 2;
            ann.markerBlendMode = ScreenshotEditorMarkerBlendMode(m_editorState);
            if (ann.pathMode != 1) {
                ann.points.clear();
            }
        } else {
            // S-H residual: pure sole highlight style (Host dual body deleted).
            ScreenshotAnnotationApplyHighLightStyle(ann, m_editorState);
        }
    } else if (ann.type == ScreenshotToolbarCommand::ToolMagnifier) {
        int oldMagnification = ann.magnifierMagnification;
        ann.penWidth = solePenWidth;
        // S-H residual: pure sole magnifier style (Host dual body deleted).
        ScreenshotAnnotationApplyMagnifierStyle(ann, m_editorState);
        if (oldMagnification != ann.magnifierMagnification) {
            ScreenshotMagnifierResizeResultFromSource(ann);
        }
    } else if (ann.type == ScreenshotToolbarCommand::ToolMosaic ||
        ann.type == ScreenshotToolbarCommand::ToolAutoMosaic) {
        ann.penWidth = solePenWidth;
        ann.mosaicMode = (std::min)((std::max)(ScreenshotEditorMosaicMode(m_editorState), 0), 1);
        if (ann.type == ScreenshotToolbarCommand::ToolMosaic) {
            ann.pathMode = ScreenshotEditorIsMosaicPencilMode(m_editorState) ? 1 : 2;
            if (ann.pathMode != 1) {
                ann.points.clear();
            }
        }
    } else if (ann.type == ScreenshotToolbarCommand::ToolEraser) {
        ann.penWidth = solePenWidth;
        ann.pathMode = ScreenshotEditorIsEraserPencilMode(m_editorState) ? 1 : 2;
        if (ann.pathMode != 1) {
            ann.points.clear();
        }
    } else if (ann.type == ScreenshotToolbarCommand::ToolSerial) {
        ann.penWidth = solePenWidth;
    } else if (ann.type == ScreenshotToolbarCommand::ToolText) {
        // S-H residual: pure sole text style (Host dual body deleted).
        ScreenshotAnnotationApplyTextStyle(ann, m_editorState);
    } else if (ann.type == ScreenshotToolbarCommand::ToolWatermark) {
        // S-H residual: pure sole watermark style (Host dual body deleted).
        const auto& watermarkStyle = ScreenshotEditorWatermarkStyleOf(m_editorState);
        ann.text = watermarkStyle.text;
        ScreenshotAnnotationApplyWatermarkStyle(ann, watermarkStyle);
        ann.start = { ScreenshotEditorCropRectLeft(m_editorState), ScreenshotEditorCropRectTop(m_editorState) };
        ann.end = { ScreenshotEditorCropRectRight(m_editorState), ScreenshotEditorCropRectBottom(m_editorState) };
    }

    // S-E-CLOSE-4: if reusing mid-edit session, only mutate draft (commit later on text commit).
    // S-E-CLOSE-6 / S-E-EXIT E3: Document-first CommitModify from draft; no Host rebuild.
    if (reusedMidEditSession) {
        return;
    }
    AnnotationSnapshot afterSnap;
    ScreenshotAnnotationDocumentCommitModify(
        m_annotationDocument, ann, -1,
        ScreenshotEditorSelectedAnnotationId(m_editorState), afterSnap);
    m_annotationHistory.pushModify(
        ann.id, AnnotationEditSessionBefore(m_annotationEditSession), afterSnap);
    AnnotationEditSessionClear(m_annotationEditSession);
}

void OverlayWindow::LoadScreenshotStyleFromSelection() {
    // S-E-EXIT E3: style from Document product-read by pure id (no Host projection).
    std::wstring selectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
    if (selectedId.empty()) {
        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
            selectedId = active->id();
        }
    }
    if (selectedId.empty()) {
        return;
    }

    // S-E-19: style sole from Document product-read when item present.
    if (const ScreenshotAnnotationItem* docItem = m_annotationDocument.findById(selectedId)) {
        if (ScreenshotAnnotationDocumentApplyStyleFromItem(
                m_editorState,
                *docItem,
                m_annotationHistory.canUndo(),
                m_annotationHistory.canRedo())) {
            // Host color-picker HSV projection after pure style apply.
            const ScreenshotToolbarCommand tool =
                ScreenshotAnnotationDocumentItemToolCommand(*docItem);
            if (tool == ScreenshotToolbarCommand::ToolWatermark) {
                ScreenshotRgbToHsvLocal(
                    static_cast<COLORREF>(m_editorState.watermarkStyle.color),
                    m_editorState.colorPicker.hue,
                    m_editorState.colorPicker.saturation,
                    m_editorState.colorPicker.value);
            } else if (m_editorState.toolStyle.usesCustomColor) {
                ScreenshotRgbToHsvLocal(
                    static_cast<COLORREF>(m_editorState.toolStyle.customColor),
                    m_editorState.colorPicker.hue,
                    m_editorState.colorPicker.saturation,
                    m_editorState.colorPicker.value);
            } else {
                int index = m_editorState.colorIndices.colorIndex;
                if (tool == ScreenshotToolbarCommand::ToolGeometry) {
                    index = m_editorState.colorIndices.geometryColorIndex;
                } else if (tool == ScreenshotToolbarCommand::ToolMarker ||
                    tool == ScreenshotToolbarCommand::ToolHighLight) {
                    index = m_editorState.colorIndices.markerColorIndex;
                }
                COLORREF color = ScreenshotPresetColorLocal(index);
                ScreenshotRgbToHsvLocal(
                    color,
                    m_editorState.colorPicker.hue,
                    m_editorState.colorPicker.saturation,
                    m_editorState.colorPicker.value);
            }
            return;
        }
    }

    // S-E-EXIT E3: Document missing — no Host projection recovery.
}
void OverlayWindow::RestoreDefaultToolbarState() {
    // OWN-102: pure dual-write is read authority before reload.
    bool roundedCorners = ScreenshotEditorIsRoundedCorners(m_editorState);
    int roundedCornerRadius = ScreenshotEditorPostProcessStyleOf(m_editorState).roundedCornerRadius;
    bool keepAspectRatio = ScreenshotEditorIsKeepAspectRatio(m_editorState);
    double aspectRatio = ScreenshotEditorAspectRatio(m_editorState);
    bool postProcessEnabled = ScreenshotEditorPostProcessStyleOf(m_editorState).enabled;
    LoadScreenshotToolSettings();
    m_editorState.postProcessStyle.roundedCorners = roundedCorners;
    m_editorState.postProcessStyle.roundedCornerRadius = roundedCornerRadius;
    m_editorState.cropPrefs.keepAspectRatio = keepAspectRatio;
    m_editorState.cropPrefs.aspectRatio = aspectRatio;
    m_editorState.postProcessStyle.enabled = postProcessEnabled;
    // LoadScreenshotToolSettings already Syncs; re-mirror fields restored after load.
}
