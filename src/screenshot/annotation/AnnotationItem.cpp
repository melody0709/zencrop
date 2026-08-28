#include "screenshot/annotation/AnnotationItem.h"
#include "core/WideStringUtils.h"
#include <atomic>
#include <algorithm>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// --- ID generation ---

static std::atomic<uint64_t> g_idCounter{0};

std::wstring ScreenshotAnnotationItem::generateId() {
    uint64_t id = ++g_idCounter;
    // OWN-127: pure ann id (WideStringUtils).
    return WideFormatAnnIdPlain(id);
}

// --- Sole-map typed accessors (S-D-2) ---

namespace {

static const std::wstring g_emptyString;

// setInt on Double schema coerces (TextFontSize legacy path).
void StoreSetInt(std::unordered_map<AnnotationProperty, AnnotationValue>& props,
    AnnotationProperty p, int v)
{
    if (GetAnnotationPropertyKind(p) == AnnotationValueKind::Double) {
        props[p] = static_cast<double>(v);
        return;
    }
    props[p] = v;
}

int StoreGetInt(const std::unordered_map<AnnotationProperty, AnnotationValue>& props,
    AnnotationProperty p, int def)
{
    auto it = props.find(p);
    if (it == props.end()) return def;
    if (const int* v = std::get_if<int>(&it->second)) return *v;
    return def;
}

bool StoreGetBool(const std::unordered_map<AnnotationProperty, AnnotationValue>& props,
    AnnotationProperty p, bool def)
{
    auto it = props.find(p);
    if (it == props.end()) return def;
    if (const bool* v = std::get_if<bool>(&it->second)) return *v;
    return def;
}

double StoreGetDouble(const std::unordered_map<AnnotationProperty, AnnotationValue>& props,
    AnnotationProperty p, double def)
{
    auto it = props.find(p);
    if (it == props.end()) return def;
    if (const double* v = std::get_if<double>(&it->second)) return *v;
    return def;
}

COLORREF StoreGetColor(const std::unordered_map<AnnotationProperty, AnnotationValue>& props,
    AnnotationProperty p, COLORREF def)
{
    auto it = props.find(p);
    if (it == props.end()) return def;
    if (const COLORREF* v = std::get_if<COLORREF>(&it->second)) return *v;
    return def;
}

const std::wstring& StoreGetString(const std::unordered_map<AnnotationProperty, AnnotationValue>& props,
    AnnotationProperty p)
{
    auto it = props.find(p);
    if (it == props.end()) return g_emptyString;
    if (const std::wstring* v = std::get_if<std::wstring>(&it->second)) return *v;
    return g_emptyString;
}

} // namespace

// --- ScreenshotAnnotationItem ---

ScreenshotAnnotationItem::ScreenshotAnnotationItem(AnnotationType type)
    : m_id(generateId()), m_type(type) {}

bool ScreenshotAnnotationItem::hasProperty(AnnotationProperty p) const {
    return m_props.count(p) > 0;
}

int ScreenshotAnnotationItem::getInt(AnnotationProperty p, int def) const {
    return StoreGetInt(m_props, p, def);
}

bool ScreenshotAnnotationItem::getBool(AnnotationProperty p, bool def) const {
    return StoreGetBool(m_props, p, def);
}

double ScreenshotAnnotationItem::getDouble(AnnotationProperty p, double def) const {
    return StoreGetDouble(m_props, p, def);
}

COLORREF ScreenshotAnnotationItem::getColor(AnnotationProperty p, COLORREF def) const {
    return StoreGetColor(m_props, p, def);
}

const std::wstring& ScreenshotAnnotationItem::getString(AnnotationProperty p) const {
    return StoreGetString(m_props, p);
}

void ScreenshotAnnotationItem::setInt(AnnotationProperty p, int v) {
    StoreSetInt(m_props, p, v);
}

void ScreenshotAnnotationItem::setBool(AnnotationProperty p, bool v) {
    m_props[p] = v;
}

void ScreenshotAnnotationItem::setDouble(AnnotationProperty p, double v) {
    m_props[p] = v;
}

void ScreenshotAnnotationItem::setColor(AnnotationProperty p, COLORREF v) {
    m_props[p] = v;
}

void ScreenshotAnnotationItem::setString(AnnotationProperty p, const std::wstring& v) {
    m_props[p] = v;
}

// Geometry

RECT ScreenshotAnnotationItem::boundingRect() const {
    return {
        std::min(m_start.x, m_end.x),
        std::min(m_start.y, m_end.y),
        std::max(m_start.x, m_end.x),
        std::max(m_start.y, m_end.y),
    };
}

// Snapshot

AnnotationSnapshot ScreenshotAnnotationItem::takeSnapshot() const {
    AnnotationSnapshot snap;
    snap.id = m_id;
    snap.type = m_type;
    snap.role = m_role;
    snap.start = m_start;
    snap.end = m_end;
    snap.text = m_text;
    snap.props = m_props;
    return snap;
}

void ScreenshotAnnotationItem::restoreFromSnapshot(const AnnotationSnapshot& snap) {
    if (!snap.id.empty()) {
        m_id = snap.id;
    }
    m_type = snap.type;
    m_role = snap.role;
    m_start = snap.start;
    m_end = snap.end;
    m_text = snap.text;
    m_props = snap.props;
}

// --- AnnotationSnapshot property accessors ---

int AnnotationSnapshot::getInt(AnnotationProperty p, int def) const {
    return StoreGetInt(props, p, def);
}

bool AnnotationSnapshot::getBool(AnnotationProperty p, bool def) const {
    return StoreGetBool(props, p, def);
}

double AnnotationSnapshot::getDouble(AnnotationProperty p, double def) const {
    return StoreGetDouble(props, p, def);
}

COLORREF AnnotationSnapshot::getColor(AnnotationProperty p, COLORREF def) const {
    return StoreGetColor(props, p, def);
}

const std::wstring& AnnotationSnapshot::getString(AnnotationProperty p) const {
    return StoreGetString(props, p);
}

void AnnotationSnapshot::setInt(AnnotationProperty p, int v) {
    StoreSetInt(props, p, v);
}

void AnnotationSnapshot::setBool(AnnotationProperty p, bool v) {
    props[p] = v;
}

void AnnotationSnapshot::setDouble(AnnotationProperty p, double v) {
    props[p] = v;
}

void AnnotationSnapshot::setColor(AnnotationProperty p, COLORREF v) {
    props[p] = v;
}

void AnnotationSnapshot::setString(AnnotationProperty p, const std::wstring& v) {
    props[p] = v;
}
