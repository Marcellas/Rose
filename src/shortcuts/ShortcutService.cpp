#include "shortcuts/ShortcutService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#include <shobjidl.h>
#endif

namespace rose::shortcuts
{
    namespace
    {
        [[nodiscard]] std::string lowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        [[nodiscard]] std::string trim(std::string value)
        {
            const auto notSpace = [](const unsigned char c) { return !std::isspace(c); };
            const auto begin = std::find_if(value.begin(), value.end(), notSpace);
            const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
            if (begin >= end) return {};
            return std::string{ begin, end };
        }

        [[nodiscard]] ShortcutInspection inspectUrl(const std::filesystem::path& path)
        {
            std::ifstream file{ path, std::ios::binary };
            if (!file) throw std::runtime_error{ "Could not open Internet Shortcut: " + path.string() };
            std::string contents{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
            if (contents.size() >= 3 && static_cast<unsigned char>(contents[0]) == 0xEF
                && static_cast<unsigned char>(contents[1]) == 0xBB
                && static_cast<unsigned char>(contents[2]) == 0xBF)
                contents.erase(0, 3);

            ShortcutInspection result;
            result.kind = "Internet Shortcut (.url)";
            bool inInternetShortcutSection{ false };
            std::size_t position{};
            while (position <= contents.size())
            {
                const auto newline = contents.find_first_of("\r\n", position);
                std::string line = trim(contents.substr(position, newline == std::string::npos ? std::string::npos : newline - position));
                position = newline == std::string::npos ? contents.size() + 1 : newline + 1;
                if (line.empty() || line[0] == ';' || line[0] == '#') continue;
                if (line.front() == '[' && line.back() == ']')
                {
                    inInternetShortcutSection = lowerAscii(line) == "[internetshortcut]";
                    continue;
                }
                if (!inInternetShortcutSection) continue;
                const auto equals = line.find('=');
                if (equals == std::string::npos) continue;
                const std::string key = lowerAscii(trim(line.substr(0, equals)));
                const std::string value = trim(line.substr(equals + 1));
                if (key == "url") result.url = value;
                else if (key == "iconfile") result.iconLocation = value;
            }
            if (result.url.empty())
                throw std::runtime_error{ "Internet Shortcut contains no URL= entry: " + path.string() };
            result.target = result.url;
            return result;
        }

#ifdef _WIN32
        [[nodiscard]] std::string utf8FromWide(const std::wstring_view text)
        {
            if (text.empty()) return {};
            const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (required <= 0) return {};
            std::string result(static_cast<std::size_t>(required), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), required, nullptr, nullptr);
            return result;
        }

        class ComApartment final
        {
        public:
            ComApartment()
            {
                hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                initialized_ = SUCCEEDED(hr_);
                if (hr_ == RPC_E_CHANGED_MODE) initialized_ = false;
                else if (FAILED(hr_)) throw std::runtime_error{ "Could not initialize COM for Shell Link inspection." };
            }
            ~ComApartment() { if (initialized_) CoUninitialize(); }
        private:
            HRESULT hr_{ E_FAIL };
            bool initialized_{ false };
        };

        template<typename T>
        class ComPtr final
        {
        public:
            ~ComPtr() { reset(); }
            T** put() { reset(); return &value_; }
            T* get() const noexcept { return value_; }
            T* operator->() const noexcept { return value_; }
            void reset() noexcept { if (value_) { value_->Release(); value_ = nullptr; } }
        private:
            T* value_{ nullptr };
        };

        [[nodiscard]] ShortcutInspection inspectLnk(const std::filesystem::path& path)
        {
            ComApartment apartment;
            ComPtr<IShellLinkW> link;
            HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                reinterpret_cast<void**>(link.put()));
            if (FAILED(hr)) throw std::runtime_error{ "Could not create Windows Shell Link reader." };

            ComPtr<IPersistFile> persist;
            hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(persist.put()));
            if (FAILED(hr)) throw std::runtime_error{ "Could not access Shell Link persistence interface." };
            hr = persist->Load(path.c_str(), STGM_READ);
            if (FAILED(hr)) throw std::runtime_error{ "Could not load Windows shortcut: " + path.string() };

            ShortcutInspection result;
            result.kind = "Windows Shell Link (.lnk)";
            std::array<wchar_t, 32768> buffer{};
            WIN32_FIND_DATAW findData{};
            if (SUCCEEDED(link->GetPath(buffer.data(), static_cast<int>(buffer.size()), &findData, SLGP_RAWPATH)))
            {
                const std::filesystem::path targetPath{ buffer.data() };
                result.target = utf8FromWide(buffer.data());
                std::error_code targetError;
                result.targetExists = std::filesystem::exists(targetPath, targetError) && !targetError;
            }
            buffer.fill(L'\0');
            if (SUCCEEDED(link->GetArguments(buffer.data(), static_cast<int>(buffer.size()))))
                result.arguments = utf8FromWide(buffer.data());
            buffer.fill(L'\0');
            if (SUCCEEDED(link->GetWorkingDirectory(buffer.data(), static_cast<int>(buffer.size()))))
                result.workingDirectory = utf8FromWide(buffer.data());
            buffer.fill(L'\0');
            if (SUCCEEDED(link->GetDescription(buffer.data(), static_cast<int>(buffer.size()))))
                result.description = utf8FromWide(buffer.data());
            buffer.fill(L'\0');
            int iconIndex{};
            if (SUCCEEDED(link->GetIconLocation(buffer.data(), static_cast<int>(buffer.size()), &iconIndex)))
            {
                result.iconLocation = utf8FromWide(buffer.data());
                if (!result.iconLocation.empty()) result.iconLocation += "," + std::to_string(iconIndex);
            }
            return result;
        }
#endif
    }

    ShortcutInspection LocalShortcutService::inspect(const std::filesystem::path& path) const
    {
        if (!path.is_absolute()) throw std::invalid_argument{ "Shortcut inspection requires an absolute file path." };
        const std::string extension = lowerAscii(path.extension().string());
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
            throw std::runtime_error{ "Shortcut does not exist or is not readable: " + path.string() };
        if (extension == ".url") return inspectUrl(path);
        if (extension == ".lnk")
        {
#ifdef _WIN32
            return inspectLnk(path);
#else
            throw std::runtime_error{ "Windows .lnk inspection is available only on Windows." };
#endif
        }
        throw std::invalid_argument{ "Shortcut inspection supports .lnk and .url files only." };
    }
}
