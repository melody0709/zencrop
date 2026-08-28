#pragma once

#include "screenshot/annotation/AnnotationTypes.h"
#include "screenshot/annotation/AnnotationProperty.h"
#include "screenshot/annotation/AnnotationValue.h"
#include <string>
#include <unordered_map>
#include <windows.h>

// S-D-2: sole property store is unordered_map<AnnotationProperty, AnnotationValue>.
// Typed get*/set* stay as thin adapters over the sole map.

struct AnnotationSnapshot {
    std::wstring id;
    AnnotationType type = AnnotationType::None;
    AnnotationRole role = AnnotationRole::Default;
    POINT start = {};
    POINT end = {};
    std::wstring text;

    std::unordered_map<AnnotationProperty, AnnotationValue> props;

    int getInt(AnnotationProperty p, int def = 0) const;
    bool getBool(AnnotationProperty p, bool def = false) const;
    double getDouble(AnnotationProperty p, double def = 0.0) const;
    COLORREF getColor(AnnotationProperty p, COLORREF def = 0) const;
    const std::wstring& getString(AnnotationProperty p) const;

    void setInt(AnnotationProperty p, int v);
    void setBool(AnnotationProperty p, bool v);
    void setDouble(AnnotationProperty p, double v);
    void setColor(AnnotationProperty p, COLORREF v);
    void setString(AnnotationProperty p, const std::wstring& v);
};

class ScreenshotAnnotationItem {
public:
    explicit ScreenshotAnnotationItem(AnnotationType type);

    const std::wstring& id() const { return m_id; }
    void setId(const std::wstring& id) { m_id = id; }
    AnnotationType type() const { return m_type; }
    AnnotationRole role() const { return m_role; }
    void setRole(AnnotationRole r) { m_role = r; }

    // Properties (thin adapters over sole AnnotationValue map)
    bool hasProperty(AnnotationProperty p) const;
    int getInt(AnnotationProperty p, int def = 0) const;
    bool getBool(AnnotationProperty p, bool def = false) const;
    double getDouble(AnnotationProperty p, double def = 0.0) const;
    COLORREF getColor(AnnotationProperty p, COLORREF def = 0) const;
    const std::wstring& getString(AnnotationProperty p) const;

    void setInt(AnnotationProperty p, int v);
    void setBool(AnnotationProperty p, bool v);
    void setDouble(AnnotationProperty p, double v);
    void setColor(AnnotationProperty p, COLORREF v);
    void setString(AnnotationProperty p, const std::wstring& v);

    // Geometry
    POINT start() const { return m_start; }
    POINT end() const { return m_end; }
    void setStart(POINT p) { m_start = p; }
    void setEnd(POINT p) { m_end = p; }
    RECT boundingRect() const;

    // Text content (for Text/Watermark/Serial)
    const std::wstring& text() const { return m_text; }
    void setText(const std::wstring& t) { m_text = t; }

    // Snapshot
    AnnotationSnapshot takeSnapshot() const;
    void restoreFromSnapshot(const AnnotationSnapshot& snap);

    static std::wstring generateId();

private:
    std::wstring m_id;
    AnnotationType m_type;
    AnnotationRole m_role = AnnotationRole::Default;
    POINT m_start = {};
    POINT m_end = {};
    std::wstring m_text;

    std::unordered_map<AnnotationProperty, AnnotationValue> m_props;
};
