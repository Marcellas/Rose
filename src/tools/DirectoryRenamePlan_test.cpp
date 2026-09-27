#include "model/IModelProvider.h"
#include "logging/Logger.h"
#include "ocr/IOcrEngine.h"
#include "tools/ApplyRenamePlanTool.h"
#include "tools/CourtFilingMetadataExtractor.h"
#include "tools/PlanDirectoryDocumentRenamesTool.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    class FakeOcrEngine final : public rose::ocr::IOcrEngine
    {
    public:
        [[nodiscard]]
        bool available() const noexcept override
        {
            return true;
        }

        [[nodiscard]]
        std::string availabilityMessage() const override
        {
            return "fake OCR available";
        }

        [[nodiscard]]
        rose::ocr::OcrResult recognizeEncodedImage(
            std::span<const std::uint8_t>,
            std::string_view) override
        {
            return {};
        }

        [[nodiscard]]
        rose::ocr::OcrResult recognizeBitmap(
            const rose::ocr::OcrBitmap&) override
        {
            return {};
        }
    };


    class FakeModelProvider final : public rose::model::IModelProvider
    {
    public:
        [[nodiscard]]
        rose::model::ModelResponse generate(
            const rose::model::ModelRequest& request) override
        {
            assert(!request.messages.empty());

            // LlamaCppModelProvider rejects zero temperature. Keep the classifier
            // effectively deterministic with a tiny positive value and topK=1.
            assert(request.sampling.temperature > 0.0f);
            assert(request.sampling.topK == 1);

            const std::string& system = request.messages.front().content;
            const std::string& text = request.messages.back().content;
            assert(text.find("/no_think") != std::string::npos);

            if (system.find("extract ONLY the court/clerk filing stamp date and time") != std::string::npos)
            {
                if (text.find("04/01/2022") != std::string::npos)
                {
                    return rose::model::ModelResponse{
                        .text = "OK|DATE=2022-04-01|TIME=UNKNOWN|CONFIDENCE=high\n",
                        .reasoning = {},
                        .generatedTokens = 0,
                        .finishReason = rose::model::ModelFinishReason::EndOfGeneration
                    };
                }

                return rose::model::ModelResponse{
                    .text = "AMBIGUOUS|MISSING=date|REASON=no explicit filing stamp date\n",
                    .reasoning = {},
                    .generatedTokens = 0,
                    .finishReason = rose::model::ModelFinishReason::EndOfGeneration
                };
            }

            if (system.find("extract ONLY the document filing TYPE") != std::string::npos)
            {
                if (text.find("NOTICE OF DISQUALIFICATION") != std::string::npos)
                {
                    return rose::model::ModelResponse{
                        .text = "OK|TYPE=NOTICE OF DISQUALIFICATION|CONFIDENCE=high\n",
                        .reasoning = {},
                        .generatedTokens = 0,
                        .finishReason = rose::model::ModelFinishReason::EndOfGeneration
                    };
                }

                return rose::model::ModelResponse{
                    .text = "AMBIGUOUS|MISSING=type|REASON=document title is not supported\n",
                    .reasoning = {},
                    .generatedTokens = 0,
                    .finishReason = rose::model::ModelFinishReason::EndOfGeneration
                };
            }

            if (text.find("NOTICE OF DISQUALIFICATION") != std::string::npos)
            {
                return rose::model::ModelResponse{
                    .text = "AMBIGUOUS|MISSING=date,type|REASON=combined pass remained uncertain\n",
                    .reasoning = {},
                    .generatedTokens = 0,
                    .finishReason = rose::model::ModelFinishReason::EndOfGeneration
                };
            }

            if (text.find("Declaration of Petitioner") != std::string::npos)
            {
                // Deliberately use source-style punctuation/underscores to prove
                // deterministic C++ normalization owns the final filename format.
                return rose::model::ModelResponse{
                    .text =
                        "OK|DATE=09/15/2026|TIME=21:02|TYPE=Declaration_of_Petitioner|CONFIDENCE=high\n",
                    .reasoning = {},
                    .generatedTokens = 0,
                    .finishReason = rose::model::ModelFinishReason::EndOfGeneration
                };
            }

            if (text.find("Motion for Continuance") != std::string::npos)
            {
                if (text.find("evidence_stage=expanded-retry") == std::string::npos)
                {
                    return rose::model::ModelResponse{
                        .text = "AMBIGUOUS|MISSING=type|REASON=need the expanded evidence window\n",
                        .reasoning = {},
                        .generatedTokens = 0,
                        .finishReason = rose::model::ModelFinishReason::EndOfGeneration
                    };
                }

                return rose::model::ModelResponse{
                    .text =
                        "OK|DATE=2026-09-16|TIME=0931|TYPE=Motion for Continuance|CONFIDENCE=medium\n",
                    .reasoning = {},
                    .generatedTokens = 0,
                    .finishReason = rose::model::ModelFinishReason::EndOfGeneration
                };
            }

            if (text.find("Declaration of Service") != std::string::npos)
            {
                return rose::model::ModelResponse{
                    .text =
                        "OK|DATE=2020-09-08|TIME=UNKNOWN|TYPE=Declaration of Service|CONFIDENCE=high\n",
                    .reasoning = {},
                    .generatedTokens = 0,
                    .finishReason = rose::model::ModelFinishReason::EndOfGeneration
                };
            }

            return rose::model::ModelResponse{
                .text =
                    "AMBIGUOUS|MISSING=date,type|REASON=filing date or type is not supported by the evidence\n",
                .reasoning = {},
                .generatedTokens = 0,
                .finishReason = rose::model::ModelFinishReason::EndOfGeneration
            };
        }

        [[nodiscard]]
        rose::model::ModelContextUsage inspectContext(
            const rose::model::ModelRequest& request) const override
        {
            return rose::model::ModelContextUsage{
                .promptTokens = static_cast<std::int32_t>(request.messages.size() * 100),
                .contextCapacity = 8192,
                .requestedGenerationTokens = request.maxGeneratedTokens
            };
        }
    };


    void writeText(
        const std::filesystem::path& path,
        const std::string& text)
    {
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        assert(output);
        output << text;
        assert(output.good());
    }


    [[nodiscard]]
    std::string readAll(
        const std::filesystem::path& path)
    {
        std::ifstream input{ path, std::ios::binary };
        assert(input);
        std::ostringstream output;
        output << input.rdbuf();
        return output.str();
    }
}


int main()
{
    namespace fs = std::filesystem;

    // Batch 18: canonical title boundaries must reject prose and strip the
    // footer/party/law-firm contamination seen in the real 34-file run.
    {
        using rose::tools::canonicalizeCourtFilingType;

        const auto declaration = canonicalizeCourtFilingType(
            "Declaration of Respondent Truitt & Lyons");
        assert(declaration.ok);
        assert(declaration.filingType == "Declaration of Respondent");

        const auto footer = canonicalizeCourtFilingType(
            "NOTICE OF APPEARANCE - Page 1 of 1 Truitt &Lyons");
        assert(footer.ok);
        assert(footer.filingType == "Notice of Appearance");

        const auto caption = canonicalizeCourtFilingType(
            "CHRISTOPHER WAYNE MONKS, NOTICE OF DISQUALIFICATION");
        assert(caption.ok);
        assert(caption.filingType == "Notice of Disqualification");

        const auto prose = canonicalizeCourtFilingType(
            "order because it is not a final judgment, normally a court will not modify a temporary order absent");
        assert(!prose.ok);

        const auto submitted = canonicalizeCourtFilingType(
            "Submitted by Christopher Wayne Monks (Attach this to the Petition)");
        assert(!submitted.ok);

        const auto contaminated = canonicalizeCourtFilingType(
            "Declaration of Attorney and other papers and pleadings filed in support of this");
        assert(contaminated.ok);
        assert(contaminated.filingType == "Declaration of Attorney");
    }

    const fs::path root =
        fs::temp_directory_path()
        / "rose-directory-rename-plan-test";

    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "docs");
    fs::create_directories(root / "plans");

    const fs::path first = root / "docs" / "hash-a.txt";
    const fs::path second = root / "docs" / "hash-b.txt";
    const fs::path third = root / "docs" / "hash-c.txt";
    const fs::path fifth = root / "docs" / "hash-d.txt";

    // Simulates one of the already-renamed-but-inconsistently-formatted files
    // from the real court-filing batch. Batch 12 should re-evaluate and normalize it.
    const fs::path fourth =
        root / "docs" / "09.08.2020_Declaration_of_Service.txt";

    writeText(
        first,
        "SUPERIOR COURT\nFILED 09/15/2026 21:02\nDeclaration of Petitioner\n");

    writeText(
        second,
        "SUPERIOR COURT\nFILED 09/16/2026 09:31\nMotion for Continuance\n");

    // Put the filing stamp after a large filler prefix. The date line itself has
    // no keyword, so Batch 16 must retain adjacent signal context around FILED.
    writeText(
        third,
        std::string(5000, 'x')
        + "\nFILED\n04/01/2022\nNOTICE OF DISQUALIFICATION\n");

    writeText(
        fifth,
        "SUPERIOR COURT\nDocument with insufficient filing metadata\n");

    writeText(
        fourth,
        "SUPERIOR COURT\nFILED 09/08/2020\nDeclaration of Service\n");

    FakeModelProvider model;
    rose::logging::Logger logger{ rose::logging::LoggerConfig{
        .mode = rose::logging::LogMode::Silent,
        .capacity = 64,
        .maxMessageBytes = 2048,
        .maxSourceBytes = 64
    } };

    rose::tools::PlanDirectoryDocumentRenamesTool planner{
        model,
        logger,
        std::make_unique<FakeOcrEngine>(),
        root / "plans"
    };

    const rose::tools::ToolResult planned = planner.execute(
        rose::tools::ToolRequest{
            .toolId = "plan_directory_document_renames",
            .arguments = {
                { "path", (root / "docs").string() },
                {
                    "instruction",
                    "Rename each court filing to its filing date followed by filing type."
                }
            }
        });

    assert(planned.success);
    assert(planned.message.find("planning_mode=filing-metadata") != std::string::npos);
    assert(planned.message.find("files_total=5") != std::string::npos);
    assert(planned.message.find("planned_operations=4") != std::string::npos);
    assert(planned.message.find("ambiguous_files=1") != std::string::npos);
    assert(planned.message.find("persistent_document_results=5") != std::string::npos);
    assert(planned.message.find("classifier_retries=1") != std::string::npos);
    assert(planned.message.find("field_recovery_attempts=1") != std::string::npos);
    assert(planned.message.find("resolved_on_primary=0") != std::string::npos);
    assert(planned.message.find("resolved_on_retry=0") != std::string::npos);
    assert(planned.message.find("resolved_on_field_recovery=0") != std::string::npos);
    assert(planned.message.find("resolved_deterministically=4") != std::string::npos);
    assert(planned.message.find("canonical_filename_pattern=M.D.YYYY[_HHMM] Filing Type.ext") != std::string::npos);

    const std::string marker = "plan_path=";
    const std::size_t begin = planned.message.find(marker);
    assert(begin != std::string::npos);
    const std::size_t valueBegin = begin + marker.size();
    const std::size_t end = planned.message.find('\n', valueBegin);
    const fs::path planPath =
        planned.message.substr(valueBegin, end - valueBegin);

    assert(fs::exists(planPath));

    const std::string planText = readAll(planPath);
    assert(planText.find("ROSE_RENAME_PLAN_V1") == 0);
    assert(planText.find("MODE\tfiling-metadata") != std::string::npos);
    assert(planText.find("STRUCTURED_RESULTS\t5") != std::string::npos);
    assert(planText.find("FIELD_RECOVERY_ATTEMPTS\t1") != std::string::npos);
    assert(planText.find("PRIMARY_RESOLVED\t0") != std::string::npos);
    assert(planText.find("RETRY_RESOLVED\t0") != std::string::npos);
    assert(planText.find("FIELD_RECOVERY_RESOLVED\t0") != std::string::npos);
    assert(planText.find("DETERMINISTIC_RESOLVED\t4") != std::string::npos);
    assert(planText.find("\t2026-09-15\t2102\tDeclaration of Petitioner\thigh\tdeterministic\t") != std::string::npos);
    assert(planText.find("\t2020-09-08\t\tDeclaration of Service\thigh\tdeterministic\t") != std::string::npos);
    assert(planText.find("\t2022-04-01\t\tNotice of Disqualification\thigh\tdeterministic\t") != std::string::npos);
    assert(planText.find("missing_filing_date_and_type") != std::string::npos);

    // Planning is non-destructive with respect to the user's directory.
    assert(fs::exists(first));
    assert(fs::exists(second));
    assert(fs::exists(third));
    assert(fs::exists(fourth));
    assert(fs::exists(fifth));

    rose::tools::ApplyRenamePlanTool apply{
        root / "plans"
    };

    const rose::tools::ToolResult applied = apply.execute(
        rose::tools::ToolRequest{
            .toolId = "apply_rename_plan",
            .arguments = {
                { "plan_path", planPath.string() }
            }
        });

    assert(applied.success);
    assert(!fs::exists(first));
    assert(!fs::exists(second));
    assert(!fs::exists(third));
    assert(!fs::exists(fourth));
    assert(fs::exists(fifth));

    assert(fs::exists(
        root / "docs" / "9.15.2026_2102 Declaration of Petitioner.txt"));
    assert(fs::exists(
        root / "docs" / "9.16.2026_0931 Motion for Continuance.txt"));
    assert(fs::exists(
        root / "docs" / "9.8.2020 Declaration of Service.txt"));
    assert(fs::exists(
        root / "docs" / "4.1.2022 Notice of Disqualification.txt"));

    fs::remove_all(root, error);

    std::cout << "Rose DirectoryRenamePlan tests: PASS\n";
    return 0;
}
