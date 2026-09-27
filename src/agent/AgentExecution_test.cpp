#include "agent/AgentJournal.h"
#include "agent/FileAgentJournalStore.h"
#include "agent/ToolExecutionService.h"
#include "permissions/ToolExecutionPolicy.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    void require(
        const bool condition,
        const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    class CountingTool final
        : public rose::tools::ITool
    {
    public:
        CountingTool(
            std::string id,
            const rose::tools::ToolRisk risk,
            const rose::tools::ToolConsent consent,
            int& executions)
            : executions_{ executions }
        {
            descriptor_.id = std::move(id);
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Agent execution test tool.";
            descriptor_.risk = risk;
            descriptor_.consent = consent;
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
            ++executions_;
            return rose::tools::ToolResult{
                .success = true,
                .message = "counted",
                .responseMode = rose::tools::ToolResponseMode::RequiresModelSynthesis,
                .artifacts = {}
            };
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
        int& executions_;
    };
}

int main()
{
    try
    {
        const auto root =
            std::filesystem::temp_directory_path()
            / "rose_agent_execution_test";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        error.clear();
        std::filesystem::create_directories(root, error);
        require(!error, "Could not create AgentExecution test directory.");

        const auto journalPath = root / "agent.rosejournal";
        int readExecutions{ 0 };
        int writeExecutions{ 0 };

        {
            rose::tools::ToolRegistry registry;
            registry.registerTool(
                std::make_unique<CountingTool>(
                    "inspect_local",
                    rose::tools::ToolRisk::ReadOnly,
                    rose::tools::ToolConsent::AutoAllowed,
                    readExecutions));
            registry.registerTool(
                std::make_unique<CountingTool>(
                    "write_local",
                    rose::tools::ToolRisk::LocalWrite,
                    rose::tools::ToolConsent::RequiresConfirmation,
                    writeExecutions));

            // Registration has an ownership-safe inverse. A dynamically loaded
            // plugin can remove an adapter without leaking or deleting it behind
            // the caller's back.
            auto removed = registry.removeTool("inspect_local");
            require(removed != nullptr, "removeTool should return ownership.");
            require(registry.find("inspect_local") == nullptr,
                    "Removed tool should no longer be registered.");
            registry.registerTool(std::move(removed));

            rose::agent::FileAgentJournalStore store{ journalPath };
            rose::agent::AgentJournal journal{
                rose::agent::AgentJournalConfig{
                    .maximumEvents = 32,
                    .maximumMessageBytes = 256,
                    .maximumDetailBytes = 512,
                    .maximumArgumentValueBytes = 128
                },
                &store
            };
            rose::permissions::ToolExecutionPolicy policy;
            rose::agent::ToolExecutionService executor{
                registry,
                policy,
                journal
            };

            const std::uint64_t runId = journal.beginRun(
                "Inspect then write.",
                rose::agent::AgentRunProvenance{
                    .projectId = "project-rose",
                    .discussionId = "discussion-b26"
                });

            const auto readResult = executor.execute(
                rose::tools::ToolRequest{
                    .toolId = "inspect_local",
                    .arguments = { { "path", "C:/Rose" } }
                },
                runId,
                1);
            require(
                readResult.status
                    == rose::agent::ToolExecutionServiceStatus::Completed,
                "Auto-allowed read tool should complete.");
            require(readExecutions == 1, "Read tool execution count mismatch.");

            const rose::tools::ToolRequest writeRequest{
                .toolId = "write_local",
                .arguments = { { "path", "C:/Rose/test.txt" } }
            };
            const auto pending = executor.execute(
                writeRequest,
                runId,
                2);
            require(
                pending.status
                    == rose::agent::ToolExecutionServiceStatus::RequiresConfirmation,
                "Local write should pause for confirmation.");
            require(writeExecutions == 0,
                    "Confirmation-gated tool must not execute early.");

            const auto confirmed = executor.execute(
                writeRequest,
                runId,
                2,
                rose::permissions::ToolConfirmationState::ExplicitlyConfirmed);
            require(
                confirmed.status
                    == rose::agent::ToolExecutionServiceStatus::Completed,
                "Confirmed local write should complete.");
            require(writeExecutions == 1, "Confirmed write execution count mismatch.");

            journal.record(
                rose::agent::AgentEvent{
                    .runId = runId,
                    .type = rose::agent::AgentEventType::RunCompleted,
                    .stepIndex = 0,
                    .toolId = {},
                    .projectId = {},
                    .discussionId = {},
                    .message = "test complete",
                    .detail = {},
                    .duration = {}
                });

            const auto events = journal.snapshot();
            require(!events.empty(), "Agent journal should contain events.");
            for (const auto& event : events)
            {
                if (event.runId == runId)
                {
                    require(event.projectId == "project-rose",
                            "Run events should inherit project provenance.");
                    require(event.discussionId == "discussion-b26",
                            "Run events should inherit discussion provenance.");
                }
            }
        }

        // Durable black-box trail should survive a normal restart.
        {
            rose::agent::FileAgentJournalStore store{ journalPath };
            rose::agent::AgentJournal journal{
                rose::agent::AgentJournalConfig{
                    .maximumEvents = 32,
                    .maximumMessageBytes = 256,
                    .maximumDetailBytes = 512,
                    .maximumArgumentValueBytes = 128
                },
                &store
            };
            require(journal.size() >= 7,
                    "Persisted agent journal did not reload expected events.");
            require(journal.persistenceError().empty(),
                    "Persisted journal should reload without an error.");
            require(
                journal.formatRecent().find("project=project-rose")
                    != std::string::npos,
                "Formatted journal should expose project provenance.");
        }

        std::filesystem::remove_all(root, error);
        std::cout << "Rose AgentExecution tests: PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rose AgentExecution tests: FAIL: "
                  << exception.what() << '\n';
        return 1;
    }
}
