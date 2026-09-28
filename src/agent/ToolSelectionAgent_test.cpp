#include "agent/CapabilityRoutingGuard.h"
#include "agent/ToolSelectionAgent.h"

#include "logging/Logger.h"
#include "model/IModelProvider.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    void require(
        const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            std::cerr
                << "ToolSelectionAgent test failed: "
                << message
                << '\n';

            std::exit(1);
        }
    }


    class CTestRecoveryFixture final
    {
    public:
        CTestRecoveryFixture()
        {
#ifdef _WIN32
            source_ =
                std::filesystem::temp_directory_path()
                / "RoseToolSelectionAgentCTestRecovery";
#else
            // CapabilityRoutingGuard intentionally recognizes Windows paths even
            // in portable unit tests. On non-Windows hosts a drive-qualified
            // string is a legal relative filename, so it gives us a deterministic
            // fixture without weakening production path validation.
            source_ =
                std::filesystem::path{
                    R"(C:\RoseToolSelectionAgentCTestRecovery)"
                };
#endif

            std::error_code error;
            std::filesystem::remove_all(source_, error);
            error.clear();
            std::filesystem::create_directories(source_ / "build", error);
            require(!error, "CTest recovery fixture directory must be creatable");

            std::ofstream{ source_ / "CMakeLists.txt" }
                << "cmake_minimum_required(VERSION 3.20)\n";
            std::ofstream{ source_ / "build" / "CMakeCache.txt" }
                << "# fixture\n";
            std::ofstream{ source_ / "build" / "CTestTestfile.cmake" }
                << "# fixture\n";
        }


        ~CTestRecoveryFixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(source_, ignored);
        }


        [[nodiscard]]
        std::string requestPath() const
        {
            return source_.string();
        }

    private:
        std::filesystem::path source_;
    };


    class FixedResponseModelProvider final
        : public rose::model::IModelProvider
    {
    public:
        explicit FixedResponseModelProvider(
            std::string response)
            : response_{ std::move(response) }
        {
        }


        [[nodiscard]]
        rose::model::ModelResponse generate(
            const rose::model::ModelRequest&) override
        {
            return rose::model::ModelResponse{
                .text = response_,
                .reasoning = {},
                .generatedTokens = 0,
                .finishReason =
                    rose::model::ModelFinishReason::EndOfGeneration
            };
        }


        [[nodiscard]]
        rose::model::ModelContextUsage inspectContext(
            const rose::model::ModelRequest&) const override
        {
            return {};
        }

    private:
        std::string response_;
    };


    class DummyTool final
        : public rose::tools::ITool
    {
    public:
        DummyTool(
            std::string id,
            const bool promptRequired)
        {
            descriptor_.id = std::move(id);
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Test tool.";
            descriptor_.risk = rose::tools::ToolRisk::ReadOnly;
            descriptor_.consent = rose::tools::ToolConsent::AutoAllowed;

            if (promptRequired)
            {
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "prompt",
                        .description = "Prompt.",
                        .type = rose::tools::ToolValueType::String,
                        .required = true
                    });

                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "quality",
                        .description = "Quality.",
                        .type = rose::tools::ToolValueType::String,
                        .required = false
                    });
            }
        }


        [[nodiscard]]
        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }


        [[nodiscard]]
        rose::tools::ToolResult execute(
            const rose::tools::ToolRequest&) override
        {
            return {};
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
    };


    class PathDummyTool final
        : public rose::tools::ITool
    {
    public:
        explicit PathDummyTool(
            std::string id)
        {
            descriptor_.id = std::move(id);
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Test path tool.";
            descriptor_.risk = rose::tools::ToolRisk::ReadOnly;
            descriptor_.consent = rose::tools::ToolConsent::RequiresConfirmation;
            descriptor_.parameters.push_back(
                rose::tools::ToolParameterDescriptor{
                    .name = "path",
                    .description = "Absolute path.",
                    .type = rose::tools::ToolValueType::String,
                    .required = true
                });
            if (descriptor_.id == "read_text_file")
            {
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "start_line",
                        .description = "Optional one-based start line.",
                        .type = rose::tools::ToolValueType::Integer,
                        .required = false
                    });
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "line_count",
                        .description = "Optional bounded line count.",
                        .type = rose::tools::ToolValueType::Integer,
                        .required = false
                    });
            }

        }

        [[nodiscard]]
        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }

        [[nodiscard]]
        rose::tools::ToolResult execute(
            const rose::tools::ToolRequest&) override
        {
            return {};
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
    };


    class ArchiveMutationDummyTool final
        : public rose::tools::ITool
    {
    public:
        explicit ArchiveMutationDummyTool(std::string id)
        {
            descriptor_.id = std::move(id);
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Test archive mutation tool.";
            descriptor_.risk = rose::tools::ToolRisk::LocalWrite;
            descriptor_.consent = rose::tools::ToolConsent::RequiresConfirmation;

            descriptor_.parameters.push_back(
                rose::tools::ToolParameterDescriptor{
                    .name = descriptor_.id == "extract_zip_archive" ? "path" : "source",
                    .description = "Source path.",
                    .type = rose::tools::ToolValueType::String,
                    .required = true
                });
            descriptor_.parameters.push_back(
                rose::tools::ToolParameterDescriptor{
                    .name = "destination",
                    .description = "Destination path.",
                    .type = rose::tools::ToolValueType::String,
                    .required = true
                });
        }

        [[nodiscard]]
        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }

        [[nodiscard]]
        rose::tools::ToolResult execute(
            const rose::tools::ToolRequest&) override
        {
            return {};
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
    };


    class TextMutationDummyTool final
        : public rose::tools::ITool
    {
    public:
        explicit TextMutationDummyTool(std::string id)
        {
            descriptor_.id = std::move(id);
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Test text mutation tool.";
            descriptor_.risk = rose::tools::ToolRisk::LocalWrite;
            descriptor_.consent = rose::tools::ToolConsent::RequiresConfirmation;

            descriptor_.parameters.push_back(
                rose::tools::ToolParameterDescriptor{
                    .name = "path",
                    .description = "Text/source path.",
                    .type = rose::tools::ToolValueType::String,
                    .required = true
                });

            if (descriptor_.id == "create_text_file")
            {
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "content",
                        .description = "Initial content.",
                        .type = rose::tools::ToolValueType::String,
                        .required = false
                    });
            }
            else
            {
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "operation",
                        .description = "Text mutation operation.",
                        .type = rose::tools::ToolValueType::String,
                        .required = true
                    });
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "find_text",
                        .description = "Exact source text.",
                        .type = rose::tools::ToolValueType::String,
                        .required = false
                    });
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "replacement_text",
                        .description = "Replacement source text.",
                        .type = rose::tools::ToolValueType::String,
                        .required = false
                    });
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "text",
                        .description = "Appended source text.",
                        .type = rose::tools::ToolValueType::String,
                        .required = false
                    });
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "start_line",
                        .description = "First source line.",
                        .type = rose::tools::ToolValueType::Integer,
                        .required = false
                    });
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "line_count",
                        .description = "Source line count.",
                        .type = rose::tools::ToolValueType::Integer,
                        .required = false
                    });
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "expected_text",
                        .description = "Expected line preimage.",
                        .type = rose::tools::ToolValueType::String,
                        .required = false
                    });
            }
        }

        [[nodiscard]]
        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }

        [[nodiscard]]
        rose::tools::ToolResult execute(const rose::tools::ToolRequest&) override
        {
            return {};
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
    };


    class CMakeBuildDummyTool final
        : public rose::tools::ITool
    {
    public:
        CMakeBuildDummyTool()
        {
            descriptor_.id = "build_cmake_project";
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Build one exact CMake project.";
            descriptor_.risk = rose::tools::ToolRisk::ExternalEffect;
            descriptor_.consent = rose::tools::ToolConsent::RequiresConfirmation;
            descriptor_.parameters = {
                { .name = "source_path", .description = "Source directory.", .type = rose::tools::ToolValueType::String, .required = true },
                { .name = "configuration", .description = "Configuration.", .type = rose::tools::ToolValueType::String, .required = false },
                { .name = "target", .description = "Target.", .type = rose::tools::ToolValueType::String, .required = false },
                { .name = "jobs", .description = "Parallel jobs.", .type = rose::tools::ToolValueType::Integer, .required = false }
            };
        }

        [[nodiscard]]
        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }

        [[nodiscard]]
        rose::tools::ToolResult execute(const rose::tools::ToolRequest&) override
        {
            return {};
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
    };


    class CMakeConfigureDummyTool final
        : public rose::tools::ITool
    {
    public:
        CMakeConfigureDummyTool()
        {
            descriptor_.id = "reconfigure_cmake_project";
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Reconfigure one exact existing CMake build tree.";
            descriptor_.risk = rose::tools::ToolRisk::ExternalEffect;
            descriptor_.consent = rose::tools::ToolConsent::RequiresConfirmation;
            descriptor_.parameters = {
                { .name = "source_path", .description = "Source directory.", .type = rose::tools::ToolValueType::String, .required = true }
            };
        }

        [[nodiscard]]
        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }

        [[nodiscard]]
        rose::tools::ToolResult execute(const rose::tools::ToolRequest&) override
        {
            return {};
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
    };


    class CMakeTestDummyTool final
        : public rose::tools::ITool
    {
    public:
        CMakeTestDummyTool()
        {
            descriptor_.id = "run_cmake_tests";
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Run registered CTest tests for one exact CMake project.";
            descriptor_.risk = rose::tools::ToolRisk::ExternalEffect;
            descriptor_.consent = rose::tools::ToolConsent::RequiresConfirmation;
            descriptor_.parameters = {
                { .name = "source_path", .description = "Source directory.", .type = rose::tools::ToolValueType::String, .required = true },
                { .name = "configuration", .description = "Configuration.", .type = rose::tools::ToolValueType::String, .required = false },
                { .name = "test", .description = "Exact test name.", .type = rose::tools::ToolValueType::String, .required = false },
                { .name = "jobs", .description = "Parallel jobs.", .type = rose::tools::ToolValueType::Integer, .required = false }
            };
        }

        [[nodiscard]]
        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }

        [[nodiscard]]
        rose::tools::ToolResult execute(const rose::tools::ToolRequest&) override
        {
            return {};
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
    };


    class OfficeMutationDummyTool final
        : public rose::tools::ITool
    {
    public:
        explicit OfficeMutationDummyTool(std::string id)
        {
            descriptor_.id = std::move(id);
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Test Office mutation tool.";
            descriptor_.risk = rose::tools::ToolRisk::LocalWrite;
            descriptor_.consent = rose::tools::ToolConsent::RequiresConfirmation;

            descriptor_.parameters.push_back(
                rose::tools::ToolParameterDescriptor{
                    .name = "path",
                    .description = "Office path.",
                    .type = rose::tools::ToolValueType::String,
                    .required = true
                });

            if (descriptor_.id == "create_office_document")
            {
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "kind",
                        .description = "Office kind.",
                        .type = rose::tools::ToolValueType::String,
                        .required = true
                    });
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "content",
                        .description = "Initial content.",
                        .type = rose::tools::ToolValueType::String,
                        .required = false
                    });
            }
            else
            {
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "operation",
                        .description = "Mutation operation.",
                        .type = rose::tools::ToolValueType::String,
                        .required = true
                    });
                descriptor_.parameters.push_back(
                    rose::tools::ToolParameterDescriptor{
                        .name = "text",
                        .description = "Mutation text.",
                        .type = rose::tools::ToolValueType::String,
                        .required = false
                    });
            }
        }

        [[nodiscard]]
        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }

        [[nodiscard]]
        rose::tools::ToolResult execute(const rose::tools::ToolRequest&) override
        {
            return {};
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
    };


    class PdfMutationDummyTool final : public rose::tools::ITool
    {
    public:
        explicit PdfMutationDummyTool(std::string id)
        {
            descriptor_.id = std::move(id);
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Test PDF mutation tool.";
            descriptor_.risk = rose::tools::ToolRisk::LocalWrite;
            descriptor_.consent = rose::tools::ToolConsent::RequiresConfirmation;

            if (descriptor_.id == "extract_pdf_pages")
            {
                descriptor_.parameters.push_back({ .name = "source_path", .description = "Source PDF.", .type = rose::tools::ToolValueType::String, .required = true });
                descriptor_.parameters.push_back({ .name = "destination_path", .description = "Destination PDF.", .type = rose::tools::ToolValueType::String, .required = true });
                descriptor_.parameters.push_back({ .name = "pages", .description = "Page range.", .type = rose::tools::ToolValueType::String, .required = true });
            }
            else
            {
                descriptor_.parameters.push_back({ .name = "path", .description = "PDF path.", .type = rose::tools::ToolValueType::String, .required = true });
                if (descriptor_.id == "edit_pdf_document")
                {
                    descriptor_.parameters.push_back({ .name = "operation", .description = "Mutation operation.", .type = rose::tools::ToolValueType::String, .required = true });
                    descriptor_.parameters.push_back({ .name = "source_path", .description = "Optional source PDF.", .type = rose::tools::ToolValueType::String, .required = false });
                    descriptor_.parameters.push_back({ .name = "page", .description = "Page index.", .type = rose::tools::ToolValueType::Integer, .required = false });
                    descriptor_.parameters.push_back({ .name = "rotation_degrees", .description = "Rotation.", .type = rose::tools::ToolValueType::Integer, .required = false });
                }
                else
                {
                    descriptor_.parameters.push_back({ .name = "text", .description = "Initial text.", .type = rose::tools::ToolValueType::String, .required = false });
                }
            }
        }

        [[nodiscard]] const rose::tools::ToolDescriptor& descriptor() const noexcept override { return descriptor_; }
        [[nodiscard]] rose::tools::ToolResult execute(const rose::tools::ToolRequest&) override { return {}; }

    private:
        rose::tools::ToolDescriptor descriptor_;
    };


    rose::tools::ToolRegistry makeRegistry()
    {
        rose::tools::ToolRegistry registry;

        registry.registerTool(
            std::make_unique<DummyTool>(
                "generate_image",
                true));

        registry.registerTool(
            std::make_unique<DummyTool>(
                "read_text_file",
                false));

        return registry;
    }


    rose::agent::AgentDecision decide(
        std::string response,
        const std::string_view userText =
            "Generate an image.")
    {
        FixedResponseModelProvider provider{
            std::move(response)
        };

        rose::tools::ToolRegistry registry =
            makeRegistry();

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode = rose::logging::LogMode::Silent
            }
        };

        rose::agent::ToolSelectionAgent agent{
            provider,
            registry,
            logger
        };

        return agent.decide(
            userText);
    }
}


int main()
{
    using rose::agent::AgentAction;

    {
        const auto decision =
            decide(
                "ACTION=TOOL\n"
                "TOOL=generate_image\n"
                "ARG prompt=adult pixie\n"
                "END\n");

        require(
            decision.action == AgentAction::InvokeTool,
            "canonical ACTION=TOOL form should still invoke the tool");

        require(
            decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "generate_image",
            "canonical form should preserve the registered tool id");
    }


    {
        const auto decision =
            decide(
                "ACTION=generate_image\n"
                "ARG prompt=adult pixie\n"
                "END\n");

        require(
            decision.action == AgentAction::InvokeTool,
            "registered-tool shorthand should invoke the tool");

        require(
            decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "generate_image",
            "registered-tool shorthand should normalize to the descriptor id");
    }


    {
        const auto decision =
            decide(
                "ACTION=GENERATE_IMAGE\n"
                "ARG prompt=adult pixie\n"
                "END\n");

        require(
            decision.action == AgentAction::InvokeTool,
            "registered-tool shorthand should tolerate ASCII case variation");
    }


    {
        const auto decision =
            decide(
                "ACTION=generate_image\n"
                "TOOL=read_text_file\n"
                "ARG prompt=adult pixie\n"
                "END\n");

        require(
            decision.action == AgentAction::RespondNormally,
            "conflicting ACTION/TOOL ids should fail closed to conversation");
    }


    {
        const auto decision =
            decide(
                "ACTION=generate_image\n"
                "END\n");

        require(
            decision.action == AgentAction::RespondNormally,
            "shorthand must still enforce required tool arguments");
    }


    {
        const auto decision =
            decide(
                "ACTION=not_a_registered_tool\n"
                "END\n");

        require(
            decision.action == AgentAction::RespondNormally,
            "unknown ACTION shorthand must not invent a capability");
    }


    {
        const auto decision =
            decide(
                "ACTION=RESPOND\n"
                "END\n");

        require(
            decision.action == AgentAction::RespondNormally,
            "ACTION=RESPOND should remain normal conversation");
    }


    {
        const auto decision =
            decide(
                "ACTION=generate_image\n"
                "ARG prompt=explicit nude adult pixie woman\n"
                "ARG quality=high\n"
                "END\n",
                "Generate a picture of an explicit nude adult pixie woman.");

        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value(),
            "adult image request should still invoke generate_image");

        require(
            !decision.toolRequest->arguments.contains(
                "quality"),
            "content words must not preserve a model-invented high quality");
    }


    {
        const auto decision =
            decide(
                "ACTION=generate_image\n"
                "ARG prompt=adult pixie woman\n"
                "ARG quality=high\n"
                "END\n",
                "Generate a high quality picture of an adult pixie woman.");

        require(
            decision.toolRequest.has_value()
                && decision.toolRequest->arguments.contains(
                    "quality")
                && decision.toolRequest->arguments.at(
                    "quality") == "high",
            "explicit user quality intent should preserve the proposed quality");
    }


    {
        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<PathDummyTool>("read_text_file"));
        registry.registerTool(std::make_unique<PathDummyTool>("read_pdf"));
        registry.registerTool(std::make_unique<PathDummyTool>("read_office_document"));
        registry.registerTool(std::make_unique<PathDummyTool>("inspect_image"));
        registry.registerTool(std::make_unique<PathDummyTool>("inspect_media"));
        registry.registerTool(std::make_unique<PathDummyTool>("list_zip_archive"));
        registry.registerTool(std::make_unique<PathDummyTool>("inspect_database"));
        registry.registerTool(std::make_unique<PathDummyTool>("inspect_shortcut"));
        registry.registerTool(std::make_unique<PathDummyTool>("launch_program"));
        registry.registerTool(std::make_unique<DummyTool>("list_processes", false));
        registry.registerTool(std::make_unique<CMakeBuildDummyTool>());
        registry.registerTool(std::make_unique<CMakeConfigureDummyTool>());
        registry.registerTool(std::make_unique<CMakeTestDummyTool>());

        const auto sourceWindow =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Read C:\\Docs\\large-source.cpp starting at line 2498 for 5 lines.",
                registry,
                {});
        require(
            sourceWindow.has_value()
                && sourceWindow->toolId == "read_text_file"
                && sourceWindow->arguments.at("start_line") == "2498"
                && sourceWindow->arguments.at("line_count") == "5",
            "deterministic text recovery must preserve explicit source-window line arguments");

        const std::string trustedFailureMetadata =
            "metadata_kind=source_diagnostic\n"
            "producer_tool=build_cmake_project\n"
            "operation_success=false\n"
            "diagnostic_path=C:\\Rose\\src\\main.cpp\n"
            "diagnostic_line=2498\n"
            "diagnostic_column=17\n"
            "diagnostic_severity=error\n"
            "diagnostic_code=C2065\n"
            "suggested_read_start_line=2468\n"
            "suggested_read_line_count=80";

        const auto diagnosticRead =
            rose::agent::CapabilityRoutingGuard::recoverDiagnosticSourceReadRequest(
                "Build C:\\Rose target Rose and fix any compiler errors.",
                registry,
                trustedFailureMetadata);
        require(
            diagnosticRead.has_value()
                && diagnosticRead->toolId == "read_text_file"
                && diagnosticRead->arguments.at("path") == "C:\\Rose\\src\\main.cpp"
                && diagnosticRead->arguments.at("start_line") == "2468"
                && diagnosticRead->arguments.at("line_count") == "80",
            "explicit repair workflows must recover the Rose-owned diagnostic source window without reparsing raw compiler output");

        const std::string trustedConfigureFailureMetadata =
            "metadata_kind=source_diagnostic\n"
            "producer_tool=reconfigure_cmake_project\n"
            "operation_success=false\n"
            "diagnostic_path=C:\\Rose\\CMakeLists.txt\n"
            "diagnostic_line=17\n"
            "diagnostic_column=0\n"
            "diagnostic_severity=error\n"
            "diagnostic_code=\n"
            "suggested_read_start_line=1\n"
            "suggested_read_line_count=47";

        const auto configureDiagnosticRead =
            rose::agent::CapabilityRoutingGuard::recoverDiagnosticSourceReadRequest(
                "Reconfigure C:\\Rose and fix any CMake errors.",
                registry,
                trustedConfigureFailureMetadata);
        require(
            configureDiagnosticRead.has_value()
                && configureDiagnosticRead->toolId == "read_text_file"
                && configureDiagnosticRead->arguments.at("path") == "C:\\Rose\\CMakeLists.txt"
                && configureDiagnosticRead->arguments.at("start_line") == "1"
                && configureDiagnosticRead->arguments.at("line_count") == "47",
            "failed CMake reconfiguration diagnostics must participate in the same grounded source-repair routing as configure/build/test failures");

        const auto buildOnlyDiagnosticRead =
            rose::agent::CapabilityRoutingGuard::recoverDiagnosticSourceReadRequest(
                "Build C:\\Rose target Rose.",
                registry,
                trustedFailureMetadata);
        require(!buildOnlyDiagnosticRead.has_value(),
            "a plain build request must report failure instead of silently expanding into source diagnosis");

        const auto untrustedProducerRead =
            rose::agent::CapabilityRoutingGuard::recoverDiagnosticSourceReadRequest(
                "Fix the compile error.",
                registry,
                "metadata_kind=source_diagnostic\n"
                "producer_tool=launch_program\n"
                "operation_success=false\n"
                "diagnostic_path=C:\\Rose\\src\\main.cpp\n"
                "suggested_read_start_line=2468\n"
                "suggested_read_line_count=80");
        require(!untrustedProducerRead.has_value(),
            "only configure/build/test validation producers may create trusted diagnostic source recovery");

        {
            FixedResponseModelProvider provider{
                "ACTION=TOOL\n"
                "TOOL=read_text_file\n"
                "ARG path=C:\\Rose\\src\\main.cpp\n"
                "ARG start_line=1\n"
                "ARG line_count=200\n"
                "END\n"
            };
            rose::logging::Logger logger{
                rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
            };
            rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

            const auto canonicalDiagnosticDecision = agent.decide(
                "Build C:\\Rose target Rose and fix any compiler errors.",
                {},
                trustedFailureMetadata);
            require(
                canonicalDiagnosticDecision.action == AgentAction::InvokeTool
                    && canonicalDiagnosticDecision.toolRequest.has_value()
                    && canonicalDiagnosticDecision.toolRequest->toolId == "read_text_file"
                    && canonicalDiagnosticDecision.toolRequest->arguments.at("start_line") == "2468"
                    && canonicalDiagnosticDecision.toolRequest->arguments.at("line_count") == "80",
                "model-selected diagnostic reads must be canonicalized to Rose-owned bounded source-window metadata");

            const auto buildOnlyDecision = agent.decide(
                "Build C:\\Rose target Rose.",
                {},
                trustedFailureMetadata);
            require(
                buildOnlyDecision.action == AgentAction::RespondNormally,
                "model-selected diagnostic reads must be rejected when the original request did not authorize repair/debug follow-up");
        }

        const auto pdf = rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
            "Read C:\\Docs\\manual.pdf", registry, {});
        require(pdf.has_value() && pdf->toolId == "read_pdf",
                "PDF path should route to read_pdf instead of read_text_file");
        require(pdf->arguments.contains("instruction"),
                "recovered PDF reads should preserve the user's analysis instruction");

        const auto workbook = rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
            "Review C:\\Docs\\budget.xlsx", registry, {});
        require(workbook.has_value() && workbook->toolId == "read_office_document",
                "Excel path should route to read_office_document");
        require(workbook->arguments.contains("instruction"),
                "recovered Office reads should preserve the user's analysis instruction");

        const auto targetedOffice = rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
            "Read C:\\Docs\\Dunamis.docx and identify every magic-system design decision.",
            registry,
            {});
        require(
            targetedOffice.has_value()
                && targetedOffice->toolId == "read_office_document",
            "generic quantifiers inside an exact-file request must not trigger directory analysis");

        const auto image = rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
            "Inspect C:\\Docs\\diagram.png", registry, {});
        require(image.has_value() && image->toolId == "inspect_image",
                "Image path should route to inspect_image");

        const std::array<std::string_view, 1> completedPdf{ "read_pdf" };
        const auto completedPdfRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Read C:\\Docs\\manual.pdf", registry, completedPdf);
        require(!completedPdfRecovery.has_value(),
                "completed PDF read must satisfy the direct request instead of falling through to read_text_file");

        const std::array<std::string_view, 1> completedOffice{ "read_office_document" };
        const auto completedOfficeRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Review C:\\Docs\\budget.xlsx", registry, completedOffice);
        require(!completedOfficeRecovery.has_value(),
                "completed Office read must satisfy the direct request instead of falling through to read_text_file");

        const auto completedTargetedOfficeRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Read C:\\Docs\\Dunamis.docx and identify every magic-system design decision.",
                registry,
                completedOffice);
        require(
            !completedTargetedOfficeRecovery.has_value(),
            "completed exact Office analysis must not fall through to the directory analyzer");

        const std::array<std::string_view, 1> completedImage{ "inspect_image" };
        const auto completedImageRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Inspect C:\\Docs\\diagram.png", registry, completedImage);
        require(!completedImageRecovery.has_value(),
                "completed image inspection must satisfy the direct request instead of falling through to read_text_file");

        const auto video = rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
            "Inspect C:\\Docs\\clip.webm and tell me what happens", registry, {});
        require(video.has_value() && video->toolId == "inspect_media",
                "WebM path should route to inspect_media");
        require(video->arguments.contains("instruction"),
                "media recovery should preserve the user's question");

        const auto animatedGif = rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
            "Review C:\\Docs\\animation.gif", registry, {});
        require(animatedGif.has_value() && animatedGif->toolId == "inspect_media",
                "animated GIF should route to inspect_media instead of static image inspection");

        const std::array<std::string_view, 1> completedMedia{ "inspect_media" };
        const auto completedMediaRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Inspect C:\\Docs\\clip.webm and tell me what happens", registry, completedMedia);
        require(!completedMediaRecovery.has_value(),
                "completed media inspection must satisfy the request instead of falling through to text reading");

        const auto archiveRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "List C:\\Docs\\bundle.zip", registry, {});
        require(archiveRecovery.has_value() && archiveRecovery->toolId == "list_zip_archive",
                "ZIP archive inspection should route to list_zip_archive");

        const std::array<std::string_view, 1> completedZip{ "list_zip_archive" };
        const auto completedZipRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "List C:\\Docs\\bundle.zip", registry, completedZip);
        require(!completedZipRecovery.has_value(),
                "completed ZIP inspection must satisfy the direct request");

        const auto unsupportedArchiveRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Read C:\\Docs\\bundle.7z", registry, {});
        require(!unsupportedArchiveRecovery.has_value(),
                "recognized unsupported archive formats must not fall through to read_text_file");

        const auto databaseRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Inspect C:\\Docs\\app.sqlite and show me the tables", registry, {});
        require(databaseRecovery.has_value() && databaseRecovery->toolId == "inspect_database",
                "SQLite database paths should route to inspect_database");

        const std::array<std::string_view, 1> completedDatabase{ "inspect_database" };
        const auto completedDatabaseRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Inspect C:\\Docs\\app.sqlite", registry, completedDatabase);
        require(!completedDatabaseRecovery.has_value(),
                "completed database inspection must satisfy the direct request");

        const auto shortcutRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Tell me where C:\\Docs\\Editor.lnk points", registry, {});
        require(shortcutRecovery.has_value() && shortcutRecovery->toolId == "inspect_shortcut",
                "shortcut paths should route to inspect_shortcut");

        const std::array<std::string_view, 1> completedShortcut{ "inspect_shortcut" };
        const auto completedShortcutRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Inspect C:\\Docs\\Editor.lnk", registry, completedShortcut);
        require(!completedShortcutRecovery.has_value(),
                "completed shortcut inspection must satisfy the direct request");

        const auto launchRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Launch C:\\Apps\\Demo.exe", registry, {});
        require(launchRecovery.has_value() && launchRecovery->toolId == "launch_program",
                "exact executable launch requests should route to launch_program");

        const std::array<std::string_view, 1> completedLaunch{ "launch_program" };
        const auto completedLaunchRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Launch C:\\Apps\\Demo.exe", registry, completedLaunch);
        require(!completedLaunchRecovery.has_value(),
                "completed launch_program must satisfy the launch request");

        const auto processListRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "List the running processes", registry, {});
        require(processListRecovery.has_value() && processListRecovery->toolId == "list_processes",
                "running-process inventory requests should route to list_processes");

        const auto configureExplanationRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Show me how to reconfigure C:\\Projects\\Rose, but do not reconfigure it.",
                registry,
                {});
        require(
            !configureExplanationRecovery.has_value(),
            "non-executing CMake reconfiguration explanations must not fall through to read_text_file on the project directory");

        const std::array<std::string_view, 1> completedConfigure{ "reconfigure_cmake_project" };
        const auto completedConfigureRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Reconfigure C:\\Projects\\Rose.",
                registry,
                completedConfigure);
        require(
            !completedConfigureRecovery.has_value(),
            "a completed reconfigure request must not be reinterpreted as read_text_file(path=<project-directory>)");

        const auto buildExplanationRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Show me how to build C:\\Projects\\Rose, but do not build it.",
                registry,
                {});
        require(
            !buildExplanationRecovery.has_value(),
            "non-executing build explanations must not fall through to read_text_file on the project directory");

        const std::array<std::string_view, 1> completedBuild{ "build_cmake_project" };
        const auto completedBuildRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Build C:\\Projects\\Rose target Rose in Debug with 8 jobs.",
                registry,
                completedBuild);
        require(
            !completedBuildRecovery.has_value(),
            "a completed build request must not be reinterpreted as read_text_file(path=<project-directory>)");


        require(
            rose::agent::CapabilityRoutingGuard::explicitCMakeTestExecutionIntent(
                "Run all tests in C:\\Projects\\Rose in Debug with 8 jobs."),
            "run-all-tests requests must be recognized as explicit CTest execution intent");

        require(
            rose::agent::CapabilityRoutingGuard::explicitCMakeTestExecutionIntent(
                "Run test RoseTextMutationToolsTest in C:\\Projects\\Rose Debug with 4 jobs."),
            "run-one-test requests must be recognized as explicit CTest execution intent");

        require(
            !rose::agent::CapabilityRoutingGuard::explicitCMakeTestExecutionIntent(
                "Show me how to run tests in C:\\Projects\\Rose, but do not run tests."),
            "CTest explanation requests must remain non-executing");

        require(
            !rose::agent::CapabilityRoutingGuard::explicitCMakeTestExecutionIntent(
                "Should I run all tests in C:\\Projects\\Rose?"),
            "CTest advice questions must remain non-executing");

        CTestRecoveryFixture ctestFixture;
        const std::string quotedCTestPath =
            "\"" + ctestFixture.requestPath() + "\"";

        const auto allTestsRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Run all tests in " + quotedCTestPath + " in Debug with 8 jobs.",
                registry,
                {});
        require(
            allTestsRecovery.has_value()
                && allTestsRecovery->toolId == "run_cmake_tests",
            "run-all-tests must recover to run_cmake_tests when the configured project is grounded");
        require(
            allTestsRecovery->arguments.at("configuration") == "Debug"
                && allTestsRecovery->arguments.at("jobs") == "8"
                && !allTestsRecovery->arguments.contains("test"),
            "run-all-tests recovery must preserve configuration/jobs and omit an exact-test filter");

        const auto oneTestRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Run test RoseTextMutationToolsTest in " + quotedCTestPath
                    + " Debug with 4 jobs.",
                registry,
                {});
        require(
            oneTestRecovery.has_value()
                && oneTestRecovery->toolId == "run_cmake_tests"
                && oneTestRecovery->arguments.at("test") == "RoseTextMutationToolsTest"
                && oneTestRecovery->arguments.at("configuration") == "Debug"
                && oneTestRecovery->arguments.at("jobs") == "4",
            "run-one-test recovery must preserve the exact registered test/configuration/jobs");

        const auto testExplanationRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Show me how to run tests in C:\\Projects\\Rose, but do not run tests.",
                registry,
                {});
        require(
            !testExplanationRecovery.has_value(),
            "non-executing test explanations must not fall through to read_text_file on the project directory");

        const std::array<std::string_view, 1> completedTests{ "run_cmake_tests" };
        const auto completedTestRecovery =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Run tests in C:\\Projects\\Rose.",
                registry,
                completedTests);
        require(
            !completedTestRecovery.has_value(),
            "a completed test request must not be reinterpreted as read_text_file(path=<project-directory>)");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=inspect_image\n"
            "ARG path=C:\\Docs\\animation.gif\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<PathDummyTool>("inspect_image"));
        registry.registerTool(std::make_unique<PathDummyTool>("inspect_media"));

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide("Inspect C:\\Docs\\animation.gif");
        require(
            decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "inspect_media",
            "format canonicalization should redirect animated GIF from inspect_image to inspect_media");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=launch_program\n"
            "ARG path=C:\\Program Files\\Demo\\demo.exe\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<PathDummyTool>("launch_program"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto invented = agent.decide("Launch Demo for me.");
        require(invented.action == AgentAction::RespondNormally && !invented.toolRequest.has_value(),
            "launch_program must reject a model-invented executable path");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=read_office_document\n"
            "ARG path=C:\\Users\\Username\\Documents\\Dunamis.docx\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(
            std::make_unique<PathDummyTool>(
                "read_office_document"));

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode = rose::logging::LogMode::Silent
            }
        };

        rose::agent::ToolSelectionAgent agent{
            provider,
            registry,
            logger
        };

        const auto invented =
            agent.decide(
                "Read Dunamis.docx and identify every magic-system design decision.");

        require(
            invented.action == AgentAction::RespondNormally
                && !invented.toolRequest.has_value(),
            "exact-file readers must reject a model-invented absolute path when the user supplied only a filename");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=read_office_document\n"
            "ARG path=Dunamis.docx\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(
            std::make_unique<PathDummyTool>(
                "read_office_document"));

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode = rose::logging::LogMode::Silent
            }
        };

        rose::agent::ToolSelectionAgent agent{
            provider,
            registry,
            logger
        };

        const std::string context =
            "<rose_project_file_resolutions>\n"
            "source=active_project_approved_roots\n"
            "<rose_project_file_resolution>\n"
            "requested=Dunamis.docx\n"
            "status=unique\n"
            "matches=1\n"
            "absolute_path=C:\\Approved\\Dunamis.docx\n"
            "</rose_project_file_resolution>\n"
            "</rose_project_file_resolutions>";

        const auto resolved =
            agent.decide(
                "Read Dunamis.docx and summarize it.",
                context);

        require(
            resolved.action == AgentAction::InvokeTool
                && resolved.toolRequest.has_value()
                && resolved.toolRequest->arguments.at("path")
                    == "C:\\Approved\\Dunamis.docx",
            "a unique active-project file resolution should ground a bare filename without guessing");

        const auto recovered =
            rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
                "Read Dunamis.docx and summarize it.",
                registry,
                {},
                context);

        require(
            recovered.has_value()
                && recovered->toolId == "read_office_document"
                && recovered->arguments.at("path")
                    == "C:\\Approved\\Dunamis.docx",
            "deterministic recovery should reuse one unique Rose-resolved project file path");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=read_office_document\n"
            "ARG path=C:\\Docs\\Dunamis.docx\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(
            std::make_unique<PathDummyTool>(
                "read_office_document"));

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode = rose::logging::LogMode::Silent
            }
        };

        rose::agent::ToolSelectionAgent agent{
            provider,
            registry,
            logger
        };

        const auto grounded =
            agent.decide(
                "Read C:\\Docs\\Dunamis.docx and summarize it.");

        require(
            grounded.action == AgentAction::InvokeTool
                && grounded.toolRequest.has_value()
                && grounded.toolRequest->toolId == "read_office_document",
            "a user-supplied exact Office path must remain eligible for the format-aware reader");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=extract_zip_archive\n"
            "ARG path=C:\\Docs\\bundle.zip\n"
            "ARG destination=C:\\Invented\\Output\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(
            std::make_unique<ArchiveMutationDummyTool>(
                "extract_zip_archive"));

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode = rose::logging::LogMode::Silent
            }
        };

        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide(
            "Extract C:\\Docs\\bundle.zip for me.");

        require(
            decision.action == AgentAction::RespondNormally
                && !decision.toolRequest.has_value(),
            "ZIP extraction must reject a model-invented destination path");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=extract_zip_archive\n"
            "ARG path=C:\\Docs\\bundle.zip\n"
            "ARG destination=C:\\Docs\\bundle-unpacked\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(
            std::make_unique<ArchiveMutationDummyTool>(
                "extract_zip_archive"));

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode = rose::logging::LogMode::Silent
            }
        };

        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide(
            "Extract C:\\Docs\\bundle.zip to C:\\Docs\\bundle-unpacked.");

        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "extract_zip_archive",
            "ZIP extraction should preserve user-grounded source and destination paths");
    }


#ifdef _WIN32
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path()
            / "Rose routing directory with spaces";

        std::error_code error;
        std::filesystem::create_directories(directory, error);

        require(
            !error,
            "directory-routing test should create its temporary directory");

        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=list_directory\n"
            "ARG path="
            + directory.string()
            + "\nEND\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(
            std::make_unique<PathDummyTool>(
                "list_directory"));
        registry.registerTool(
            std::make_unique<PathDummyTool>(
                "read_text_file"));
        registry.registerTool(
            std::make_unique<PathDummyTool>(
                "scan_directory_tree"));
        registry.registerTool(
            std::make_unique<PathDummyTool>(
                "analyze_directory_documents"));

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode = rose::logging::LogMode::Silent
            }
        };

        rose::agent::ToolSelectionAgent agent{
            provider,
            registry,
            logger
        };

        const std::string userRequest =
            "Read each file under the directory "
            + directory.string()
            + " then rename each file based on its contents.";

        const rose::agent::AgentDecision decision =
            agent.decide(
                userRequest);

        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "analyze_directory_documents",
            "batch content request should normalize list_directory to document analysis");

        require(
            decision.toolRequest->arguments.at("path")
                == directory.lexically_normal().string(),
            "directory routing should preserve an unquoted existing Windows path containing spaces");

        std::filesystem::remove_all(directory, error);
    }
#endif


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_text_file\n"
            "ARG path=C:\\Docs\\sample.cpp\n"
            "ARG operation=replace_text\n"
            "ARG find_text=oldValue\n"
            "ARG replacement_text=newValue\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<TextMutationDummyTool>("edit_text_file"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Replace oldValue with newValue in C:\\Docs\\sample.cpp.");

        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "edit_text_file",
            "explicit grounded text mutation should remain a tool request");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_text_file\n"
            "ARG path=C:\\Docs\\sample.cpp\n"
            "ARG operation=replace_line_range\n"
            "ARG start_line=41\n"
            "ARG line_count=1\n"
            "ARG expected_text=\n"
            "ARG replacement_text=\\s\\sreturn 0;\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<TextMutationDummyTool>("edit_text_file"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Replace line 41 in C:\\Docs\\sample.cpp with return 0;.");

        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "edit_text_file",
            "replace_line_range should remain a grounded explicit text mutation");
        require(
            decision.toolRequest->arguments.at("expected_text").empty(),
            "the control protocol must preserve an explicitly empty optional preimage");
        require(
            decision.toolRequest->arguments.at("replacement_text")
                == "\\s\\sreturn 0;",
            "the control protocol must preserve escaped edge whitespace for the tool adapter");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_text_file\n"
            "ARG path=\n"
            "ARG operation=replace_text\n"
            "ARG find_text=oldValue\n"
            "ARG replacement_text=newValue\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<TextMutationDummyTool>("edit_text_file"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Replace oldValue with newValue in C:\\Docs\\sample.cpp.");

        require(
            decision.action == AgentAction::RespondNormally
                && !decision.toolRequest.has_value(),
            "allowing empty optional ARG values must not allow required tool arguments to be empty");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_text_file\n"
            "ARG path=C:\\Users\\Username\\Documents\\sample.cpp\n"
            "ARG operation=replace_text\n"
            "ARG find_text=oldValue\n"
            "ARG replacement_text=newValue\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<TextMutationDummyTool>("edit_text_file"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Replace oldValue with newValue in sample.cpp.");

        require(
            decision.action == AgentAction::RespondNormally
                && !decision.toolRequest.has_value(),
            "text mutation must reject a model-invented absolute file path");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_text_file\n"
            "ARG path=C:\\Docs\\sample.cpp\n"
            "ARG operation=replace_text\n"
            "ARG find_text=oldValue\n"
            "ARG replacement_text=newValue\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<TextMutationDummyTool>("edit_text_file"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Summarize C:\\Docs\\sample.cpp and tell me what it does.");

        require(
            decision.action == AgentAction::RespondNormally
                && !decision.toolRequest.has_value(),
            "read-only source requests must never be upgraded into a text mutation");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_text_file\n"
            "ARG path=C:\\Docs\\sample.cpp\n"
            "ARG operation=replace_text\n"
            "ARG find_text=oldValue\n"
            "ARG replacement_text=newValue\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<TextMutationDummyTool>("edit_text_file"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Show me how to edit C:\\Docs\\sample.cpp to replace oldValue with newValue, but do not edit it.");

        require(
            decision.action == AgentAction::RespondNormally
                && !decision.toolRequest.has_value(),
            "explicit no-edit wording must override text mutation verbs");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_text_file\n"
            "ARG path=sample.cpp\n"
            "ARG operation=replace_text\n"
            "ARG find_text=oldValue\n"
            "ARG replacement_text=newValue\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<TextMutationDummyTool>("edit_text_file"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const std::string context =
            "<rose_project_file_resolutions>\n"
            "source=active_project_approved_roots\n"
            "<rose_project_file_resolution>\n"
            "requested=sample.cpp\n"
            "status=unique\n"
            "matches=1\n"
            "absolute_path=C:\\Approved\\sample.cpp\n"
            "</rose_project_file_resolution>\n"
            "</rose_project_file_resolutions>";

        const auto decision = agent.decide(
            "Replace oldValue with newValue in sample.cpp.",
            context);

        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value()
                && decision.toolRequest->arguments.at("path")
                    == "C:\\Approved\\sample.cpp",
            "text mutation should accept only Rose-owned unique Project filename resolution");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=create_text_file\n"
            "ARG path=C:\\Docs\\notes.md\n"
            "ARG content=Hello\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<TextMutationDummyTool>("create_text_file"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Create a new text file at C:\\Docs\\notes.md containing Hello.");

        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value(),
            "explicit grounded text creation should remain a tool request");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=create_text_file\n"
            "ARG path=C:\\Temp\\invented.txt\n"
            "ARG content=Hello\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<TextMutationDummyTool>("create_text_file"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Create a new text file for me containing Hello.");

        require(
            decision.action == AgentAction::RespondNormally
                && !decision.toolRequest.has_value(),
            "create_text_file must reject a model-invented destination path");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_office_document\n"
            "ARG path=C:\\Docs\\Budget.xlsx\n"
            "ARG operation=set_excel_cell\n"
            "ARG text=4225.30\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<OfficeMutationDummyTool>("edit_office_document"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide(
            "Set C:\\Docs\\Budget.xlsx cell B7 on sheet Budget to 4225.30.");
        require(
            decision.action == AgentAction::InvokeTool && decision.toolRequest.has_value(),
            "explicit grounded Office mutation should remain a tool request");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_office_document\n"
            "ARG path=C:\\Users\\Username\\Documents\\Dunamis.docx\n"
            "ARG operation=append_word_text\n"
            "ARG text=New paragraph\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<OfficeMutationDummyTool>("edit_office_document"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide("Append 'New paragraph' to Dunamis.docx.");
        require(
            decision.action == AgentAction::RespondNormally && !decision.toolRequest.has_value(),
            "Office mutation must reject a model-invented absolute file path");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=create_office_document\n"
            "ARG path=C:\\Docs\\New Notes.docx\n"
            "ARG kind=word\n"
            "ARG content=Hello\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<OfficeMutationDummyTool>("create_office_document"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide(
            "Create a new Word document at C:\\Docs\\New Notes.docx containing Hello.");
        require(
            decision.action == AgentAction::InvokeTool && decision.toolRequest.has_value(),
            "explicit grounded Office creation should remain a tool request");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=edit_office_document\n"
            "ARG path=C:\\Docs\\Dunamis.docx\n"
            "ARG operation=append_word_text\n"
            "ARG text=Invented mutation\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<OfficeMutationDummyTool>("edit_office_document"));
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide("Summarize C:\\Docs\\Dunamis.docx.");
        require(
            decision.action == AgentAction::RespondNormally && !decision.toolRequest.has_value(),
            "read-only Office requests must reject a model-invented mutation");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\nTOOL=edit_pdf_document\nARG path=C:\\Docs\\manual.pdf\nARG operation=rotate_page\nARG page=1\nARG rotation_degrees=90\nEND\n"
        };
        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<PdfMutationDummyTool>("edit_pdf_document"));
        rose::logging::Logger logger{ rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent } };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide("Rotate page 1 of C:\\Docs\\manual.pdf clockwise 90 degrees.");
        require(decision.toolRequest.has_value() && decision.toolRequest->toolId == "edit_pdf_document",
            "explicit grounded PDF edit should be accepted");
    }

    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\nTOOL=edit_pdf_document\nARG path=C:\\Docs\\manual.pdf\nARG operation=rotate_page\nARG page=1\nARG rotation_degrees=90\nEND\n"
        };
        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<PdfMutationDummyTool>("edit_pdf_document"));
        rose::logging::Logger logger{ rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent } };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide("Summarize C:\\Docs\\manual.pdf.");
        require(decision.action == AgentAction::RespondNormally && !decision.toolRequest.has_value(),
            "read-only PDF request must never be upgraded into a mutation");
    }

    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\nTOOL=create_pdf_document\nARG path=C:\\Temp\\invented.pdf\nARG text=Hello\nEND\n"
        };
        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<PdfMutationDummyTool>("create_pdf_document"));
        rose::logging::Logger logger{ rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent } };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide("Create a PDF for me.");
        require(decision.action == AgentAction::RespondNormally && !decision.toolRequest.has_value(),
            "create_pdf_document must reject an invented destination path");
    }

    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\nTOOL=extract_pdf_pages\nARG source_path=C:\\Docs\\manual.pdf\nARG destination_path=C:\\Docs\\excerpt.pdf\nARG pages=1-3\nEND\n"
        };
        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<PdfMutationDummyTool>("extract_pdf_pages"));
        rose::logging::Logger logger{ rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent } };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide("Extract pages 1-3 from C:\\Docs\\manual.pdf to C:\\Docs\\excerpt.pdf.");
        require(decision.toolRequest.has_value() && decision.toolRequest->toolId == "extract_pdf_pages",
            "grounded PDF extraction should be accepted");
    }

    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=reconfigure_cmake_project\n"
            "ARG source_path=C:\\Projects\\Rose\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeConfigureDummyTool>());
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Reconfigure C:\\Projects\\Rose now.");
        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "reconfigure_cmake_project"
                && decision.toolRequest->arguments.at("source_path") == "C:\\Projects\\Rose",
            "an explicit grounded existing-tree CMake reconfiguration should be routable");
    }

    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=reconfigure_cmake_project\n"
            "ARG source_path=C:\\Other\\Project\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeConfigureDummyTool>());
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Reconfigure C:\\Projects\\Rose now.");
        require(
            decision.action == AgentAction::RespondNormally,
            "reconfigure_cmake_project must reject a model-invented source directory");
    }

    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=reconfigure_cmake_project\n"
            "ARG source_path=C:\\Projects\\Rose\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeConfigureDummyTool>());
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Show me how to reconfigure C:\\Projects\\Rose, but do not reconfigure it.");
        require(
            decision.action == AgentAction::RespondNormally,
            "CMake reconfiguration explanations and explicit no-configure requests must never execute CMake");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=build_cmake_project\n"
            "ARG source_path=C:\\Projects\\Rose\n"
            "ARG configuration=Release\n"
            "ARG target=install\n"
            "ARG jobs=32\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeBuildDummyTool>());

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };

        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };
        const auto decision = agent.decide(
            "Build C:\\Projects\\Rose now.");

        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "build_cmake_project",
            "an explicit grounded CMake build should be routable");
        require(
            decision.toolRequest->arguments.at("source_path") == "C:\\Projects\\Rose",
            "CMake build must preserve the grounded source path");
        require(
            !decision.toolRequest->arguments.contains("configuration")
                && !decision.toolRequest->arguments.contains("target")
                && !decision.toolRequest->arguments.contains("jobs"),
            "model-invented CMake build options must be discarded");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=build_cmake_project\n"
            "ARG source_path=C:\\Other\\Project\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeBuildDummyTool>());
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Build C:\\Projects\\Rose now.");
        require(
            decision.action == AgentAction::RespondNormally,
            "build_cmake_project must reject a model-invented source directory");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=build_cmake_project\n"
            "ARG source_path=C:\\Projects\\Rose\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeBuildDummyTool>());
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Show me how to build C:\\Projects\\Rose, but do not build it.");
        require(
            decision.action == AgentAction::RespondNormally,
            "build explanations and explicit no-build requests must never execute the project");
    }


    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=run_cmake_tests\n"
            "ARG source_path=C:\\Projects\\Rose\n"
            "ARG configuration=Release\n"
            "ARG test=InventedTest\n"
            "ARG jobs=32\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeTestDummyTool>());
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Run tests in C:\\Projects\\Rose now.");
        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value()
                && decision.toolRequest->toolId == "run_cmake_tests",
            "an explicit grounded CTest run should be routable");
        require(
            decision.toolRequest->arguments.at("source_path") == "C:\\Projects\\Rose",
            "CTest must preserve the grounded source path");
        require(
            !decision.toolRequest->arguments.contains("configuration")
                && !decision.toolRequest->arguments.contains("test")
                && !decision.toolRequest->arguments.contains("jobs"),
            "model-invented CTest options must be discarded");
    }

    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=run_cmake_tests\n"
            "ARG source_path=C:\\Projects\\Rose\n"
            "ARG configuration=Debug\n"
            "ARG test=RoseTextMutationToolsTest\n"
            "ARG jobs=4\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeTestDummyTool>());
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Run test RoseTextMutationToolsTest in C:\\Projects\\Rose Debug with 4 jobs.");
        require(
            decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value(),
            "grounded exact CTest filters should remain executable");
        require(
            decision.toolRequest->arguments.at("configuration") == "Debug"
                && decision.toolRequest->arguments.at("test") == "RoseTextMutationToolsTest"
                && decision.toolRequest->arguments.at("jobs") == "4",
            "explicit CTest configuration/test/jobs must be preserved");
    }

    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=run_cmake_tests\n"
            "ARG source_path=C:\\Other\\Project\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeTestDummyTool>());
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Run tests in C:\\Projects\\Rose.");
        require(
            decision.action == AgentAction::RespondNormally,
            "run_cmake_tests must reject a model-invented source directory");
    }

    {
        FixedResponseModelProvider provider{
            "ACTION=TOOL\n"
            "TOOL=run_cmake_tests\n"
            "ARG source_path=C:\\Projects\\Rose\n"
            "END\n"
        };

        rose::tools::ToolRegistry registry;
        registry.registerTool(std::make_unique<CMakeTestDummyTool>());
        rose::logging::Logger logger{
            rose::logging::LoggerConfig{ .mode = rose::logging::LogMode::Silent }
        };
        rose::agent::ToolSelectionAgent agent{ provider, registry, logger };

        const auto decision = agent.decide(
            "Show me how to run tests in C:\\Projects\\Rose, but do not run tests.");
        require(
            decision.action == AgentAction::RespondNormally,
            "test explanations and explicit no-test requests must never execute CTest");
    }


    std::cout
        << "Rose ToolSelectionAgent tests: PASS\n";

    return 0;
}
