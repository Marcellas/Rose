#pragma once

#include <filesystem>
#include <optional>

namespace rose::ui
{
    // Capture is invoked only by an explicit paste or screen action on the UI
    // thread. The caller owns and later removes the resulting temporary file.
    [[nodiscard]] std::optional<std::filesystem::path> saveClipboardImage();
    [[nodiscard]] bool hasClipboardImage();
    [[nodiscard]] std::optional<std::filesystem::path> captureDesktopImage();
}
