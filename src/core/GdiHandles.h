#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <utility>

namespace zencrop {

template <typename H>
class ScopedGdiObject {
public:
    constexpr ScopedGdiObject() noexcept : m_handle(nullptr) {}
    explicit ScopedGdiObject(H handle) noexcept : m_handle(handle) {}
    ~ScopedGdiObject() noexcept {
        reset();
    }

    ScopedGdiObject(const ScopedGdiObject&) = delete;
    ScopedGdiObject& operator=(const ScopedGdiObject&) = delete;

    ScopedGdiObject(ScopedGdiObject&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    ScopedGdiObject& operator=(ScopedGdiObject&& other) noexcept {
        if (this != &other) {
            reset(other.m_handle);
            other.m_handle = nullptr;
        }
        return *this;
    }

    void reset(H handle = nullptr) noexcept {
        if (m_handle == handle) return;
        if (m_handle) {
            ::DeleteObject(m_handle);
        }
        m_handle = handle;
    }

    [[nodiscard]] H release() noexcept {
        H tmp = m_handle;
        m_handle = nullptr;
        return tmp;
    }

    [[nodiscard]] H get() const noexcept { return m_handle; }
    operator H() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return m_handle != nullptr; }

private:
    H m_handle = nullptr;
};

using ScopedHBITMAP = ScopedGdiObject<HBITMAP>;
using ScopedHBRUSH = ScopedGdiObject<HBRUSH>;
using ScopedHPEN = ScopedGdiObject<HPEN>;
using ScopedHFONT = ScopedGdiObject<HFONT>;
using ScopedHRGN = ScopedGdiObject<HRGN>;

class ScopedDC {
public:
    constexpr ScopedDC() noexcept : m_hdc(nullptr) {}
    explicit ScopedDC(HDC hdc) noexcept : m_hdc(hdc) {}
    ~ScopedDC() noexcept {
        reset();
    }

    ScopedDC(const ScopedDC&) = delete;
    ScopedDC& operator=(const ScopedDC&) = delete;

    ScopedDC(ScopedDC&& other) noexcept : m_hdc(other.m_hdc) {
        other.m_hdc = nullptr;
    }

    ScopedDC& operator=(ScopedDC&& other) noexcept {
        if (this != &other) {
            reset(other.m_hdc);
            other.m_hdc = nullptr;
        }
        return *this;
    }

    void reset(HDC hdc = nullptr) noexcept {
        if (m_hdc == hdc) return;
        if (m_hdc) {
            ::DeleteDC(m_hdc);
        }
        m_hdc = hdc;
    }

    [[nodiscard]] HDC release() noexcept {
        HDC tmp = m_hdc;
        m_hdc = nullptr;
        return tmp;
    }

    [[nodiscard]] HDC get() const noexcept { return m_hdc; }
    operator HDC() const noexcept { return m_hdc; }
    explicit operator bool() const noexcept { return m_hdc != nullptr; }

private:
    HDC m_hdc = nullptr;
};

class ScopedWindowDC {
public:
    constexpr ScopedWindowDC() noexcept : m_hwnd(nullptr), m_hdc(nullptr) {}
    ScopedWindowDC(HWND hwnd, HDC hdc) noexcept : m_hwnd(hwnd), m_hdc(hdc) {}
    ~ScopedWindowDC() noexcept {
        reset();
    }

    ScopedWindowDC(const ScopedWindowDC&) = delete;
    ScopedWindowDC& operator=(const ScopedWindowDC&) = delete;

    ScopedWindowDC(ScopedWindowDC&& other) noexcept
        : m_hwnd(other.m_hwnd), m_hdc(other.m_hdc) {
        other.m_hwnd = nullptr;
        other.m_hdc = nullptr;
    }

    ScopedWindowDC& operator=(ScopedWindowDC&& other) noexcept {
        if (this != &other) {
            reset(other.m_hwnd, other.m_hdc);
            other.m_hwnd = nullptr;
            other.m_hdc = nullptr;
        }
        return *this;
    }

    void reset(HWND hwnd = nullptr, HDC hdc = nullptr) noexcept {
        if (m_hwnd == hwnd && m_hdc == hdc) return;
        if (m_hdc) {
            ::ReleaseDC(m_hwnd, m_hdc);
        }
        m_hwnd = hwnd;
        m_hdc = hdc;
    }

    [[nodiscard]] HDC release() noexcept {
        HDC tmp = m_hdc;
        m_hdc = nullptr;
        m_hwnd = nullptr;
        return tmp;
    }

    [[nodiscard]] HDC get() const noexcept { return m_hdc; }
    [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd; }
    operator HDC() const noexcept { return m_hdc; }
    explicit operator bool() const noexcept { return m_hdc != nullptr; }

private:
    HWND m_hwnd = nullptr;
    HDC m_hdc = nullptr;
};

class ScopedSelectObject {
public:
    ScopedSelectObject(HDC hdc, HGDIOBJ obj) noexcept
        : m_hdc(hdc), m_old(hdc && obj ? ::SelectObject(hdc, obj) : nullptr) {}
    ~ScopedSelectObject() noexcept {
        if (m_hdc && m_old && m_old != HGDI_ERROR) {
            ::SelectObject(m_hdc, m_old);
        }
    }

    ScopedSelectObject(const ScopedSelectObject&) = delete;
    ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;

    ScopedSelectObject(ScopedSelectObject&& other) noexcept
        : m_hdc(other.m_hdc), m_old(other.m_old) {
        other.m_hdc = nullptr;
        other.m_old = nullptr;
    }

    ScopedSelectObject& operator=(ScopedSelectObject&& other) noexcept {
        if (this != &other) {
            if (m_hdc && m_old && m_old != HGDI_ERROR) {
                ::SelectObject(m_hdc, m_old);
            }
            m_hdc = other.m_hdc;
            m_old = other.m_old;
            other.m_hdc = nullptr;
            other.m_old = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HGDIOBJ old() const noexcept { return m_old; }
    [[nodiscard]] bool valid() const noexcept { return m_old != nullptr && m_old != HGDI_ERROR; }

private:
    HDC m_hdc = nullptr;
    HGDIOBJ m_old = nullptr;
};

class ScopedPaintDC {
public:
    ScopedPaintDC(HWND hwnd, PAINTSTRUCT* ps) noexcept
        : m_hwnd(hwnd), m_ps(ps), m_hdc(hwnd && ps ? ::BeginPaint(hwnd, ps) : nullptr) {}
    ~ScopedPaintDC() noexcept {
        if (m_hwnd && m_ps) {
            ::EndPaint(m_hwnd, m_ps);
        }
    }

    ScopedPaintDC(const ScopedPaintDC&) = delete;
    ScopedPaintDC& operator=(const ScopedPaintDC&) = delete;

    [[nodiscard]] HDC get() const noexcept { return m_hdc; }
    operator HDC() const noexcept { return m_hdc; }
    explicit operator bool() const noexcept { return m_hdc != nullptr; }

private:
    HWND m_hwnd = nullptr;
    PAINTSTRUCT* m_ps = nullptr;
    HDC m_hdc = nullptr;
};

} // namespace zencrop
