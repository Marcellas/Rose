#ifdef _WIN32
#include <Windows.h>
#endif

#include "agent/AgentLoop.h"
#include "agent/ToolConfirmation.h"
#include "agent/ToolObservation.h"
#include "agent/ToolSelectionAgent.h"
#include "artifacts/ArtifactStore.h"
#include "avatar/AvatarController.h"
#include "avatar/SdlAvatar.h"
#include "core/RoseCore.h"
#include "imagegen/StableDiffusionCliGenerator.h"
#include "logging/Logger.h"
#include "model/LlamaCppModelProvider.h"
#include "model/LlamaLogBridge.h"
#include "ocr/TesseractCliOcrEngine.h"
#include "permissions/PermissionSystem.h"
#include "permissions/ToolExecutionPolicy.h"
#include "persistence/FileConversationStore.h"
#include "platform/SdlRuntime.h"
#include "platform/TtfRuntime.h"
#include "tools/AttachmentIngestion.h"
#include "tools/CreateTextFileTool.h"
#include "tools/GenerateImageTool.h"
#include "tools/GenerateImageRegisteredTool.h"
#include "tools/ToolRegistry.h"
#include "tools/ReadFileTool.h"
#include "ui/ChatBridge.h"
#include "ui/SdlChatWindow.h"
#include "vision/LlamaMtmdVisionProvider.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>


int main()
{
#ifdef _WIN32
    // Rose uses UTF-8 internally. Keep the development console in UTF-8 too.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try
    {
        // =====================================================================
        // Application-lifetime infrastructure
        // =====================================================================

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode = rose::logging::LogMode::Normal
            }
        };

        // Declaration order is intentional: TTF is destroyed before SDL.
        rose::platform::SdlRuntime sdlRuntime;
        rose::platform::TtfRuntime ttfRuntime;


        // =====================================================================
        // Avatar + graphical chat
        // =====================================================================

        rose::avatar::SdlAvatar avatar{
            sdlRuntime,
            320,
            300
        };

        avatar.loadSprite(
            "assets/avatar/RoseIdle.png");

        rose::avatar::AvatarController avatarController{
            avatar
        };

        // ChatBridge is the only normal communication path between the SDL/main
        // thread and Rose's worker thread.
        rose::ui::ChatBridge chatBridge;

        rose::ui::SdlChatWindow chatWindow{
            sdlRuntime,
            ttfRuntime,
            chatBridge,
            "assets/fonts/RoseSans.ttf",
            720,
            520
        };


        // =====================================================================
        // Worker synchronization
        // =====================================================================

        std::atomic<bool> conversationFinished{
            false
        };

        std::exception_ptr workerException;

        avatarController.handleActivity(
            rose::core::RoseActivity::Working);

        std::cout
            << "Rose v0.1\n"
            << "Loading local model...\n";


        // =====================================================================
        // Rose worker
        // =====================================================================
        //
        // Model inference, conversation mutation, attachment processing, OCR,
        // semantic vision, image generation orchestration, and persistence all
        // stay off the SDL thread.

        std::thread conversationWorker{
            [&logger,
             &avatarController,
             &chatBridge,
             &conversationFinished,
             &workerException]()
            {
                try
                {
                    // ---------------------------------------------------------
                    // llama.cpp logging + primary text model
                    // ---------------------------------------------------------

                    rose::model::LlamaLogBridge llamaLogBridge{
                        logger
                    };

                    rose::model::LlamaCppConfig modelConfig{
                        .modelPath =
                            "models/Qwen3-8B-Q4_K_M.gguf",

                        .contextSize =
                            8192,

                        // 999 means "offload every layer llama.cpp can offload".
                        .gpuLayers =
                            999
                    };

                    auto modelProvider =
                        std::make_unique<
                            rose::model::LlamaCppModelProvider>(
                                modelConfig,
                                logger);

                    // RoseCore will own the provider, but the Agent also needs
                    // non-owning access to the SAME already-loaded model.
                    //
                    // This pointer remains valid because:
                    //   1. RoseCore takes ownership immediately below.
                    //   2. ToolSelectionAgent is declared after RoseCore and is
                    //      therefore destroyed before RoseCore.
                    //   3. Everything stays on this single worker thread.
                    auto* agentModelProvider =
                        modelProvider.get();


                    // ---------------------------------------------------------
                    // Persistence + RoseCore
                    // ---------------------------------------------------------

                    rose::persistence::FileConversationStore
                        conversationStore{
                            "data/conversations/current.rosechat"
                        };

                    rose::core::RoseCore roseCore{
                        std::move(modelProvider),
                        logger,
                        conversationStore
                    };


                    // ---------------------------------------------------------
                    // Safe attachment input stack
                    // ---------------------------------------------------------

                    rose::permissions::PermissionSystem permissionSystem;

                    rose::tools::ReadFileTool readFileTool{
                        permissionSystem
                    };

                    auto ocrEngine =
                        std::make_unique<
                            rose::ocr::TesseractCliOcrEngine>();

                    auto visionProvider =
                        std::make_unique<
                            rose::vision::LlamaMtmdVisionProvider>(
                                rose::vision::LlamaMtmdVisionConfig{
                                    .modelPath =
                                        "models/vision/"
                                        "Qwen3VL-8B-Instruct-Q4_K_M.gguf",

                                    .mmprojPath =
                                        "models/vision/"
                                        "mmproj-Qwen3VL-8B-Instruct-Q8_0.gguf",

                                    .contextSize =
                                        4096,

                                    .maxGeneratedTokens =
                                        512,

                                    .gpuLayers =
                                        999,

                                    .mmprojUseGpu =
                                        true,

                                    .imageMaxTokens =
                                        1536
                                });

                    rose::tools::AttachmentIngestion attachmentIngestion{
                        permissionSystem,
                        readFileTool,
                        std::move(ocrEngine),
                        std::move(visionProvider)
                    };


                    // ---------------------------------------------------------
                    // Rose-owned image output stack
                    // ---------------------------------------------------------
                    //
                    // ArtifactStore owns the output location policy.
                    // StableDiffusionCliGenerator owns process-adapter state.
                    // GenerateImageTool borrows both for the worker lifetime.

                    rose::artifacts::ArtifactStore artifactStore{
                        "data/artifacts/generated"
                    };

                    rose::imagegen::StableDiffusionCliGenerator imageGenerator{
                        rose::imagegen::StableDiffusionCliConfig{
                            .modelPath =
                                "models/imagegen/"
                                "v1-5-pruned-emaonly.safetensors",

                            // Keep the first diffusion test inside an explicit
                            // budget while Qwen remains resident on the GPU.
                            .maxVramAssignment =
                                "cuda0=8"
                        }
                    };

                    rose::tools::GenerateImageTool generateImageTool{
                        imageGenerator,
                        artifactStore
                    };


                    // ---------------------------------------------------------
                    // Tool registry
                    // ---------------------------------------------------------
                    //
                    // ToolRegistry owns generic tool adapters. The adapter below
                    // borrows GenerateImageTool, which stays alive for the entire
                    // worker scope.
                    //
                    // This is the first step toward agent-selected tools: main no
                    // longer calls GenerateImageTool directly. Every execution now
                    // crosses the same generic ToolRequest/ToolResult boundary that
                    // the Agent will use in the next checkpoint.

                    rose::tools::ToolRegistry toolRegistry;

                    toolRegistry.registerTool(
                        std::make_unique<
                            rose::tools::GenerateImageRegisteredTool>(
                                generateImageTool));

                    // First user-confirmed filesystem mutation tool. It is owned
                    // directly by ToolRegistry and deliberately cannot overwrite
                    // an existing file.
                    toolRegistry.registerTool(
                        std::make_unique<
                            rose::tools::CreateTextFileTool>());


                    // ---------------------------------------------------------
                    // First bounded Agent layer
                    // ---------------------------------------------------------
                    //
                    // The Agent borrows the same provider RoseCore owns. It runs a
                    // short, non-persistent control inference that chooses either:
                    //
                    //     RespondNormally
                    //
                    // or exactly one ToolRequest.
                    //
                    // It never executes tools itself. Agent proposals still pass
                    // through ToolExecutionPolicy and ToolRegistry.

                    rose::permissions::ToolExecutionPolicy toolExecutionPolicy;

                    rose::agent::ToolSelectionAgent toolSelectionAgent{
                        *agentModelProvider,
                        toolRegistry,
                        logger
                    };

                    // The bounded Agent loop may execute several safe steps in one
                    // request, but never more than its configured ceiling.
                    //
                    // It still cannot bypass ToolExecutionPolicy. A step requiring
                    // consent pauses the WHOLE run with the exact ToolRequest intact.
                    rose::agent::AgentLoop agentLoop{
                        toolSelectionAgent,
                        toolRegistry,
                        toolExecutionPolicy,
                        logger,
                        rose::agent::AgentLoopConfig{
                            .maximumToolExecutions = 3
                        }
                    };

                    // At most one workflow may be paused for confirmation. The state
                    // includes prior tool observations so /confirm resumes the SAME
                    // user goal instead of starting a new model guess.
                    std::optional<rose::agent::PendingAgentRun>
                        pendingAgentRun;


                    avatarController.handleActivity(
                        rose::core::RoseActivity::Idle);

                    std::cout
                        << "Local model initialized.\n"
                        << "Input is now handled by Rose's SDL chat window.\n"
                        << "Natural-language tool selection is enabled.\n"
                        << "Commands: "
                        << "/image <prompt>, "
                        << "/tools, "
                        << "/confirm, "
                        << "/cancel, "
                        << "/clear, "
                        << "/log silent|normal|verbose, "
                        << "/quit\n\n";


                    // =========================================================
                    // Conversation loop
                    // =========================================================

                    while (true)
                    {
                        avatarController.handleActivity(
                            rose::core::RoseActivity::Listening);

                        std::optional<rose::input::UserSubmission>
                            pendingSubmission =
                                chatBridge.waitForUserSubmission();

                        if (!pendingSubmission)
                        {
                            break;
                        }

                        rose::input::UserSubmission submission =
                            std::move(*pendingSubmission);


                        // -----------------------------------------------------
                        // Submission metadata
                        // -----------------------------------------------------
                        //
                        // Development commands are only recognized for text-only
                        // submissions. A file whose name/content happens to look
                        // like a command is never treated as one.

                        const bool commandEligible =
                            submission.attachments.empty();

                        const std::string& submittedText =
                            submission.text;

                        std::cout
                            << "You: "
                            << submittedText;

                        if (!submission.attachments.empty())
                        {
                            std::cout
                                << " ["
                                << submission.attachments.size()
                                << " attachment";

                            if (submission.attachments.size() != 1)
                            {
                                std::cout << 's';
                            }

                            std::cout << ']';
                        }

                        std::cout << '\n';


                        // =====================================================
                        // Explicit paused-agent confirmation
                        // =====================================================
                        //
                        // /confirm resumes the exact PendingAgentRun. The stored
                        // ToolRequest is executed first, then the bounded loop may
                        // reason again and choose another step if the original goal
                        // still requires one.

                        std::optional<rose::agent::PendingAgentRun>
                            confirmedAgentRun;

                        if (
                            commandEligible
                            && submittedText == "/confirm")
                        {
                            if (!pendingAgentRun.has_value())
                            {
                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            AssistantStarted
                                    });

                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            AssistantFinished,

                                        .text =
                                            "There is no pending tool action to confirm."
                                    });

                                continue;
                            }

                            confirmedAgentRun =
                                std::move(*pendingAgentRun);

                            pendingAgentRun.reset();
                        }
                        else if (
                            commandEligible
                            && submittedText == "/cancel")
                        {
                            if (pendingAgentRun.has_value())
                            {
                                const bool partialWorkCompleted =
                                    pendingAgentRun->state.executedToolCount > 0;

                                pendingAgentRun.reset();

                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            AssistantStarted
                                    });

                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            AssistantFinished,

                                        .text =
                                            partialWorkCompleted
                                                ? "Cancelled the pending next action. Earlier steps that already completed remain in place."
                                                : "Cancelled. I did not execute the pending action."
                                    });
                            }
                            else
                            {
                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            AssistantStarted
                                    });

                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            AssistantFinished,

                                        .text =
                                            "There is no pending tool action to cancel."
                                    });
                            }

                            continue;
                        }
                        else if (pendingAgentRun.has_value())
                        {
                            // A paused action is intentionally short-lived. Any
                            // unrelated next message expires it, preventing a stale
                            // /confirm from firing much later.
                            pendingAgentRun.reset();
                        }


                        // =====================================================
                        // Development image-generation command
                        // =====================================================
                        //
                        // /image is still an explicit development command, but it no
                        // longer knows anything about StableDiffusionCliGenerator or
                        // GenerateImageTool. It builds a generic ToolRequest and asks
                        // ToolRegistry to execute it.
                        //
                        // The next checkpoint can replace this command parser with an
                        // Agent tool-selection pass without changing the tool itself.

                        constexpr std::string_view imageCommandPrefix{
                            "/image "
                        };

                        if (
                            commandEligible
                            && submittedText.starts_with(
                                imageCommandPrefix))
                        {
                            try
                            {
                                const std::string prompt =
                                    submittedText.substr(
                                        imageCommandPrefix.size());

                                if (prompt.empty())
                                {
                                    throw std::runtime_error{
                                        "Usage: /image <prompt>"
                                    };
                                }

                                avatarController.handleActivity(
                                    rose::core::RoseActivity::Working);

                                rose::tools::ToolRequest toolRequest{
                                    .toolId = "generate_image",
                                    .arguments = {
                                        { "prompt", prompt },
                                        { "width", "512" },
                                        { "height", "512" },
                                        { "steps", "20" },
                                        { "cfg_scale", "7.0" }
                                    }
                                };

                                rose::tools::ToolResult toolResult =
                                    toolRegistry.execute(
                                        toolRequest);

                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            AssistantStarted
                                    });

                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            AssistantFinished,

                                        .text =
                                            toolResult.message
                                    });

                                for (auto& artifact : toolResult.artifacts)
                                {
                                    chatBridge.postEvent(
                                        rose::ui::ChatEvent{
                                            .type =
                                                rose::ui::ChatEventType::
                                                ArtifactReady,

                                            .artifact =
                                                std::move(artifact)
                                        });
                                }

                                avatarController.handleActivity(
                                    rose::core::RoseActivity::Idle);
                            }
                            catch (const std::exception& exception)
                            {
                                avatarController.handleActivity(
                                    rose::core::RoseActivity::Confused);

                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::Error,

                                        .text =
                                            std::string{
                                                "Image generation failed: "
                                            }
                                            + exception.what()
                                    });
                            }

                            continue;
                        }


                        // =====================================================
                        // Tool registry diagnostic command
                        // =====================================================

                        if (
                            commandEligible
                            && submittedText == "/tools")
                        {
                            std::string toolList =
                                "Registered Rose tools:\n";

                            for (const rose::tools::ToolDescriptor& descriptor :
                                 toolRegistry.descriptors())
                            {
                                toolList +=
                                    "- "
                                    + descriptor.id
                                    + ": "
                                    + descriptor.description
                                    + "\n";
                            }

                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::ChatEventType::
                                        AssistantStarted
                                });

                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::ChatEventType::
                                        AssistantFinished,

                                    .text =
                                        std::move(toolList)
                                });

                            continue;
                        }


                        // =====================================================
                        // Development/control commands
                        // =====================================================

                        if (
                            commandEligible
                            && submittedText == "/log silent")
                        {
                            logger.setMode(
                                rose::logging::LogMode::Silent);

                            std::cout
                                << "Logging mode: Silent.\n\n";

                            continue;
                        }

                        if (
                            commandEligible
                            && submittedText == "/log normal")
                        {
                            logger.setMode(
                                rose::logging::LogMode::Normal);

                            std::cout
                                << "Logging mode: Normal.\n\n";

                            continue;
                        }

                        if (
                            commandEligible
                            && submittedText == "/log verbose")
                        {
                            logger.setMode(
                                rose::logging::LogMode::Verbose);

                            std::cout
                                << "Logging mode: Verbose.\n\n";

                            continue;
                        }

                        if (
                            commandEligible
                            && submittedText == "/quit")
                        {
                            chatBridge.requestShutdown();
                            break;
                        }

                        if (
                            commandEligible
                            && submittedText == "/clear")
                        {
                            pendingAgentRun.reset();
                            roseCore.clearConversation();

                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::ChatEventType::
                                        ConversationCleared
                                });

                            continue;
                        }


                        // Nothing typed and nothing attached means no request.
                        // Attachment-only requests are valid and are handled below.
                        if (
                            submittedText.empty()
                            && submission.attachments.empty())
                        {
                            continue;
                        }


                        // =====================================================
                        // One complete normal Rose request
                        // =====================================================
                        //
                        // This try/catch is deliberately INSIDE the loop. A bad
                        // attachment, context overflow, or inference failure must
                        // reject only this request, not kill Rose's worker.

                        try
                        {
                            rose::tools::IngestedUserSubmission ingested =
                                attachmentIngestion.ingest(
                                    std::move(submission));

                            std::string input =
                                std::move(
                                    ingested.userText);

                            std::string transientContext =
                                std::move(
                                    ingested.transientContext);

                            // -------------------------------------------------
                            // Bounded Agent workflow
                            // -------------------------------------------------
                            //
                            // Text-only requests enter a bounded loop:
                            //
                            //   decide -> policy -> execute -> observe -> decide
                            //
                            // Safe tools may chain automatically. Any step requiring
                            // confirmation pauses here and resumes later from the exact
                            // stored run state. Attachments still bypass Agent routing
                            // for now and go directly to normal RoseCore inference.

                            std::vector<rose::artifacts::Artifact> pendingArtifacts;

                            if (confirmedAgentRun.has_value())
                            {
                                avatarController.handleActivity(
                                    rose::core::RoseActivity::Working);

                                rose::agent::AgentLoopResult agentResult =
                                    agentLoop.resumeConfirmed(
                                        std::move(*confirmedAgentRun));

                                if (
                                    agentResult.status
                                    == rose::agent::AgentLoopStatus::
                                        RequiresConfirmation)
                                {
                                    // Earlier auto-allowed steps may already have
                                    // produced artifacts before a later step paused.
                                    // Deliver those now; they are real completed work.
                                    for (auto& artifact : agentResult.artifacts)
                                    {
                                        chatBridge.postEvent(
                                            rose::ui::ChatEvent{
                                                .type =
                                                    rose::ui::ChatEventType::
                                                    ArtifactReady,
                                                .artifact =
                                                    std::move(artifact)
                                            });
                                    }
                                    if (!agentResult.pending.has_value())
                                    {
                                        throw std::runtime_error{
                                            "Agent loop requested confirmation without pending state."
                                        };
                                    }

                                    pendingAgentRun =
                                        std::move(agentResult.pending);

                                    chatBridge.postEvent(
                                        rose::ui::ChatEvent{
                                            .type =
                                                rose::ui::ChatEventType::
                                                AssistantStarted
                                        });

                                    chatBridge.postEvent(
                                        rose::ui::ChatEvent{
                                            .type =
                                                rose::ui::ChatEventType::
                                                AssistantFinished,
                                            .text =
                                                pendingAgentRun->confirmation.
                                                    userFacingSummary
                                        });

                                    avatarController.handleActivity(
                                        rose::core::RoseActivity::Idle);

                                    continue;
                                }

                                for (auto& artifact : agentResult.artifacts)
                                {
                                    pendingArtifacts.push_back(
                                        std::move(artifact));
                                }

                                // /confirm is a control command, not the canonical
                                // user turn. Resume final response generation against
                                // the ORIGINAL request stored in the agent run.
                                input =
                                    std::move(agentResult.userTextForResponse);

                                transientContext =
                                    std::move(agentResult.transientContext);
                            }
                            else if (commandEligible)
                            {
                                avatarController.handleActivity(
                                    rose::core::RoseActivity::Thinking);

                                rose::agent::AgentLoopResult agentResult =
                                    agentLoop.start(
                                        input,
                                        transientContext);

                                if (
                                    agentResult.status
                                    == rose::agent::AgentLoopStatus::
                                        RequiresConfirmation)
                                {
                                    // Earlier auto-allowed steps may already have
                                    // produced artifacts before a later step paused.
                                    // Deliver those now; they are real completed work.
                                    for (auto& artifact : agentResult.artifacts)
                                    {
                                        chatBridge.postEvent(
                                            rose::ui::ChatEvent{
                                                .type =
                                                    rose::ui::ChatEventType::
                                                    ArtifactReady,
                                                .artifact =
                                                    std::move(artifact)
                                            });
                                    }
                                    if (!agentResult.pending.has_value())
                                    {
                                        throw std::runtime_error{
                                            "Agent loop requested confirmation without pending state."
                                        };
                                    }

                                    pendingAgentRun =
                                        std::move(agentResult.pending);

                                    chatBridge.postEvent(
                                        rose::ui::ChatEvent{
                                            .type =
                                                rose::ui::ChatEventType::
                                                AssistantStarted
                                        });

                                    chatBridge.postEvent(
                                        rose::ui::ChatEvent{
                                            .type =
                                                rose::ui::ChatEventType::
                                                AssistantFinished,
                                            .text =
                                                pendingAgentRun->confirmation.
                                                    userFacingSummary
                                        });

                                    avatarController.handleActivity(
                                        rose::core::RoseActivity::Idle);

                                    continue;
                                }

                                for (auto& artifact : agentResult.artifacts)
                                {
                                    pendingArtifacts.push_back(
                                        std::move(artifact));
                                }

                                input =
                                    std::move(agentResult.userTextForResponse);

                                transientContext =
                                    std::move(agentResult.transientContext);
                            }


                            bool responseStarted{
                                false
                            };


                            // -------------------------------------------------
                            // Rose activity -> avatar
                            // -------------------------------------------------

                            const rose::core::RoseActivityCallback onActivity =
                                [&avatarController](
                                    const rose::core::RoseActivity activity)
                                {
                                    avatarController.handleActivity(
                                        activity);
                                };


                            // -------------------------------------------------
                            // Model streaming -> UI
                            // -------------------------------------------------
                            //
                            // The callback lends a string_view only for this call.
                            // ChatBridge therefore receives an owned std::string.

                            const rose::model::ModelTextCallback onText =
                                [&chatBridge,
                                 &responseStarted](
                                    const std::string_view text)
                                {
                                    if (text.empty())
                                    {
                                        return;
                                    }

                                    if (!responseStarted)
                                    {
                                        chatBridge.postEvent(
                                            rose::ui::ChatEvent{
                                                .type =
                                                    rose::ui::ChatEventType::
                                                    AssistantStarted
                                            });

                                        responseStarted = true;
                                    }

                                    chatBridge.postEvent(
                                        rose::ui::ChatEvent{
                                            .type =
                                                rose::ui::ChatEventType::
                                                AssistantText,

                                            .text =
                                                std::string{
                                                    text
                                                }
                                        });
                                };


                            // -------------------------------------------------
                            // Generate normal Rose response
                            // -------------------------------------------------

                            const rose::model::ModelResponse response =
                                roseCore.processMessage(
                                    input,
                                    transientContext,
                                    onText,
                                    onActivity);

                            // Defensive fallback for providers that return only a
                            // final response and never invoke the streaming callback.
                            if (!responseStarted)
                            {
                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            AssistantStarted
                                    });

                                if (!response.text.empty())
                                {
                                    chatBridge.postEvent(
                                        rose::ui::ChatEvent{
                                            .type =
                                                rose::ui::ChatEventType::
                                                AssistantText,

                                            .text =
                                                response.text
                                        });
                                }
                            }

                            // AssistantFinished carries the canonical final text.
                            // The UI parses rich presentation only at this point.
                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::ChatEventType::
                                        AssistantFinished,

                                    .text =
                                        response.text
                                });

                            // Keep artifact ordering intuitive: Rose's final text
                            // arrives first, then the generated artifact card(s).
                            for (auto& artifact : pendingArtifacts)
                            {
                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::
                                            ArtifactReady,

                                        .artifact =
                                            std::move(artifact)
                                    });
                            }

                            std::cout
                                << "\n\n"
                                << "[Generated tokens: "
                                << response.generatedTokens
                                << "]\n"
                                << "[Finish reason: "
                                << (
                                    response.finishReason
                                    == rose::model::ModelFinishReason::TokenLimit
                                    ? "Token limit"
                                    : "End of generation")
                                << "]\n\n";
                        }
                        catch (const std::exception& exception)
                        {
                            avatarController.handleActivity(
                                rose::core::RoseActivity::Confused);

                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::ChatEventType::Error,

                                    .text =
                                        std::string{
                                            "Rose could not complete that request: "
                                        }
                                        + exception.what()
                                });

                            // No break: Rose remains alive for the next request.
                        }
                    }


                    avatarController.handleActivity(
                        rose::core::RoseActivity::Idle);
                }
                catch (...)
                {
                    // Only worker initialization/lifetime failures belong here.
                    workerException =
                        std::current_exception();

                    avatarController.handleActivity(
                        rose::core::RoseActivity::Confused);

                    chatBridge.postEvent(
                        rose::ui::ChatEvent{
                            .type =
                                rose::ui::ChatEventType::Error,

                            .text =
                                "Rose's conversation worker stopped unexpectedly."
                        });
                }

                conversationFinished.store(
                    true,
                    std::memory_order_release);
            }
        };


        // =====================================================================
        // SDL main thread
        // =====================================================================
        //
        // Only this thread consumes SDL's event queue and renders SDL resources.

        bool avatarWindowOpen{
            true
        };

        bool chatWindowOpen{
            true
        };

        while (
            !conversationFinished.load(
                std::memory_order_acquire))
        {
            SDL_Event event{};

            while (SDL_PollEvent(
                &event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    chatBridge.requestShutdown();
                    avatarWindowOpen = false;
                    chatWindowOpen = false;
                    continue;
                }

                if (avatarWindowOpen)
                {
                    avatarWindowOpen =
                        avatar.handleEvent(
                            event);
                }

                if (chatWindowOpen)
                {
                    const bool stillOpen =
                        chatWindow.handleEvent(
                            event);

                    if (!stillOpen)
                    {
                        chatWindowOpen = false;

                        // For the current development UI, closing chat shuts Rose
                        // down. This can change once Rose becomes tray/presence-first.
                        chatBridge.requestShutdown();
                    }
                }
            }


            if (chatWindowOpen)
            {
                chatWindow.update();
            }

            if (avatarWindowOpen)
            {
                avatar.render();
            }

            if (chatWindowOpen)
            {
                chatWindow.render();
            }

            // Approximately 60 Hz presentation loop. Inference is on the worker.
            std::this_thread::sleep_for(
                std::chrono::milliseconds{
                    16
                });
        }


        // =====================================================================
        // Deterministic shutdown
        // =====================================================================

        if (conversationWorker.joinable())
        {
            conversationWorker.join();
        }

        if (workerException)
        {
            std::rethrow_exception(
                workerException);
        }

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Fatal Rose error: "
            << exception.what()
            << '\n';

        return 1;
    }
}
