#include "agent/CapabilityRoutingGuard.h"
#include "agent/ToolSelectionAgent.h"

#include "logging/Logger.h"
#include "model/IModelProvider.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"

#include <array>
#include <cstdlib>
#include <filesystem>
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

    std::cout
        << "Rose ToolSelectionAgent tests: PASS\n";

    return 0;
}
