#include "screenshot/annotation/AnnotationModel.h"

void ScreenshotAnnotationModel::add(std::unique_ptr<ScreenshotAnnotationItem> item) {
    m_items.push_back(std::move(item));
}

// S-E-9: order-preserving insert for Document dual-write cutover.
void ScreenshotAnnotationModel::insertAt(int index, std::unique_ptr<ScreenshotAnnotationItem> item) {
    if (index < 0) index = 0;
    if (index > static_cast<int>(m_items.size())) {
        index = static_cast<int>(m_items.size());
    }
    m_items.insert(m_items.begin() + index, std::move(item));
}

bool ScreenshotAnnotationModel::replaceById(const std::wstring& id, std::unique_ptr<ScreenshotAnnotationItem> item) {
    int idx = findIndex(id);
    if (idx < 0) return false;
    // Preserve active if replacing active item (id may change).
    const bool wasActive = m_hasActive && m_activeId == id;
    m_items[static_cast<size_t>(idx)] = std::move(item);
    if (wasActive) {
        setActiveItem(m_items[static_cast<size_t>(idx)]->id());
    }
    return true;
}

int ScreenshotAnnotationModel::findIndex(const std::wstring& id) const {
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i]->id() == id) return i;
    }
    return -1;
}

ScreenshotAnnotationItem* ScreenshotAnnotationModel::findById(const std::wstring& id) {
    int idx = findIndex(id);
    return idx >= 0 ? m_items[idx].get() : nullptr;
}

const ScreenshotAnnotationItem* ScreenshotAnnotationModel::findById(const std::wstring& id) const {
    int idx = findIndex(id);
    return idx >= 0 ? m_items[idx].get() : nullptr;
}

bool ScreenshotAnnotationModel::removeById(const std::wstring& id) {
    int idx = findIndex(id);
    if (idx < 0) return false;
    if (m_hasActive && m_activeId == id) {
        clearActiveItem();
    }
    m_items.erase(m_items.begin() + idx);
    return true;
}

std::unique_ptr<ScreenshotAnnotationItem> ScreenshotAnnotationModel::takeById(const std::wstring& id) {
    int idx = findIndex(id);
    if (idx < 0) return nullptr;
    if (m_hasActive && m_activeId == id) {
        clearActiveItem();
    }
    auto item = std::move(m_items[idx]);
    m_items.erase(m_items.begin() + idx);
    return item;
}

ScreenshotAnnotationItem* ScreenshotAnnotationModel::activeItem() {
    if (!m_hasActive) return nullptr;
    return findById(m_activeId);
}

const ScreenshotAnnotationItem* ScreenshotAnnotationModel::activeItem() const {
    if (!m_hasActive) return nullptr;
    return findById(m_activeId);
}

void ScreenshotAnnotationModel::setActiveItem(const std::wstring& id) {
    if (findById(id)) {
        m_activeId = id;
        m_hasActive = true;
    } else {
        clearActiveItem();
    }
}

void ScreenshotAnnotationModel::clearActiveItem() {
    m_activeId.clear();
    m_hasActive = false;
}

void ScreenshotAnnotationModel::forEach(const std::function<void(ScreenshotAnnotationItem&)>& fn) {
    for (auto& item : m_items) {
        fn(*item);
    }
}

void ScreenshotAnnotationModel::forEach(const std::function<void(const ScreenshotAnnotationItem&)>& fn) const {
    for (const auto& item : m_items) {
        fn(*item);
    }
}

void ScreenshotAnnotationModel::clear() {
    m_items.clear();
    clearActiveItem();
}
