#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>

namespace kb::editor {

#if defined(_WIN32)

struct ScopedBrush {
    explicit ScopedBrush(COLORREF color)
        : handle(CreateSolidBrush(color)) {}

    ~ScopedBrush() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedBrush(const ScopedBrush&) = delete;
    ScopedBrush& operator=(const ScopedBrush&) = delete;

    HBRUSH handle = nullptr;
};

struct ScopedFont {
    ScopedFont(int pointSize, int weight)
        : handle(CreateFontW(
              -pointSize,
              0,
              0,
              0,
              weight,
              FALSE,
              FALSE,
              FALSE,
              DEFAULT_CHARSET,
              OUT_DEFAULT_PRECIS,
              CLIP_DEFAULT_PRECIS,
              CLEARTYPE_QUALITY,
              DEFAULT_PITCH | FF_DONTCARE,
              L"Segoe UI")) {}

    ~ScopedFont() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;

    HFONT handle = nullptr;
};

struct ScopedPen {
    ScopedPen(int width, COLORREF color)
        : handle(CreatePen(PS_SOLID, width, color)) {}

    ~ScopedPen() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedPen(const ScopedPen&) = delete;
    ScopedPen& operator=(const ScopedPen&) = delete;

    HPEN handle = nullptr;
};

struct ScopedCompatibleDc {
    explicit ScopedCompatibleDc(HDC source)
        : handle(CreateCompatibleDC(source)) {}

    ~ScopedCompatibleDc() {
        if (handle != nullptr) {
            DeleteDC(handle);
        }
    }

    ScopedCompatibleDc(const ScopedCompatibleDc&) = delete;
    ScopedCompatibleDc& operator=(const ScopedCompatibleDc&) = delete;

    HDC handle = nullptr;
};

struct ScopedBitmap {
    ScopedBitmap(HDC source, int width, int height)
        : handle(CreateCompatibleBitmap(source, std::max(1, width), std::max(1, height))) {}

    ~ScopedBitmap() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedBitmap(const ScopedBitmap&) = delete;
    ScopedBitmap& operator=(const ScopedBitmap&) = delete;

    HBITMAP handle = nullptr;
};

#endif

} // namespace kb::editor
