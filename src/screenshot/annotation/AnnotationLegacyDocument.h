#pragma once

#include "core/WideStringUtils.h"
#include "screenshot/ScreenshotAnnotationLegacy.h"
#include "screenshot/annotation/AnnotationMigration.h"
#include "screenshot/annotation/AnnotationModel.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotToolbarCommandGroups.h"
#include "screenshot/ScreenshotTypes.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// Builds an ephemeral ordered view; an active draft can override one item.
// Defined later after the Document-to-legacy conversion helpers.
inline std::vector<ScreenshotAnnotation>
ScreenshotAnnotationDocumentProjectOrdered(
    const AnnotationDocument& document,
    const std::wstring& liveDragId = L"",
    const ScreenshotAnnotation* liveDraft = nullptr);

// S-E-20: Document product-read history snapshot by stable id.
// Host convertLegacyAnnotation not authority when Document holds item.
// Returns true and fills out when item found.
inline bool ScreenshotAnnotationDocumentTakeSnapshotById(
    const AnnotationDocument& document,
    const std::wstring& id,
    AnnotationSnapshot& out)
{
    if (id.empty()) return false;
    const ScreenshotAnnotationItem* item = document.findById(id);
    if (!item) return false;
    out = item->takeSnapshot();
    return true;
}

// S-E-21: Document product-read history before-snapshot for modify start.
// Prefer Document item; recovery Host convert only when Document missing.
// S-E-CLOSE-12: const ann — no Host projection id mutation via EnsureLegacyAnnotationId.
// Document product-read by id first. Host recovery uses local copy only (empty-id / missing).
inline AnnotationSnapshot ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    int index)
{
    AnnotationSnapshot snap;
    if (!ann.id.empty() &&
        ScreenshotAnnotationDocumentTakeSnapshotById(document, ann.id, snap)) {
        return snap;
    }
    // Recovery uses a local copy and never mutates caller data.
    ScreenshotAnnotation local = ann;
    EnsureLegacyAnnotationId(local);
    if (!local.id.empty() && local.id != ann.id &&
        ScreenshotAnnotationDocumentTakeSnapshotById(document, local.id, snap)) {
        return snap;
    }
    return convertLegacyAnnotation(local, index).takeSnapshot();
}

// S-E-18/S-E-19: map Document item type+role → Host tool command (role tools first).
inline ScreenshotToolbarCommand ScreenshotAnnotationDocumentItemToolCommand(
    const ScreenshotAnnotationItem& item)
{
    switch (item.role()) {
    case AnnotationRole::Magnifier: return ScreenshotToolbarCommand::ToolMagnifier;
    case AnnotationRole::HighLight: return ScreenshotToolbarCommand::ToolHighLight;
    case AnnotationRole::Watermark: return ScreenshotToolbarCommand::ToolWatermark;
    case AnnotationRole::AutoMosaicRect:
    case AnnotationRole::AutoMosaicPath: return ScreenshotToolbarCommand::ToolAutoMosaic;
    default: break;
    }
    const ScreenshotToolbarCommand fromType = AnnotationTypeToToolCommand(item.type());
    if (fromType != ScreenshotToolbarCommand::Confirm) {
        return fromType;
    }
    return ScreenshotToolbarCommand::Confirm;
}

// S-E-18: Document product-read Geometry/Arrow style into pure editor state.
// Kept as thin wrapper over S-E-19 full apply for existing call sites/tests.
// Returns true when item is Geometry or Arrow and style applied.
inline bool ScreenshotAnnotationDocumentApplyStyleFromItem(
    ScreenshotEditorState& state,
    const ScreenshotAnnotationItem& item,
    bool canUndo,
    bool canRedo);


// S-E-19: Document product-read style for all tools into pure editor state.
// Host vector not consulted. Returns true when item maps to a drawing tool and style applied.
// Color-picker HSV projection remains Host (GDI UI); pure only writes style scalars.
inline bool ScreenshotAnnotationDocumentApplyStyleFromItem(
    ScreenshotEditorState& state,
    const ScreenshotAnnotationItem& item,
    bool canUndo,
    bool canRedo)
{
    const ScreenshotToolbarCommand tool = ScreenshotAnnotationDocumentItemToolCommand(item);
    if (!ScreenshotIsDrawingToolCommand(tool)) {
        return false;
    }

    ScreenshotEditorSelectToolWithHistory(state, tool, canUndo, canRedo);
    if (tool == ScreenshotToolbarCommand::ToolGeometry ||
        tool == ScreenshotToolbarCommand::ToolHighLight) {
        state.toolGroupMemory.geometryTool = tool;
    } else if (tool == ScreenshotToolbarCommand::ToolPencil ||
        tool == ScreenshotToolbarCommand::ToolMarker) {
        state.toolGroupMemory.markerTool = tool;
    } else if (tool == ScreenshotToolbarCommand::ToolArrow ||
        tool == ScreenshotToolbarCommand::ToolBrokenLine ||
        tool == ScreenshotToolbarCommand::ToolMagnifier) {
        state.toolGroupMemory.arrowTool = tool;
    } else if (tool == ScreenshotToolbarCommand::ToolText ||
        tool == ScreenshotToolbarCommand::ToolWatermark) {
        state.toolGroupMemory.textTool = tool;
    } else if (tool == ScreenshotToolbarCommand::ToolMosaic ||
        tool == ScreenshotToolbarCommand::ToolAutoMosaic) {
        state.toolGroupMemory.mosaicTool = tool;
    }

    // Color: Watermark uses watermark color; others use tool color.
    if (tool == ScreenshotToolbarCommand::ToolWatermark) {
        state.watermarkStyle.color = static_cast<unsigned int>(
            item.getColor(AnnotationProperty::WatermarkColor, 0));
    } else {
        state.toolStyle.usesCustomColor = item.hasProperty(AnnotationProperty::Color);
        if (state.toolStyle.usesCustomColor) {
            state.toolStyle.customColor =
                static_cast<unsigned int>(item.getColor(AnnotationProperty::Color, 0));
        }
        state.toolStyle.colorAlpha = item.getInt(AnnotationProperty::ColorAlpha, 100);
        const int colorIndex =
            (std::min)((std::max)(item.getInt(AnnotationProperty::ColorIndex, 0), 0), 6);
        if (tool == ScreenshotToolbarCommand::ToolGeometry) {
            state.colorIndices.geometryColorIndex = colorIndex;
        } else if (tool == ScreenshotToolbarCommand::ToolMarker ||
            tool == ScreenshotToolbarCommand::ToolHighLight) {
            state.colorIndices.markerColorIndex = colorIndex;
        } else {
            state.colorIndices.colorIndex = colorIndex;
        }
    }

    const int penWidth = item.getInt(AnnotationProperty::PenWidth, 0);
    const int pathMode = item.getInt(AnnotationProperty::PathMode, 0);

    if (tool == ScreenshotToolbarCommand::ToolGeometry) {
        // PathMode 3 = ellipse (convertLegacyAnnotation: ellipse → PathMode 3).
        state.toolModes.geometryEllipse = pathMode == 3;
        state.toolStyle.fillingEnabled = item.getBool(AnnotationProperty::Filling, false);
        state.toolModes.lineStyle = item.getInt(AnnotationProperty::PenStyle, 0);
        if (penWidth > 0) {
            state.toolStyle.geometryPenWidth = penWidth;
        }
        state.effectStyle.geometryRoundedRadius =
            item.getInt(AnnotationProperty::RectRoundRadius, 0);
    } else if (tool == ScreenshotToolbarCommand::ToolArrow) {
        state.toolModes.lineStyle = item.getInt(AnnotationProperty::PenStyle, 0);
        const int arrowShape = item.getInt(AnnotationProperty::LineShape, 0);
        if (arrowShape >= 1 && arrowShape <= 8) {
            state.toolModes.arrowShape = arrowShape;
        }
        if (penWidth > 0) {
            state.toolStyle.arrowPenWidth = penWidth;
        }
    } else if (tool == ScreenshotToolbarCommand::ToolBrokenLine) {
        state.toolModes.lineStyle = item.getInt(AnnotationProperty::PenStyle, 0);
        if (penWidth > 0) {
            state.toolStyle.pencilPenWidth = penWidth;
        }
        state.toolModes.brokenLineMode =
            (std::min)((std::max)(item.getInt(AnnotationProperty::BrokenLineMode, 0), 0), 1);
        state.toolModes.brokenLineArrow =
            item.getBool(AnnotationProperty::BrokenLineArrowEnabled, false);
        state.toolModes.brokenLineStartArrowType =
            item.getInt(AnnotationProperty::BrokenLineStartArrowType, 0);
        state.toolModes.brokenLineEndArrowType =
            item.getInt(AnnotationProperty::BrokenLineEndArrowType, 0);
    } else if (tool == ScreenshotToolbarCommand::ToolPencil) {
        state.toolModes.lineStyle = item.getInt(AnnotationProperty::PenStyle, 0);
        if (penWidth > 0) {
            state.toolStyle.pencilPenWidth = penWidth;
        }
    } else if (tool == ScreenshotToolbarCommand::ToolMarker) {
        if (penWidth > 0) {
            state.toolStyle.markerPenWidth = penWidth;
        }
        if (pathMode > 0) {
            state.toolModes.markerPencilMode = pathMode == 1;
        }
        state.effectStyle.markerBlendMode =
            (std::min)((std::max)(item.getInt(AnnotationProperty::MarkerBlendMode, 0), 0), 1);
    } else if (tool == ScreenshotToolbarCommand::ToolHighLight) {
        if (penWidth > 0) {
            state.toolStyle.markerPenWidth = penWidth;
        }
        state.highLightStyle.opacity =
            static_cast<int>(item.getDouble(AnnotationProperty::HighLightOpacity, 0.0));
        state.highLightStyle.stroke = item.getBool(AnnotationProperty::HighLightStroke, false);
        state.highLightStyle.strokeColor = static_cast<unsigned int>(
            item.getColor(AnnotationProperty::HighLightStrokeColor, 0));
    } else if (tool == ScreenshotToolbarCommand::ToolMagnifier) {
        state.magnifierStyle.ellipse = pathMode == 3;
        if (penWidth > 0) {
            state.toolStyle.magnifierPenWidth = penWidth;
        }
        state.magnifierStyle.roundedRadius =
            item.getInt(AnnotationProperty::RectRoundRadius, 0);
        state.magnifierStyle.linkType =
            item.getInt(AnnotationProperty::MagnifierLinkType, 0);
        state.magnifierStyle.magnification =
            item.getInt(AnnotationProperty::MagnifierMagnification, 0);
        state.magnifierStyle.antiAlias =
            item.getBool(AnnotationProperty::MagnifierAntiAlias, false);
        state.magnifierStyle.eraseMark =
            item.getBool(AnnotationProperty::MagnifierEraseMark, false);
        state.magnifierStyle.shadow =
            item.getBool(AnnotationProperty::MagnifierShadow, false);
    } else if (tool == ScreenshotToolbarCommand::ToolMosaic ||
        tool == ScreenshotToolbarCommand::ToolAutoMosaic) {
        if (penWidth > 0) {
            state.toolStyle.mosaicPenWidth = penWidth;
        }
        state.effectStyle.mosaicMode =
            (std::min)((std::max)(item.getInt(AnnotationProperty::MosaicMode, 0), 0), 1);
        if (tool == ScreenshotToolbarCommand::ToolMosaic && pathMode > 0) {
            state.toolModes.mosaicPencilMode = pathMode == 1;
        }
    } else if (tool == ScreenshotToolbarCommand::ToolEraser) {
        if (penWidth > 0) {
            state.toolStyle.eraserPenWidth = penWidth;
        }
        if (pathMode > 0) {
            state.toolModes.eraserPencilMode = pathMode == 1;
        }
    } else if (tool == ScreenshotToolbarCommand::ToolSerial) {
        if (penWidth > 0) {
            state.toolStyle.serialPenWidth = penWidth;
        }
    } else if (tool == ScreenshotToolbarCommand::ToolText) {
        state.textStyle.bold = item.getBool(AnnotationProperty::TextBold, false);
        state.textStyle.italics = item.getBool(AnnotationProperty::TextItalics, false);
        state.textStyle.outline = item.getBool(AnnotationProperty::TextOutline, false);
        state.textStyle.outlineSize = item.getInt(AnnotationProperty::TextOutlineSize, 0);
        state.textStyle.outlineColor = static_cast<unsigned int>(
            item.getColor(AnnotationProperty::TextOutlineColor, 0));
        state.textStyle.background = item.getBool(AnnotationProperty::TextBackground, false);
        state.textStyle.backgroundColor = static_cast<unsigned int>(
            item.getColor(AnnotationProperty::TextBackgroundColor, 0));
        state.textStyle.backgroundOpacity =
            item.getInt(AnnotationProperty::TextBackgroundOpacity, 0);
        state.textStyle.backgroundRounded =
            item.getInt(AnnotationProperty::TextBackgroundRounded, 0);
        state.textStyle.backgroundPadding =
            item.getInt(AnnotationProperty::TextBackgroundPadding, 0);
        const std::wstring& fontFamily = item.getString(AnnotationProperty::TextFontFamily);
        if (!fontFamily.empty()) {
            state.textStyle.fontFamily = fontFamily;
        }
        const double fontSizeF = item.getDouble(AnnotationProperty::TextFontSize, 0.0);
        if (fontSizeF > 0.0) {
            state.textStyle.fontSizeF = (std::max)(fontSizeF, 8.0);
            state.textStyle.fontSize =
                (std::max)(static_cast<int>(std::lround(state.textStyle.fontSizeF)), 8);
        }
    } else if (tool == ScreenshotToolbarCommand::ToolWatermark) {
        state.watermarkStyle.bold = item.getBool(AnnotationProperty::TextBold, false);
        state.watermarkStyle.italics = item.getBool(AnnotationProperty::TextItalics, false);
        const std::wstring& watermarkText = item.getString(AnnotationProperty::WatermarkText);
        state.watermarkStyle.text = !watermarkText.empty() ? watermarkText : item.text();
        state.watermarkStyle.opacity = (std::min)((std::max)(
            static_cast<int>(item.getDouble(AnnotationProperty::WatermarkOpacity, 10.0)), 10), 100);
        state.watermarkStyle.fontSize = (std::min)((std::max)(
            item.getInt(AnnotationProperty::WatermarkFontSize, 8), 8), 80);
        state.watermarkStyle.gap = (std::min)((std::max)(
            item.getInt(AnnotationProperty::WatermarkGap, 0), 0), 100);
        state.watermarkStyle.angle = item.getInt(AnnotationProperty::WatermarkAngle, 0);
        const std::wstring& watermarkFont =
            item.getString(AnnotationProperty::WatermarkFontFamily);
        if (!watermarkFont.empty()) {
            state.watermarkStyle.fontFamily = watermarkFont;
        } else {
            const std::wstring& textFont = item.getString(AnnotationProperty::TextFontFamily);
            if (!textFont.empty()) {
                state.watermarkStyle.fontFamily = textFont;
            }
        }
        state.watermarkStyle.position = item.getInt(AnnotationProperty::WatermarkPosition, 0);
    }
    return true;
}

// Document active state is synchronized only from the stable selection id.
inline void ScreenshotAnnotationDocumentSyncActive(
    AnnotationDocument& document,
    const std::wstring& activeId)
{
    if (activeId.empty()) {
        document.clearActiveItem();
    } else {
        document.setActiveItem(activeId);
    }
}

// S-E-EXIT E3: select by id Document sole — no Host projection vector.
// Count from Document; layout index short-life from Document order when needed.
inline void ScreenshotAnnotationSelectById(
    ScreenshotEditorState& state,
    AnnotationDocument& document,
    const std::wstring& id)
{
    const int count = static_cast<int>(document.count());
    ScreenshotEditorSetAnnotationCount(state, count);
    if (id.empty()) {
        ScreenshotEditorSelectAnnotationById(state, -1, L"");
        ScreenshotAnnotationDocumentSyncActive(document, L"");
        return;
    }
    // Layout index = Document order position (ephemeral; id remains authority).
    int index = -1;
    int i = 0;
    document.forEach([&](const ScreenshotAnnotationItem& item) {
        if (index < 0 && item.id() == id) {
            index = i;
        }
        ++i;
    });
    if (index < 0) {
        ScreenshotEditorSelectAnnotationById(state, -1, L"");
        ScreenshotAnnotationDocumentSyncActive(document, id);
        return;
    }
    ScreenshotEditorSelectAnnotationById(state, index, id);
    ScreenshotAnnotationDocumentSyncActive(document, id);
}

// S-E-EXIT E3: Document remove sole — no Host projection.
// Returns true when Document item removed. Selection must be re-synced by id product-side.
inline bool ScreenshotAnnotationDocumentRemove(
    AnnotationDocument& document,
    const std::wstring& id)
{
    if (id.empty()) return false;
    return document.removeById(id);
}

// S-E-EXIT E3: history insert Document sole — no Host projection rebuild.
// Returns created id (empty on fail). StackIndex from snapshot clamped to Document count.
inline std::wstring ScreenshotAnnotationDocumentInsertFromSnapshotSole(
    AnnotationDocument& document,
    const AnnotationSnapshot& snap,
    const std::wstring& fallbackId)
{
    ScreenshotAnnotation ann = LegacyAnnotationFromSnapshot(snap, fallbackId);
    if (ann.type == ScreenshotToolbarCommand::Confirm) return L"";
    EnsureLegacyAnnotationId(ann);
    int insertAt = snap.getInt(AnnotationProperty::StackIndex, static_cast<int>(document.count()));
    insertAt = (std::max)(0, (std::min)(insertAt, static_cast<int>(document.count())));
    auto item = std::make_unique<ScreenshotAnnotationItem>(convertLegacyAnnotation(ann, insertAt));
    item->restoreFromSnapshot(snap);
    if (item->id().empty()) {
        item->setId(ann.id);
    }
    const std::wstring id = item->id();
    if (insertAt >= document.count()) {
        document.add(std::move(item));
    } else {
        document.insertAt(insertAt, std::move(item));
    }
    return id;
}

// S-E-EXIT E3: history replace Document sole — no Host projection rebuild.
inline bool ScreenshotAnnotationDocumentReplaceFromSnapshotSole(
    AnnotationDocument& document,
    const std::wstring& id,
    const AnnotationSnapshot& snap)
{
    if (id.empty() || !document.findById(id)) return false;
    ScreenshotAnnotation ann = LegacyAnnotationFromSnapshot(snap, id);
    if (ann.type == ScreenshotToolbarCommand::Confirm) return false;
    if (ann.id.empty()) ann.id = id;
    EnsureLegacyAnnotationId(ann);
    auto item = std::make_unique<ScreenshotAnnotationItem>(convertLegacyAnnotation(ann, 0));
    item->restoreFromSnapshot(snap);
    if (item->id().empty()) {
        item->setId(ann.id);
    }
    return document.replaceById(id, std::move(item));
}

// Convert a local draft/legacy-shaped value into the committed Document store.
// This adapter never rebuilds or owns a persistent Host projection.

inline void ScreenshotAnnotationDocumentAddFromLegacy(
    AnnotationDocument& document,
    ScreenshotAnnotation& ann,
    int index,
    const std::wstring& activeId = L"")
{
    EnsureLegacyAnnotationId(ann);
    auto item = std::make_unique<ScreenshotAnnotationItem>(convertLegacyAnnotation(ann, index));
    if (index < 0 || index >= document.count()) {
        document.add(std::move(item));
    } else {
        document.insertAt(index, std::move(item));
    }
    if (!activeId.empty()) {
        document.setActiveItem(activeId);
    }
}

// S-E-EXIT E3: create Document sole — no Host projection rebuild.
// Returns Document-order index of created item (short-life layout). Selects by id.
inline int ScreenshotAnnotationDocumentCreate(
    AnnotationDocument& document,
    ScreenshotAnnotation& ann,
    ScreenshotEditorState& state)
{
    EnsureLegacyAnnotationId(ann);
    const int index = static_cast<int>(document.count());
    ScreenshotAnnotationDocumentAddFromLegacy(document, ann, index, ann.id);
    ScreenshotAnnotationSelectById(state, document, ann.id);
    return index;
}

// S-E-EXIT E3: text pending-create Document sole without select.
// Returns Document-order index. Count dual-write from Document.
inline int ScreenshotAnnotationDocumentCreatePendingText(
    AnnotationDocument& document,
    ScreenshotAnnotation& ann,
    ScreenshotEditorState& state)
{
    EnsureLegacyAnnotationId(ann);
    const int index = static_cast<int>(document.count());
    ScreenshotAnnotationDocumentAddFromLegacy(document, ann, index, L"");
    ScreenshotEditorSetAnnotationCount(state, static_cast<int>(document.count()));
    return index;
}

inline bool ScreenshotAnnotationDocumentReplaceFromLegacy(
    AnnotationDocument& document,
    ScreenshotAnnotation& ann,
    int index,
    const std::wstring& activeId = L"")
{
    EnsureLegacyAnnotationId(ann);
    const std::wstring id = ann.id;
    auto item = std::make_unique<ScreenshotAnnotationItem>(convertLegacyAnnotation(ann, index));
    const bool ok = document.replaceById(id, std::move(item));
    if (!ok) {
        // Recovery: add when Document does not contain this id.
        document.add(std::make_unique<ScreenshotAnnotationItem>(convertLegacyAnnotation(ann, index)));
    }
    if (!activeId.empty()) {
        document.setActiveItem(activeId);
    }
    return ok;
}

// Commit a legacy-shaped draft into Document and capture its resulting snapshot.
inline bool ScreenshotAnnotationDocumentCommitModify(
    AnnotationDocument& document,
    ScreenshotAnnotation& ann,
    int index,
    const std::wstring& activeId,
    AnnotationSnapshot& outAfter)
{
    EnsureLegacyAnnotationId(ann);
    ScreenshotAnnotationDocumentReplaceFromLegacy(document, ann, index, activeId);
    if (ScreenshotAnnotationDocumentTakeSnapshotById(document, ann.id, outAfter)) {
        return true;
    }
    // Recovery: Document item missing after replace.
    outAfter = convertLegacyAnnotation(ann, index).takeSnapshot();
    return true;
}

// S-E-31: create history snapshot sole — Document product-read after create.
// Prefer TakeSnapshotById; recovery Host convert only when Document missing.
// Call after Document create or replacement.
inline bool ScreenshotAnnotationDocumentCommitCreateSnapshot(
    const AnnotationDocument& document,
    ScreenshotAnnotation& ann,
    int index,
    AnnotationSnapshot& outSnap)
{
    EnsureLegacyAnnotationId(ann);
    if (ScreenshotAnnotationDocumentTakeSnapshotById(document, ann.id, outSnap)) {
        return true;
    }
    // Recovery: Document item missing after create.
    outSnap = convertLegacyAnnotation(ann, index).takeSnapshot();
    return true;
}

// S-E-43: Geometry layout product-read from Document by id (Phase A1 Host-vector exit).
// Dual-written: start/end (item fields), PathPoints, Angle, PathMode (ellipse=3).
// S-E-CLOSE-2/9/10: mid-edit geometry lives on EditSession draft.
// preferAnnLayout: when true, use ann layout fields (draft-as-ann mid-edit) — not Host projection.
// Export/post-commit: Document first. Host recovery when Document item missing.
struct ScreenshotAnnotationGeometryLayout {
    POINT start = {};
    POINT end = {};
    std::vector<POINT> points;
    double angle = 0.0;
    int pathMode = 0;
    bool ellipse = false;
};

// Pure PathPoints parse (mirrors AnnotationMigration static; free for Document product-read).
inline std::vector<POINT> ScreenshotAnnotationParsePathPoints(const std::wstring& value)
{
    std::vector<POINT> points;
    size_t pos = 0;
    while (pos < value.length()) {
        size_t comma = value.find(L',', pos);
        if (comma == std::wstring::npos) break;
        size_t semi = value.find(L';', comma + 1);
        const std::wstring xText = value.substr(pos, comma - pos);
        const std::wstring yText = semi == std::wstring::npos
            ? value.substr(comma + 1)
            : value.substr(comma + 1, semi - comma - 1);
        POINT pt = {
            WideParseJsonIntToken(xText),
            WideParseJsonIntToken(yText)
        };
        points.push_back(pt);
        if (semi == std::wstring::npos) break;
        pos = semi + 1;
    }
    return points;
}

inline bool ScreenshotAnnotationDocumentResolveGeometryLayout(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationGeometryLayout& out)
{
    if (ann.id.empty()) return false;
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    out.start = item->start();
    out.end = item->end();
    out.points = ScreenshotAnnotationParsePathPoints(
        item->getString(AnnotationProperty::PathPoints));
    out.angle = item->getDouble(AnnotationProperty::Angle, 0.0);
    out.pathMode = item->getInt(AnnotationProperty::PathMode, 0);
    out.ellipse = out.pathMode == 3;
    return true;
}

inline ScreenshotAnnotationGeometryLayout
ScreenshotAnnotationGeometryLayoutFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationGeometryLayout s;
    s.start = ann.start;
    s.end = ann.end;
    s.points = ann.points;
    s.angle = ann.angle;
    s.pathMode = ann.pathMode;
    s.ellipse = ann.ellipse;
    return s;
}

// S-E-43: resolve Geometry layout — Document product-read first, Host recovery.
// S-E-CLOSE-10: preferAnnLayout — when true, use ann layout fields (draft-as-ann mid-edit).
// Not Host projection authority; product must pass draft (or committed ann) as ann.
inline ScreenshotAnnotationGeometryLayout
ScreenshotAnnotationResolveGeometryLayout(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    bool preferAnnLayout = false)
{
    if (preferAnnLayout) {
        return ScreenshotAnnotationGeometryLayoutFromHost(ann);
    }
    ScreenshotAnnotationGeometryLayout s;
    if (ScreenshotAnnotationDocumentResolveGeometryLayout(document, ann, s)) {
        return s;
    }
    return ScreenshotAnnotationGeometryLayoutFromHost(ann);
}

// Apply resolved layout onto a Host ann copy for draw/export that still take ScreenshotAnnotation.
inline ScreenshotAnnotation
ScreenshotAnnotationWithResolvedGeometry(
    const ScreenshotAnnotation& ann,
    const ScreenshotAnnotationGeometryLayout& layout)
{
    ScreenshotAnnotation out = ann;
    out.start = layout.start;
    out.end = layout.end;
    out.points = layout.points;
    out.angle = layout.angle;
    out.pathMode = layout.pathMode;
    out.ellipse = layout.ellipse;
    return out;
}

// S-E-EXIT E1: Document product-read → Host-shaped legacy by id (no Host projection).
// Returns false when id empty or Document item missing.
inline bool ScreenshotAnnotationDocumentTryLegacyById(
    const AnnotationDocument& document,
    const std::wstring& id,
    ScreenshotAnnotation& out)
{
    if (id.empty()) {
        return false;
    }
    const ScreenshotAnnotationItem* item = document.findById(id);
    if (!item) {
        return false;
    }
    out = LegacyAnnotationFromSnapshot(item->takeSnapshot(), item->id());
    return true;
}

// S-E-EXIT E2: live ann for BeginModify seed / pure read — EditSession draft prefer, else Document.
// liveDraft null or id mismatch → Document product-read only. No Host projection.
inline bool ScreenshotAnnotationDocumentResolveLiveAnn(
    const AnnotationDocument& document,
    const std::wstring& id,
    const ScreenshotAnnotation* liveDraft,
    ScreenshotAnnotation& out)
{
    if (!id.empty() && liveDraft && !liveDraft->id.empty() && liveDraft->id == id) {
        out = *liveDraft;
        return true;
    }
    return ScreenshotAnnotationDocumentTryLegacyById(document, id, out);
}

// S-E-45 / S-E-EXIT E1: Document-order iterate — ephemeral Host-shaped view from Document.
// Order authority: Document forEach. Full item via LegacyAnnotationFromSnapshot.
// S-E-CLOSE-9: mid-edit overlay is EditSession liveDraft only.
// S-E-CLOSE-11: empty Document → empty ordered.
// S-E-EXIT E1: no hostAnns param (Document + optional liveDraft sole).
// liveDragId + liveDraft: full draft replaces Document projection for that id.
inline std::vector<ScreenshotAnnotation>
ScreenshotAnnotationDocumentProjectOrdered(
    const AnnotationDocument& document,
    const std::wstring& liveDragId,
    const ScreenshotAnnotation* liveDraft)
{
    if (document.empty()) {
        return {};
    }
    std::vector<ScreenshotAnnotation> out;
    out.reserve(static_cast<size_t>(document.count()));
    document.forEach([&](const ScreenshotAnnotationItem& item) {
        ScreenshotAnnotation ann = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
        // S-E-CLOSE-2/3/9 / S-E-EXIT E1: EditSession draft sole mid-edit overlay for live id.
        if (!liveDragId.empty() && ann.id == liveDragId &&
            liveDraft && !liveDraft->id.empty() && liveDraft->id == liveDragId) {
            ann = *liveDraft;
        }
        out.push_back(std::move(ann));
    });
    return out;
}

inline void ScreenshotAnnotationDocumentClear(
    AnnotationDocument& document)
{
    document.clear();
}
