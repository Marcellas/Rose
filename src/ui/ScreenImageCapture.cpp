#include "ui/ScreenImageCapture.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace rose::ui
{
    namespace
    {
        [[nodiscard]] std::filesystem::path temporaryImagePath(const char* extension)
        {
            const auto directory = std::filesystem::temp_directory_path()
                / "Rose" / "image-input";
            std::filesystem::create_directories(directory);
            const auto instant = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            return directory / ("rose-image-" + std::to_string(instant) + extension);
        }

#ifdef _WIN32
        [[nodiscard]] std::filesystem::path saveBitmap(HBITMAP bitmap)
        {
            BITMAP details{};
            if (GetObjectW(bitmap, sizeof(details), &details) != sizeof(details)
                || details.bmWidth <= 0 || details.bmHeight == 0)
            {
                throw std::runtime_error{ "Clipboard bitmap has invalid dimensions." };
            }

            const std::uint64_t width = static_cast<std::uint64_t>(details.bmWidth);
            const std::uint64_t height = static_cast<std::uint64_t>(
                details.bmHeight < 0 ? -static_cast<std::int64_t>(details.bmHeight)
                                     : details.bmHeight);
            const std::uint64_t bytes = width * height * 4u;
            if (width > 16384u || height > 16384u || bytes > 60u * 1024u * 1024u)
            {
                throw std::runtime_error{ "Screenshot exceeds Rose's 60 MiB image input limit." };
            }

            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = details.bmWidth;
            info.bmiHeader.biHeight = static_cast<LONG>(height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(bytes));
            HDC screen = GetDC(nullptr);
            if (!screen) throw std::runtime_error{ "Could not access display for image conversion." };
            const int rows = GetDIBits(screen, bitmap, 0, static_cast<UINT>(height),
                pixels.data(), &info, DIB_RGB_COLORS);
            ReleaseDC(nullptr, screen);
            if (rows != static_cast<int>(height))
                throw std::runtime_error{ "Could not convert clipboard image to BMP." };

            BITMAPFILEHEADER header{};
            header.bfType = 0x4D42;
            header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            header.bfSize = static_cast<DWORD>(header.bfOffBits + bytes);

            const auto path = temporaryImagePath(".bmp");
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(&header), sizeof(header));
            out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
            out.write(reinterpret_cast<const char*>(pixels.data()),
                static_cast<std::streamsize>(pixels.size()));
            if (!out) throw std::runtime_error{ "Could not write temporary screenshot." };
            return path;
        }
#endif
    }

    bool hasClipboardImage()
    {
        if (SDL_HasClipboardData("image/png")) return true;
#ifdef _WIN32
        return IsClipboardFormatAvailable(CF_BITMAP) != 0;
#else
        return false;
#endif
    }

    std::optional<std::filesystem::path> saveClipboardImage()
    {
        if (SDL_HasClipboardData("image/png"))
        {
            std::size_t size{ 0 };
            void* data = SDL_GetClipboardData("image/png", &size);
            if (data && size > 0 && size <= 60u * 1024u * 1024u)
            {
                const auto path = temporaryImagePath(".png");
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out.write(static_cast<const char*>(data),
                    static_cast<std::streamsize>(size));
                SDL_free(data);
                if (!out) throw std::runtime_error{ "Could not save pasted PNG." };
                return path;
            }
            SDL_free(data);
        }

#ifdef _WIN32
        if (!IsClipboardFormatAvailable(CF_BITMAP) || !OpenClipboard(nullptr))
            return std::nullopt;
        struct CloseClipboardOnExit
        {
            ~CloseClipboardOnExit() { CloseClipboard(); }
        } guard;
        const HBITMAP bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
        if (bitmap) return saveBitmap(bitmap);
#endif
        return std::nullopt;
    }

    std::optional<std::filesystem::path> captureDesktopImage()
    {
#ifdef _WIN32
        const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384
            || static_cast<std::uint64_t>(width) * height * 4u > 60u * 1024u * 1024u)
            throw std::runtime_error{ "Desktop screenshot exceeds Rose's input limit." };

        HDC screen = GetDC(nullptr);
        if (!screen) throw std::runtime_error{ "Could not access the desktop display." };
        HDC memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = memory ? CreateCompatibleBitmap(screen, width, height) : nullptr;
        if (!memory || !bitmap)
        {
            if (bitmap) DeleteObject(bitmap);
            if (memory) DeleteDC(memory);
            ReleaseDC(nullptr, screen);
            throw std::runtime_error{ "Could not allocate desktop screenshot." };
        }
        HGDIOBJ previous = SelectObject(memory, bitmap);
        const BOOL copied = BitBlt(memory, 0, 0, width, height,
            screen, x, y, SRCCOPY | CAPTUREBLT);
        SelectObject(memory, previous); // GetDIBits requires an unselected bitmap.
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        try
        {
            if (!copied) throw std::runtime_error{ "Could not capture the desktop." };
            auto path = saveBitmap(bitmap);
            DeleteObject(bitmap);
            return path;
        }
        catch (...)
        {
            DeleteObject(bitmap);
            throw;
        }
#else
        return std::nullopt;
#endif
    }
}
