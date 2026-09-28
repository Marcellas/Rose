#include "agent/CodingTaskPlan.h"

#include <iostream>
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
}


int main()
{
    try
    {
        const auto parsed =
            rose::agent::parseCodingTaskPlanStep(
                "edit_text_file|C:\\Rose\\src\\Example.h|Update the declaration");

        require(
            parsed.has_value()
                && parsed->toolId == "edit_text_file"
                && parsed->path == "C:\\Rose\\src\\Example.h"
                && parsed->note == "Update the declaration",
            "valid coding plan step should parse deterministically");

        require(
            !rose::agent::parseCodingTaskPlanStep(
                "launch_program|C:\\Rose\\tool.exe|Run helper").has_value(),
            "coding plans must reject tools outside the bounded coding tool set");

        require(
            !rose::agent::parseCodingTaskPlanStep(
                "edit_text_file|relative.cpp|Edit file").has_value(),
            "coding plans must reject non-absolute paths");

        rose::agent::CodingTaskPlan plan{
            .steps = {
                {
                    .toolId = "read_text_file",
                    .path = "C:\\Rose\\src\\Example.h",
                    .note = "Inspect the declaration"
                },
                {
                    .toolId = "edit_text_file",
                    .path = "C:\\Rose\\src\\Example.h",
                    .note = "Update the declaration"
                },
                {
                    .toolId = "edit_text_file",
                    .path = "C:\\Rose\\src\\Example.cpp",
                    .note = "Update the implementation"
                },
                {
                    .toolId = "build_cmake_project",
                    .path = "C:\\Rose",
                    .note = "Build the Rose target"
                }
            }
        };

        require(
            rose::agent::isValidCodingTaskPlan(plan),
            "bounded grounded multi-step plan should validate");

        require(
            !rose::agent::isValidCodingTaskPlan(
                rose::agent::CodingTaskPlan{
                    .steps = {
                        {
                            .toolId = "read_text_file",
                            .path = "C:\\Rose\\src\\Example.h",
                            .note = "Inspect header"
                        },
                        {
                            .toolId = "read_text_file",
                            .path = "C:\\Rose\\src\\Example.cpp",
                            .note = "Inspect source"
                        }
                    }
                }),
            "read-only discovery must not masquerade as a multi-file write plan");

        const rose::tools::ToolRequest currentEdit{
            .toolId = "edit_text_file",
            .arguments = {
                { "path", "C:\\Rose\\src\\Example.cpp" },
                { "operation", "replace_line_range" }
            }
        };

        require(
            rose::agent::codingTaskPlanCoversRequest(
                plan,
                currentEdit),
            "plan should match a pending coding request by tool and canonical path");

        const std::string confirmation =
            rose::agent::formatCodingTaskPlanForConfirmation(
                plan,
                currentEdit);

        require(
            confirmation.find("> 3. edit_text_file") != std::string::npos
                && confirmation.find("current_action_in_plan=true")
                    != std::string::npos,
            "confirmation should highlight the current exact action inside the larger plan");

        const std::string context =
            rose::agent::formatCodingTaskPlanContext(plan);

        require(
            context.find("<rose_coding_task_plan>") != std::string::npos
                && context.find("authority=false") != std::string::npos
                && context.find("step_count=4") != std::string::npos,
            "control context must explicitly keep the plan non-authoritative");

        rose::agent::CodingTaskWorkspaceState oneFile;
        oneFile.sourceWindows.push_back(
            rose::tools::SourceWindowEvidence{
                .path = "C:\\Rose\\src\\Example.h",
                .startLine = 1,
                .lineCount = 1,
                .lineSha256s = { "hash-a" },
                .sha256 = "window-a"
            });

        require(
            !rose::agent::codingTaskPlanRequiredBeforeRequest(
                oneFile,
                currentEdit),
            "single-file source work should not be forced through a multi-file plan");

        oneFile.sourceWindows.push_back(
            rose::tools::SourceWindowEvidence{
                .path = "C:\\Rose\\src\\Example.cpp",
                .startLine = 1,
                .lineCount = 1,
                .lineSha256s = { "hash-b" },
                .sha256 = "window-b"
            });

        require(
            rose::agent::codingTaskPlanRequiredBeforeRequest(
                oneFile,
                currentEdit),
            "first source mutation after observing multiple files should require an explicit coding plan");

        require(
            !rose::agent::codingTaskPlanRequiredBeforeRequest(
                oneFile,
                rose::tools::ToolRequest{
                    .toolId = "read_text_file",
                    .arguments = {
                        { "path", "C:\\Rose\\src\\Example.cpp" }
                    }
                }),
            "read-only discovery should remain possible before the multi-file plan is created");

        rose::agent::CodingTaskPlan tooLarge;
        for (
            std::size_t index{ 0 };
            index < rose::agent::maximumCodingTaskPlanSteps + 1u;
            ++index)
        {
            tooLarge.steps.push_back(
                rose::agent::CodingTaskPlanStep{
                    .toolId = "read_text_file",
                    .path = "C:\\Rose\\src\\File.cpp",
                    .note = "Inspect"
                });
        }

        require(
            !rose::agent::isValidCodingTaskPlan(tooLarge),
            "coding task plans must remain hard-bounded");

        std::cout
            << "Rose CodingTaskPlan tests: PASS\n";

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "CodingTaskPlan test failed: "
            << exception.what()
            << '\n';

        return 1;
    }
}
