#pragma once

#include "tools/AnalyzeDirectoryDocumentsTool.h"
#include "tools/ITool.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace rose::model
{
    class IModelProvider;
}

namespace rose::logging
{
    class Logger;
}

namespace rose::ocr
{
    class IOcrEngine;
}

namespace rose::tools
{

    struct PlanDirectoryDocumentRenamesToolConfig
    {
        std::size_t maximumDocuments{ 1024 };
        std::size_t maximumInstructionBytes{ 4096 };
        std::size_t maximumBasenameBytes{ 180 };

        // The analyzer extracts one bounded evidence block large enough for a
        // second-pass classifier retry. The first pass receives only the smaller
        // primary slice so most documents stay fast and inexpensive.
        std::size_t primaryEvidenceBytes{ 1400 };
        std::size_t expandedEvidenceBytes{ 4200 };

        std::int32_t classifierMaxGeneratedTokens{ 192 };
        std::int32_t classifierRetryMaxGeneratedTokens{ 256 };

        // Qwen3 currently supports /no_think for these extraction calls. The small
        // budgets are intentional: a classifier should emit its protocol line
        // instead of consuming hundreds of tokens on hidden reasoning.
        // Final ambiguity-recovery asks two focused questions only after both
        // combined passes remain unresolved.
        std::int32_t classifierFieldRecoveryMaxGeneratedTokens{ 192 };
    };


    // Builds a compact, persistent rename plan for a whole document directory.
    //
    // Important architectural point: the large document corpus never enters the
    // AgentLoop control context. This tool reads one document at a time using the
    // existing bounded directory analyzer, asks the provider to classify only that
    // one document, and writes the resulting source/destination pair into a
    // Rose-owned plan file. Court-filing rename requests use structured metadata
    // extraction plus deterministic C++ filename formatting; other rename rules
    // retain the generic basename planner. The Agent receives only the compact
    // plan summary.
    class PlanDirectoryDocumentRenamesTool final : public ITool
    {
    public:
        PlanDirectoryDocumentRenamesTool(
            model::IModelProvider& modelProvider,
            logging::Logger& logger,
            std::unique_ptr<ocr::IOcrEngine> ocrEngine,
            std::filesystem::path planDirectory,
            PlanDirectoryDocumentRenamesToolConfig config = {});

        ~PlanDirectoryDocumentRenamesTool() override;

        PlanDirectoryDocumentRenamesTool(
            const PlanDirectoryDocumentRenamesTool&) = delete;

        PlanDirectoryDocumentRenamesTool& operator=(
            const PlanDirectoryDocumentRenamesTool&) = delete;

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        model::IModelProvider& modelProvider_;
        logging::Logger& logger_;
        AnalyzeDirectoryDocumentsTool analyzer_;
        std::filesystem::path planDirectory_;
        PlanDirectoryDocumentRenamesToolConfig config_;
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
