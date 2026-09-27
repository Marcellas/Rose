#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rose::imagegen
{

    // User/developer preference for Rose's local image model.
    //
    // Auto preserves Rose's existing behavior while allowing newly installed
    // checkpoints to participate in a deterministic priority order. Explicit
    // values make model choice deterministic across launches.
    enum class LocalImageModelPreference
    {
        Auto,
        Flux2Klein4B,
        RealVisXLV5,
        JuggernautXL,
        PonyV6,
        StableDiffusion15Fallback
    };


    [[nodiscard]]
    std::string_view toString(
        LocalImageModelPreference preference) noexcept;


    // Accept canonical persisted ids plus a few convenient developer aliases.
    [[nodiscard]]
    std::optional<LocalImageModelPreference>
        parseLocalImageModelPreference(
            std::string_view text);


    // Missing preference files are normal and resolve to Auto. Invalid files also
    // fall back to Auto, but return a warning so startup can make the problem
    // observable instead of silently ignoring a typo.
    [[nodiscard]]
    LocalImageModelPreference loadLocalImageModelPreference(
        const std::filesystem::path& path,
        std::string* warning = nullptr);


    // Persist the canonical model id. Parent directories are created as needed.
    void saveLocalImageModelPreference(
        const std::filesystem::path& path,
        LocalImageModelPreference preference);

} // namespace rose::imagegen
