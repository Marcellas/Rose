#include "tools/EditTextFileTool.h"

#include "artifacts/Artifact.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <optional>
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
            const auto found =
                request.arguments.find(std::string{ name });

            if (found == request.arguments.end() || found->second.empty())
            {
                throw std::invalid_argument{
                    "edit_text_file requires argument '"
                    + std::string{ name }
                    + "'."
                };
            }

            return found->second;
        }

        [[nodiscard]]
        const std::string& requiredArgumentAllowEmpty(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(std::string{ name });

            if (found == request.arguments.end())
            {
                throw std::invalid_argument{
                    "edit_text_file requires argument '"
                    + std::string{ name }
                    + "'."
                };
            }

            return found->second;
        }

        [[nodiscard]]
        std::size_t requiredPositiveInteger(
            const ToolRequest& request,
            const std::string_view name)
        {
            const std::string& raw = requiredArgument(request, name);
            std::size_t value{ 0 };

            const auto [end, error] = std::from_chars(
                raw.data(),
                raw.data() + raw.size(),
                value);

            if (
                error != std::errc{}
                || end != raw.data() + raw.size()
                || value == 0)
            {
                throw std::invalid_argument{
                    "edit_text_file argument '"
                    + std::string{ name }
                    + "' must be a positive integer."
                };
            }

            return value;
        }

        [[nodiscard]]
        std::string optionalArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(std::string{ name });

            return found == request.arguments.end()
                ? std::string{}
                : found->second;
        }

        [[nodiscard]]
        std::string lowerAscii(
            std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });

            return value;
        }

        [[nodiscard]]
        std::string decodeTextEscapes(
            const std::string_view encoded)
        {
            std::string decoded;
            decoded.reserve(encoded.size());

            for (std::size_t index = 0; index < encoded.size(); ++index)
            {
                const char character = encoded[index];
                if (character != '\\' || index + 1 >= encoded.size())
                {
                    decoded.push_back(character);
                    continue;
                }

                const char next = encoded[index + 1];
                switch (next)
                {
                case 'n':
                    decoded.push_back('\n');
                    ++index;
                    break;
                case 'r':
                    decoded.push_back('\r');
                    ++index;
                    break;
                case 't':
                    decoded.push_back('\t');
                    ++index;
                    break;
                case 's':
                    // Rose's single-line control protocol trims argument edges.
                    // \s provides an explicit way to preserve leading/trailing spaces
                    // when exact source preimages or replacements require them.
                    decoded.push_back(' ');
                    ++index;
                    break;
                case '\\':
                    decoded.push_back('\\');
                    ++index;
                    break;
                default:
                    // Unknown escapes are preserved literally so source-code text
                    // such as \q is not silently rewritten by the tool adapter.
                    decoded.push_back('\\');
                    break;
                }
            }

            return decoded;
        }

        [[nodiscard]]
        files::TextFileMutationKind parseOperation(
            const std::string_view raw)
        {
            const std::string operation =
                lowerAscii(std::string{ raw });

            if (operation == "replace_text")
            {
                return files::TextFileMutationKind::ReplaceExactText;
            }
            if (operation == "append_text")
            {
                return files::TextFileMutationKind::AppendText;
            }
            if (operation == "remove_text")
            {
                return files::TextFileMutationKind::RemoveText;
            }
            if (operation == "replace_line_range")
            {
                return files::TextFileMutationKind::ReplaceLineRange;
            }

            throw std::invalid_argument{
                "Unsupported edit_text_file operation."
            };
        }
    }

    EditTextFileTool::EditTextFileTool(
        files::ITextFileMutationService& mutationService)
        : mutationService_{ mutationService }
        , descriptor_{
            .id = "edit_text_file",
            .displayName = "Edit Text File",
            .description =
                "Apply one explicit bounded UTF-8 mutation to an existing text/source file. "
                "Operations: replace_text, append_text, remove_text, replace_line_range. "
                "replace/remove require one unique exact find_text match. replace_line_range "
                "requires one-based start_line/line_count plus exact expected_text or a Rose-owned expected_digest preimage. "
                "Rose writes and flushes a sibling temporary "
                "file before transactionally replacing the original. The tool never executes "
                "the edited file.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description = "Absolute path of the existing UTF-8 text/source file.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "operation",
                    .description = "One of replace_text, append_text, remove_text, replace_line_range.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "find_text",
                    .description =
                        "Unique exact text to replace or remove. Encode newlines as literal \\n sequences.",
                    .type = ToolValueType::String,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "replacement_text",
                    .description =
                        "Replacement for replace_text or replace_line_range. May be empty. "
                        "Encode newlines as literal \\n and edge spaces as \\s when exact whitespace matters.",
                    .type = ToolValueType::String,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "text",
                    .description =
                        "Text appended by append_text. Encode newlines as literal \\n sequences.",
                    .type = ToolValueType::String,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "start_line",
                    .description =
                        "One-based first source line for replace_line_range.",
                    .type = ToolValueType::Integer,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "line_count",
                    .description =
                        "Number of contiguous source lines for replace_line_range (maximum 200).",
                    .type = ToolValueType::Integer,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "expected_text",
                    .description =
                        "Exact observed logical text for the selected lines before mutation. Use literal \\n for line separators and \\s for edge spaces; CRLF is normalized for preimage comparison.",
                    .type = ToolValueType::String,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "expected_digest",
                    .description =
                        "Optional Rose-owned SHA-256 provenance for the exact selected source window. "
                        "Use instead of expected_text when Rose just read the same line range.",
                    .type = ToolValueType::String,
                    .required = false
                }
            }
        }
    {
    }

    const ToolDescriptor& EditTextFileTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult EditTextFileTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "EditTextFileTool received a request for a different tool id."
            };
        }

        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (
                name != "path"
                && name != "operation"
                && name != "find_text"
                && name != "replacement_text"
                && name != "text"
                && name != "start_line"
                && name != "line_count"
                && name != "expected_text"
                && name != "expected_digest")
            {
                throw std::invalid_argument{
                    "edit_text_file does not accept argument '"
                    + name
                    + "'."
                };
            }
        }

        const std::filesystem::path path{
            requiredArgument(request, "path")
        };

        if (!path.is_absolute())
        {
            throw std::invalid_argument{
                "edit_text_file requires an absolute file path."
            };
        }

        const files::TextFileMutationKind operation =
            parseOperation(requiredArgument(request, "operation"));

        files::EditTextFileRequest editRequest{
            .path = path,
            .kind = operation,
            .findText = {},
            .replacementText = {},
            .text = {},
            .startLine = 0,
            .lineCount = 0,
            .expectedText = {},
            .expectedDigest = {}
        };

        switch (operation)
        {
        case files::TextFileMutationKind::ReplaceExactText:
            editRequest.findText = decodeTextEscapes(
                requiredArgument(request, "find_text"));
            editRequest.replacementText = decodeTextEscapes(
                optionalArgument(request, "replacement_text"));
            break;

        case files::TextFileMutationKind::AppendText:
            editRequest.text = decodeTextEscapes(
                requiredArgument(request, "text"));
            break;

        case files::TextFileMutationKind::RemoveText:
            editRequest.findText = decodeTextEscapes(
                requiredArgument(request, "find_text"));
            break;

        case files::TextFileMutationKind::ReplaceLineRange:
        {
            editRequest.startLine = requiredPositiveInteger(request, "start_line");
            editRequest.lineCount = requiredPositiveInteger(request, "line_count");

            const bool hasExpectedText =
                request.arguments.contains("expected_text");
            const bool hasExpectedDigest =
                request.arguments.contains("expected_digest");

            if (hasExpectedText == hasExpectedDigest)
            {
                throw std::invalid_argument{
                    "replace_line_range requires exactly one of expected_text or expected_digest."
                };
            }

            if (hasExpectedText)
            {
                editRequest.expectedText = decodeTextEscapes(
                    requiredArgumentAllowEmpty(request, "expected_text"));
            }
            else
            {
                editRequest.expectedDigest =
                    requiredArgument(request, "expected_digest");
            }

            editRequest.replacementText = decodeTextEscapes(
                requiredArgumentAllowEmpty(request, "replacement_text"));
            break;
        }
        }

        const files::TextFileMutationResult mutation =
            mutationService_.edit(editRequest);

        ToolResult result{
            .success = true,
            .message =
                "Edited text/source file:\npath="
                + path.lexically_normal().string()
                + "\noperation="
                + mutation.operation
                + "\naffected_count="
                + std::to_string(mutation.affectedCount)
                + "\nProject Knowledge may need /knowledge index to refresh persisted source excerpts.",
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };

        result.artifacts.push_back(
            artifacts::Artifact{
                .path = path.lexically_normal(),
                .displayName = path.filename().string(),
                .mediaType = "text/plain; charset=utf-8",
                .kind = artifacts::ArtifactKind::Document,
                .metadataPath = std::nullopt
            });

        return result;
    }
}
