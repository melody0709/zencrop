#pragma once

#include <windows.h>
#include <map>
#include <utility>

// ScreenshotFontCache
//
// Caches HFONT handles keyed by (devicePixelHeight, weight) for the screenshot
// toolbar and hover magnifier rendering hot paths. Previously each DrawText
// call created and destroyed a font, costing 1200+ kernel transitions per
// second at 60fps. The cache is keyed by the actual device-pixel height
// (already DPI-scaled by the caller) plus weight, so DPI changes naturally
// produce new entries without invalidating old ones.
//
// Lifetime: the cache owns the HFONT handles and deletes them in its
// destructor. Callers should keep one instance per rendering scope (e.g. one
// per DrawScreenshotToolbar invocation, or one per HoverMagnifierWidget
// instance) and let it go out of scope to release the fonts.
class ScreenshotFontCache {
public:
    ScreenshotFontCache() = default;

    // Non-copyable: holds owned HFONT handles.
    ScreenshotFontCache(const ScreenshotFontCache&) = delete;
    ScreenshotFontCache& operator=(const ScreenshotFontCache&) = delete;

    ~ScreenshotFontCache() {
        Clear();
    }

    // Release all cached fonts. Safe to call any time; subsequent Get calls
    // will recreate fonts on demand.
    void Clear() {
        for (auto& kv : m_cache) {
            if (kv.second) DeleteObject(kv.second);
        }
        m_cache.clear();
    }

    // Return a cached HFONT for the given (devicePixelHeight, weight), or
    // create one on miss. Returns nullptr on creation failure (cached as
    // nullptr so we don't retry every frame).
    //
    // devicePixelHeight: negative value passed to CreateFontW (e.g. -S(14)).
    //                    Pass the already-scaled value, NOT the logical 96-DPI
    //                    value. This keeps DPI changes naturally segregated.
    // weight: FW_NORMAL, FW_BOLD, etc.
    HFONT Get(int devicePixelHeight, int weight) {
        Key key{ devicePixelHeight, weight };
        auto it = m_cache.find(key);
        if (it != m_cache.end()) return it->second;

        HFONT font = CreateFontW(devicePixelHeight, 0, 0, 0, weight,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        m_cache.emplace(key, font);
        return font;
    }

private:
    struct Key {
        int height;
        int weight;
        bool operator<(const Key& other) const {
            if (height != other.height) return height < other.height;
            return weight < other.weight;
        }
    };

    std::map<Key, HFONT> m_cache;
};
