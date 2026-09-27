#include "imagegen/LocalImageModelPreference.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace rose::imagegen
{
    namespace
    {
        [[nodiscard]]
        std::string trimAndLower(
            std::string text)
        {
            const auto first =
                std::find_if_not(
                    text.begin(),
                    text.end(),
                    [](const unsigned char value)
                    {
                        return std::isspace(value) != 0;
                    });

            const auto last =
                std::find_if_not(
                    text.rbegin(),
                    text.rend(),
                    [](const unsigned char value)
                    {
                        return std::isspace(value) != 0;
                    }).base();

            if (first >= last)
            {
                return {};
            }

            std::string result{
                first,
                last
            };

            std::transform(
                result.begin(),
                result.end(),
                result.begin(),
                [](const unsigned char value)
                {
                    return static_cast<char>(
                        std::tolower(value));
                });

            return result;
        }
    }


    std::string_view toString(
        const LocalImageModelPreference preference) noexcept
    {
        switch (preference)
        {
        case LocalImageModelPreference::Auto:
            return "auto";

        case LocalImageModelPreference::Flux2Klein4B:
            return "flux2-klein-4b";

        case LocalImageModelPreference::RealVisXLV5:
            return "realvisxl-v5";

        case LocalImageModelPreference::JuggernautXL:
            return "juggernaut-xl";

        case LocalImageModelPreference::PonyV6:
            return "pony-v6";

        case LocalImageModelPreference::StableDiffusion15Fallback:
            return "sd15-fallback";
        }

        return "auto";
    }


    std::optional<LocalImageModelPreference>
        parseLocalImageModelPreference(
            const std::string_view text)
    {
        const std::string normalized =
            trimAndLower(
                std::string{ text });

        if (normalized == "auto")
        {
            return LocalImageModelPreference::Auto;
        }

        if (
            normalized == "flux2-klein-4b"
            || normalized == "flux"
            || normalized == "klein")
        {
            return LocalImageModelPreference::Flux2Klein4B;
        }

        if (
            normalized == "realvisxl-v5"
            || normalized == "realvisxl"
            || normalized == "realvis")
        {
            return LocalImageModelPreference::RealVisXLV5;
        }

        if (
            normalized == "juggernaut-xl"
            || normalized == "juggernaut"
            || normalized == "jugxl")
        {
            return LocalImageModelPreference::JuggernautXL;
        }

        if (
            normalized == "pony-v6"
            || normalized == "pony-v6-xl"
            || normalized == "ponyxl"
            || normalized == "pony")
        {
            return LocalImageModelPreference::PonyV6;
        }

        if (
            normalized == "sd15-fallback"
            || normalized == "sd15"
            || normalized == "sd1.5")
        {
            return LocalImageModelPreference::StableDiffusion15Fallback;
        }

        return std::nullopt;
    }


    LocalImageModelPreference loadLocalImageModelPreference(
        const std::filesystem::path& path,
        std::string* warning)
    {
        if (warning != nullptr)
        {
            warning->clear();
        }

        std::ifstream file{
            path,
            std::ios::binary
        };

        if (!file)
        {
            return LocalImageModelPreference::Auto;
        }

        std::string value;
        std::getline(
            file,
            value);

        const auto parsed =
            parseLocalImageModelPreference(
                value);

        if (parsed.has_value())
        {
            return *parsed;
        }

        if (warning != nullptr)
        {
            *warning =
                "Ignoring invalid image-model preference '"
                + value
                + "' in "
                + path.string()
                + ". Falling back to auto.";
        }

        return LocalImageModelPreference::Auto;
    }


    void saveLocalImageModelPreference(
        const std::filesystem::path& path,
        const LocalImageModelPreference preference)
    {
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(
                path.parent_path());
        }

        std::ofstream file{
            path,
            std::ios::binary | std::ios::trunc
        };

        if (!file)
        {
            throw std::runtime_error{
                "Could not write image-model preference: "
                + path.string()
            };
        }

        file
            << toString(preference)
            << '\n';

        file.flush();

        if (!file)
        {
            throw std::runtime_error{
                "Could not finish writing image-model preference: "
                + path.string()
            };
        }
    }

} // namespace rose::imagegen
