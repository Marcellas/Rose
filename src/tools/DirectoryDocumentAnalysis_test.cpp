#include "ocr/IOcrEngine.h"
#include "tools/AnalyzeDirectoryDocumentsTool.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

namespace
{
    class UnusedOcrEngine final : public rose::ocr::IOcrEngine
    {
    public:
        [[nodiscard]]
        bool available() const noexcept override
        {
            return false;
        }

        [[nodiscard]]
        std::string availabilityMessage() const override
        {
            return "OCR intentionally unavailable in text-only unit test.";
        }

        [[nodiscard]]
        rose::ocr::OcrResult recognizeEncodedImage(
            std::span<const std::uint8_t>,
            std::string_view) override
        {
            throw std::runtime_error{ "OCR should not be called in this test." };
        }

        [[nodiscard]]
        rose::ocr::OcrResult recognizeBitmap(
            const rose::ocr::OcrBitmap&) override
        {
            throw std::runtime_error{ "OCR should not be called in this test." };
        }
    };


    void require(
        const bool condition,
        const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }


    std::filesystem::path makeTempRoot()
    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / "rose-directory-document-analysis-test";

        std::error_code error;
        std::filesystem::remove_all(root, error);
        error.clear();
        std::filesystem::create_directories(root / "nested", error);

        if (error)
        {
            throw std::system_error{
                error,
                "Could not create directory-analysis test root"
            };
        }

        return root;
    }
}


int main()
{
    try
    {
        const std::filesystem::path root =
            makeTempRoot();

        {
            std::ofstream stream{ root / "alpha.txt" };
            stream << "Declaration of Petitioner\nFiled September 15, 2026\n";
        }

        {
            std::ofstream stream{ root / "nested" / "beta.md" };
            stream << "# Motion for Continuance\nFiled September 16, 2026\n";
        }

        {
            std::ofstream stream{ root / "ignored.bin", std::ios::binary };
            stream << "not a supported document";
        }

        rose::tools::AnalyzeDirectoryDocumentsTool tool{
            std::make_unique<UnusedOcrEngine>(),
            rose::tools::AnalyzeDirectoryDocumentsToolConfig{
                .maximumFilesPerBatch = 10,
                .maximumObservationBytes = 16u * 1024u,
                .maximumExcerptBytesPerFile = 512,
                .maximumDepth = 4,
                .maximumTextFileBytes = 4096,
                .maximumPdfBytes = 1024u * 1024u
            }
        };

        require(
            tool.descriptor().risk
                == rose::tools::ToolRisk::ReadOnly,
            "analyze_directory_documents must be read-only");

        require(
            tool.descriptor().consent
                == rose::tools::ToolConsent::RequiresConfirmation,
            "analyze_directory_documents must require confirmation");

        const rose::tools::ToolResult result =
            tool.execute(
                rose::tools::ToolRequest{
                    .toolId = "analyze_directory_documents",
                    .arguments = {
                        { "path", root.string() }
                    }
                });

        require(result.success, "directory document analysis should succeed");
        require(
            result.message.find("supported_files_total=2")
                != std::string::npos,
            "only supported text documents should be counted");
        require(
            result.message.find("Declaration of Petitioner")
                != std::string::npos,
            "analysis should include first text document excerpt");
        require(
            result.message.find("Motion for Continuance")
                != std::string::npos,
            "analysis should include nested text document excerpt");
        require(
            result.message.find("ignored.bin")
                == std::string::npos,
            "unsupported binary file should not appear in document analysis");
        require(
            result.message.find("has_more=false")
                != std::string::npos,
            "small directory should fit in one batch");

        std::error_code cleanupError;
        std::filesystem::remove_all(root, cleanupError);

        std::cout << "Rose DirectoryDocumentAnalysis tests: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Rose DirectoryDocumentAnalysis tests: FAIL: "
            << error.what()
            << '\n';
        return 1;
    }
}
