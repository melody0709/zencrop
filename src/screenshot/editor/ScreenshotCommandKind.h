#pragma once

#include "screenshot/ScreenshotTypes.h"
#include "screenshot/editor/ScreenshotToolbarCommandGroups.h"

// Stage 2 S-C: pure classification of ScreenshotToolbarCommand without HWND.
// Overlay ActionCatalog / Toolbar still own visibility lists; this is the kind seam.

enum class ScreenshotCommandKind {
    Unknown = 0,
    SessionAction,   // Copy / Save / Confirm / Cancel / Pin / OCR helpers
    HistoryAction,   // Undo / Redo
    DrawingTool,     // ToolGeometry … ToolEraser
    ToolGroupOpen,   // OpenGeometryGroup …
    ConfigControl,   // Config* panel controls
    CropSide,        // ScreenshotSide* / post-process side chrome
    FunctionArea,    // FunctionAreaAdjust / More
};

inline ScreenshotCommandKind ScreenshotClassifyCommand(ScreenshotToolbarCommand command)
{
    if (ScreenshotIsDrawingToolCommand(command)) {
        return ScreenshotCommandKind::DrawingTool;
    }

    switch (command) {
    case ScreenshotToolbarCommand::Undo:
    case ScreenshotToolbarCommand::Redo:
        return ScreenshotCommandKind::HistoryAction;

    case ScreenshotToolbarCommand::OpenGeometryGroup:
    case ScreenshotToolbarCommand::OpenMarkerGroup:
    case ScreenshotToolbarCommand::OpenArrowGroup:
    case ScreenshotToolbarCommand::OpenTextGroup:
    case ScreenshotToolbarCommand::OpenMosaicGroup:
        return ScreenshotCommandKind::ToolGroupOpen;

    case ScreenshotToolbarCommand::More:
    case ScreenshotToolbarCommand::FunctionAreaAdjust:
    case ScreenshotToolbarCommand::MoveToolbar:
        return ScreenshotCommandKind::FunctionArea;

    case ScreenshotToolbarCommand::ScreenshotSideRounded:
    case ScreenshotToolbarCommand::ScreenshotSideKeepAspect:
    case ScreenshotToolbarCommand::ScreenshotSideShadowBorder:
    case ScreenshotToolbarCommand::ScreenshotSideRefresh:
    case ScreenshotToolbarCommand::ScreenshotPostProcessModeShadow:
    case ScreenshotToolbarCommand::ScreenshotPostProcessModeBorder:
    case ScreenshotToolbarCommand::ScreenshotPostProcessStrengthSet:
    case ScreenshotToolbarCommand::ScreenshotRoundedRadiusSet:
    case ScreenshotToolbarCommand::ScreenshotPostProcessEnableEveryScreenshot:
    case ScreenshotToolbarCommand::ScreenshotPostProcessShadowColorPick:
    case ScreenshotToolbarCommand::ScreenshotPostProcessBorderColorPick:
    case ScreenshotToolbarCommand::ToggleBorder:
    case ScreenshotToolbarCommand::ToggleShadow:
        return ScreenshotCommandKind::CropSide;

    case ScreenshotToolbarCommand::Copy:
    case ScreenshotToolbarCommand::Save:
    case ScreenshotToolbarCommand::QuickSave:
    case ScreenshotToolbarCommand::Pin:
    case ScreenshotToolbarCommand::CopyOcrText:
    case ScreenshotToolbarCommand::Confirm:
    case ScreenshotToolbarCommand::Cancel:
    case ScreenshotToolbarCommand::LongShot:
    case ScreenshotToolbarCommand::GifShot:
    case ScreenshotToolbarCommand::Translate:
    case ScreenshotToolbarCommand::OcrTable:
    case ScreenshotToolbarCommand::LatexRecognition:
    case ScreenshotToolbarCommand::WinRoi:
    case ScreenshotToolbarCommand::Print:
        return ScreenshotCommandKind::SessionAction;

    default:
        break;
    }

    // Config* and remaining palette/slider commands.
    // Explicit ranges would be brittle; treat non-classified as Config when name-level intent is config.
    switch (command) {
    case ScreenshotToolbarCommand::ConfigConsume:
    case ScreenshotToolbarCommand::ConfigGeometryRectangle:
    case ScreenshotToolbarCommand::ConfigGeometryEllipse:
    case ScreenshotToolbarCommand::ConfigPathRectangle:
    case ScreenshotToolbarCommand::ConfigPathPencil:
    case ScreenshotToolbarCommand::ConfigBrokenLineMode:
    case ScreenshotToolbarCommand::ConfigCurveMode:
    case ScreenshotToolbarCommand::ConfigBrokenLineModeNone:
    case ScreenshotToolbarCommand::ConfigBrokenLineStartArrowType:
    case ScreenshotToolbarCommand::ConfigBrokenLineEndArrowType:
    case ScreenshotToolbarCommand::ConfigMagnifierRectangle:
    case ScreenshotToolbarCommand::ConfigMagnifierEllipse:
    case ScreenshotToolbarCommand::ConfigMagnifierLinkType:
    case ScreenshotToolbarCommand::ConfigMagnifierMagnification:
    case ScreenshotToolbarCommand::ConfigToggleFilling:
    case ScreenshotToolbarCommand::ConfigToggleHighLightStroke:
    case ScreenshotToolbarCommand::ConfigToggleBrokenLineArrow:
    case ScreenshotToolbarCommand::ConfigToggleMagnifierEraseMark:
    case ScreenshotToolbarCommand::ConfigToggleMagnifierAntiAlias:
    case ScreenshotToolbarCommand::ConfigToggleMagnifierShadow:
    case ScreenshotToolbarCommand::ConfigToggleAutoMosaicSync:
    case ScreenshotToolbarCommand::ConfigTextBold:
    case ScreenshotToolbarCommand::ConfigTextItalics:
    case ScreenshotToolbarCommand::ConfigTextOutline:
    case ScreenshotToolbarCommand::ConfigTextBackground:
    case ScreenshotToolbarCommand::ConfigTextFontFamilyCombo:
    case ScreenshotToolbarCommand::ConfigTextFontSizeCombo:
    case ScreenshotToolbarCommand::ConfigTextFontFamilyMicrosoftYaHei:
    case ScreenshotToolbarCommand::ConfigTextFontFamilySegoeUi:
    case ScreenshotToolbarCommand::ConfigTextFontFamilySimSun:
    case ScreenshotToolbarCommand::ConfigTextFontFamilyArial:
    case ScreenshotToolbarCommand::ConfigTextFontSize14:
    case ScreenshotToolbarCommand::ConfigTextFontSize18:
    case ScreenshotToolbarCommand::ConfigTextFontSize2698:
    case ScreenshotToolbarCommand::ConfigTextFontSize24:
    case ScreenshotToolbarCommand::ConfigTextFontSize32:
    case ScreenshotToolbarCommand::ConfigTextFontSize48:
    case ScreenshotToolbarCommand::ConfigLineStyle:
    case ScreenshotToolbarCommand::ConfigArrowShape:
    case ScreenshotToolbarCommand::ConfigMarkerBlendMode:
    case ScreenshotToolbarCommand::ConfigMosaicMode:
    case ScreenshotToolbarCommand::ConfigSerialStyle:
    case ScreenshotToolbarCommand::ConfigSerialDecrease:
    case ScreenshotToolbarCommand::ConfigSerialIncrease:
    case ScreenshotToolbarCommand::ConfigSerialType:
    case ScreenshotToolbarCommand::ConfigSerialAdvanced:
    case ScreenshotToolbarCommand::ConfigPenWidth:
    case ScreenshotToolbarCommand::ConfigRoundedRadius:
    case ScreenshotToolbarCommand::ConfigMosaicStrength:
    case ScreenshotToolbarCommand::ConfigClearAllMarks:
    case ScreenshotToolbarCommand::ConfigOpenColorPalette:
    case ScreenshotToolbarCommand::ConfigLineStyleSolid:
    case ScreenshotToolbarCommand::ConfigLineStyleDash:
    case ScreenshotToolbarCommand::ConfigLineStyleDot:
    case ScreenshotToolbarCommand::ConfigLineStyleDashDot:
    case ScreenshotToolbarCommand::ConfigLineStyleDashDotDot:
    case ScreenshotToolbarCommand::ConfigArrowShapeStraight:
    case ScreenshotToolbarCommand::ConfigArrowShapeStraightBilateral:
    case ScreenshotToolbarCommand::ConfigArrowShapeOutline:
    case ScreenshotToolbarCommand::ConfigArrowShapeFill:
    case ScreenshotToolbarCommand::ConfigArrowShapeDimensionLine:
    case ScreenshotToolbarCommand::ConfigArrowShapeSolid:
    case ScreenshotToolbarCommand::ConfigArrowShapeSolidBilateral:
    case ScreenshotToolbarCommand::ConfigArrowShapeDimensionArrow:
    case ScreenshotToolbarCommand::ConfigArrowHeadNone:
    case ScreenshotToolbarCommand::ConfigArrowHeadLineArrow:
    case ScreenshotToolbarCommand::ConfigArrowHeadSolidArrow:
    case ScreenshotToolbarCommand::ConfigArrowHeadUnfilledArrow:
    case ScreenshotToolbarCommand::ConfigArrowHeadSolidDot:
    case ScreenshotToolbarCommand::ConfigArrowHeadOpenCircle:
    case ScreenshotToolbarCommand::ConfigArrowHeadSolidDiamond:
    case ScreenshotToolbarCommand::ConfigArrowHeadOpenDiamond:
    case ScreenshotToolbarCommand::ConfigArrowHeadArchitecturalTick:
    case ScreenshotToolbarCommand::ConfigArrowHeadCross:
    case ScreenshotToolbarCommand::ConfigArrowHeadOpenArrow:
    case ScreenshotToolbarCommand::ConfigArrowHeadClosedFilledArrow:
    case ScreenshotToolbarCommand::ConfigMagnifierLinkHide:
    case ScreenshotToolbarCommand::ConfigMagnifierLinkLine:
    case ScreenshotToolbarCommand::ConfigMagnifierLinkDotLine:
    case ScreenshotToolbarCommand::ConfigMagnifierLinkShape:
    case ScreenshotToolbarCommand::ConfigMarkerBlendMultiply:
    case ScreenshotToolbarCommand::ConfigMarkerBlendTranslucent:
    case ScreenshotToolbarCommand::ConfigMosaicModeMosaic:
    case ScreenshotToolbarCommand::ConfigMosaicModeBlur:
    case ScreenshotToolbarCommand::ConfigMosaicModeSmartErase:
    case ScreenshotToolbarCommand::ConfigSerialType123:
    case ScreenshotToolbarCommand::ConfigSerialTypeRoman:
    case ScreenshotToolbarCommand::ConfigSerialTypeLower:
    case ScreenshotToolbarCommand::ConfigSerialTypeUpper:
    case ScreenshotToolbarCommand::ConfigSerialTypeChinese:
    case ScreenshotToolbarCommand::ConfigPenWidthSet:
    case ScreenshotToolbarCommand::ConfigRoundedRadiusSet:
    case ScreenshotToolbarCommand::ConfigMosaicStrengthSet:
    case ScreenshotToolbarCommand::ConfigMagnifierMagnificationSet:
    case ScreenshotToolbarCommand::ConfigTextOutlineSizeSet:
    case ScreenshotToolbarCommand::ConfigTextBackgroundOpacitySet:
    case ScreenshotToolbarCommand::ConfigTextBackgroundRoundedSet:
    case ScreenshotToolbarCommand::ConfigTextBackgroundPaddingSet:
    case ScreenshotToolbarCommand::ConfigHighLightOpacitySet:
    case ScreenshotToolbarCommand::ConfigWatermarkContent:
    case ScreenshotToolbarCommand::ConfigWatermarkPositionCombo:
    case ScreenshotToolbarCommand::ConfigWatermarkStyle:
    case ScreenshotToolbarCommand::ConfigWatermarkFontFamilyCombo:
    case ScreenshotToolbarCommand::ConfigWatermarkOpacitySet:
    case ScreenshotToolbarCommand::ConfigWatermarkFontSizeSet:
    case ScreenshotToolbarCommand::ConfigWatermarkGapSet:
    case ScreenshotToolbarCommand::ConfigWatermarkAngleSet:
    case ScreenshotToolbarCommand::ConfigWatermarkPositionTile:
    case ScreenshotToolbarCommand::ConfigWatermarkPositionBottomRight:
    case ScreenshotToolbarCommand::ConfigWatermarkPositionBottomLeft:
    case ScreenshotToolbarCommand::ConfigWatermarkPositionTopRight:
    case ScreenshotToolbarCommand::ConfigWatermarkPositionTopLeft:
    case ScreenshotToolbarCommand::ConfigWatermarkPositionTopCenter:
    case ScreenshotToolbarCommand::ConfigWatermarkPositionBottomCenter:
    case ScreenshotToolbarCommand::ConfigWatermarkPositionCenter:
    case ScreenshotToolbarCommand::ConfigAutoMosaicProviderGate:
    case ScreenshotToolbarCommand::ConfigColorClear:
    case ScreenshotToolbarCommand::ConfigColorRed:
    case ScreenshotToolbarCommand::ConfigColorOrange:
    case ScreenshotToolbarCommand::ConfigColorYellow:
    case ScreenshotToolbarCommand::ConfigColorGreen:
    case ScreenshotToolbarCommand::ConfigColorBlue:
    case ScreenshotToolbarCommand::ConfigColorDark:
    case ScreenshotToolbarCommand::ConfigColorWhite:
    case ScreenshotToolbarCommand::ConfigColorPickerSatVal:
    case ScreenshotToolbarCommand::ConfigColorPickerHue:
    case ScreenshotToolbarCommand::ConfigColorPickerAlpha:
    case ScreenshotToolbarCommand::ConfigColorPickerConfirm:
    case ScreenshotToolbarCommand::ConfigColorPickerCancel:
        return ScreenshotCommandKind::ConfigControl;
    default:
        return ScreenshotCommandKind::Unknown;
    }
}

inline bool ScreenshotCommandIsSessionAction(ScreenshotToolbarCommand command)
{
    return ScreenshotClassifyCommand(command) == ScreenshotCommandKind::SessionAction;
}

inline bool ScreenshotCommandIsConfigControl(ScreenshotToolbarCommand command)
{
    return ScreenshotClassifyCommand(command) == ScreenshotCommandKind::ConfigControl;
}

// S-C-1: pure sole definition of slider drag targets (Host IsScreenshotSliderCommand deleted).
inline bool ScreenshotCommandIsSliderControl(ScreenshotToolbarCommand command)
{
    switch (command) {
    case ScreenshotToolbarCommand::ScreenshotPostProcessStrengthSet:
    case ScreenshotToolbarCommand::ScreenshotRoundedRadiusSet:
    case ScreenshotToolbarCommand::ConfigPenWidthSet:
    case ScreenshotToolbarCommand::ConfigRoundedRadiusSet:
    case ScreenshotToolbarCommand::ConfigMosaicStrengthSet:
    case ScreenshotToolbarCommand::ConfigMagnifierMagnificationSet:
    case ScreenshotToolbarCommand::ConfigTextOutlineSizeSet:
    case ScreenshotToolbarCommand::ConfigTextBackgroundOpacitySet:
    case ScreenshotToolbarCommand::ConfigTextBackgroundRoundedSet:
    case ScreenshotToolbarCommand::ConfigTextBackgroundPaddingSet:
    case ScreenshotToolbarCommand::ConfigHighLightOpacitySet:
    case ScreenshotToolbarCommand::ConfigWatermarkOpacitySet:
    case ScreenshotToolbarCommand::ConfigWatermarkFontSizeSet:
    case ScreenshotToolbarCommand::ConfigWatermarkGapSet:
    case ScreenshotToolbarCommand::ConfigWatermarkAngleSet:
        return true;
    default:
        return false;
    }
}

inline bool ScreenshotCommandIsColorPickerDrag(ScreenshotToolbarCommand command)
{
    return command == ScreenshotToolbarCommand::ConfigColorPickerSatVal ||
        command == ScreenshotToolbarCommand::ConfigColorPickerHue ||
        command == ScreenshotToolbarCommand::ConfigColorPickerAlpha;
}
