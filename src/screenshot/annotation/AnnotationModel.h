#pragma once

#include "screenshot/annotation/AnnotationItem.h"
#include <vector>
#include <memory>
#include <functional>

class ScreenshotAnnotationModel {
public:
    ScreenshotAnnotationModel() = default;

    // Count
    int count() const { return static_cast<int>(m_items.size()); }
    bool empty() const { return m_items.empty(); }

    // CRUD
    void add(std::unique_ptr<ScreenshotAnnotationItem> item);
    // S-E-9: order-preserving insert/replace for Document dual-write cutover.
    void insertAt(int index, std::unique_ptr<ScreenshotAnnotationItem> item);
    bool replaceById(const std::wstring& id, std::unique_ptr<ScreenshotAnnotationItem> item);
    bool removeById(const std::wstring& id);
    ScreenshotAnnotationItem* findById(const std::wstring& id);
    const ScreenshotAnnotationItem* findById(const std::wstring& id) const;

    // Ownership transfer (for undo/redo re-insertion)
    std::unique_ptr<ScreenshotAnnotationItem> takeById(const std::wstring& id);

    // Active item
    ScreenshotAnnotationItem* activeItem();
    const ScreenshotAnnotationItem* activeItem() const;
    void setActiveItem(const std::wstring& id);
    void clearActiveItem();

    // Iteration
    void forEach(const std::function<void(ScreenshotAnnotationItem&)>& fn);
    void forEach(const std::function<void(const ScreenshotAnnotationItem&)>& fn) const;

    // Clear all
    void clear();

private:
    std::vector<std::unique_ptr<ScreenshotAnnotationItem>> m_items;
    std::wstring m_activeId;
    bool m_hasActive = false;

    int findIndex(const std::wstring& id) const;
};

// S-E-7: Document ownership seam alias (research §11.5).
// Host dual-writes legacy vector → Document; Document sole authority later.
using AnnotationDocument = ScreenshotAnnotationModel;
