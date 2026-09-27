#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

#include "tools/RecyclePathTool.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]]
        const std::string& requiredArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });

            if (
                found == request.arguments.end()
                || found->second.empty())
            {
                throw std::invalid_argument{
                    "Tool '"
                    + request.toolId
                    + "' requires argument '"
                    + std::string{ name }
                    + "'."
                };
            }

            return found->second;
        }


        void rejectUnknownArguments(
            const ToolRequest& request)
        {
            for (const auto& [name, value] : request.arguments)
            {
                (void)value;

                if (name != "path")
                {
                    throw std::invalid_argument{
                        "Tool 'recycle_path' does not accept argument '"
                        + name
                        + "'."
                    };
                }
            }
        }


        [[nodiscard]]
        bool isFilesystemRoot(
            const std::filesystem::path& path)
        {
            const std::filesystem::path normalized =
                path.lexically_normal();

            return !normalized.root_path().empty()
                && normalized == normalized.root_path();
        }
    } // namespace


    RecyclePathTool::RecyclePathTool()
        : descriptor_{
            .id = "recycle_path",
            .displayName = "Move Path to Recycle Bin",
            .description =
                "Move one existing file or directory to the Windows Recycle Bin. "
                "This never intentionally performs a permanent delete and always "
                "requires explicit confirmation of the exact path.",
            .risk = ToolRisk::Destructive,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description =
                        "Absolute path of the existing file or directory to recycle.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
    }


    const ToolDescriptor& RecyclePathTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult RecyclePathTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "RecyclePathTool received a request for a different tool."
            };
        }

        rejectUnknownArguments(request);

        std::filesystem::path path{
            requiredArgument(
                request,
                "path")
        };

        if (!path.is_absolute())
        {
            throw std::invalid_argument{
                "recycle_path requires an absolute path."
            };
        }

        path = path.lexically_normal();

        if (
            path.empty()
            || isFilesystemRoot(path))
        {
            throw std::invalid_argument{
                "recycle_path will not operate on a filesystem root."
            };
        }

        std::error_code error;

        if (!std::filesystem::exists(path, error) || error)
        {
            throw std::runtime_error{
                "recycle_path target does not exist or cannot be inspected: "
                + path.string()
            };
        }

#ifdef _WIN32
        // SHFileOperation requires a double-NUL terminated source list.
        std::wstring sourceList = path.wstring();
        sourceList.push_back(L'\0');
        sourceList.push_back(L'\0');

        SHFILEOPSTRUCTW operation{};
        operation.wFunc = FO_DELETE;
        operation.pFrom = sourceList.c_str();
        operation.fFlags = static_cast<FILEOP_FLAGS>(
            FOF_ALLOWUNDO
            | FOF_NOCONFIRMATION
            | FOF_SILENT
            | FOF_NOERRORUI);

        const int result =
            SHFileOperationW(&operation);

        if (result != 0)
        {
            throw std::runtime_error{
                "Windows could not move the path to the Recycle Bin. SHFileOperation result="
                + std::to_string(result)
            };
        }

        if (operation.fAnyOperationsAborted != FALSE)
        {
            throw std::runtime_error{
                "The recycle operation was aborted before completion."
            };
        }

        return ToolResult{
            .success = true,
            .message =
                "Moved path to the Windows Recycle Bin: "
                + path.string(),
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
#else
        throw std::runtime_error{
            "recycle_path currently supports Windows only."
        };
#endif
    }

} // namespace rose::tools
