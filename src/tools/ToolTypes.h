#pragma once

#include "artifacts/Artifact.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rose::tools
{

    // Broad effect/risk category. Risk describes WHAT a tool can affect.
    enum class ToolRisk
    {
        ReadOnly,
        LocalWrite,
        Destructive,
        ExternalEffect
    };


    // Consent describes whether Rose policy may execute a proposed tool without
    // an additional confirmation interaction.
    //
    // Defaulting to RequiresConfirmation is deliberate: newly added tools fail
    // safe until their execution semantics are reviewed.
    enum class ToolConsent
    {
        AutoAllowed,
        RequiresConfirmation
    };


    enum class ToolValueType
    {
        String,
        Integer,
        Number,
        Boolean
    };


    struct ToolParameterDescriptor
    {
        std::string name;
        std::string description;
        ToolValueType type{ ToolValueType::String };
        bool required{ false };
    };


    struct ToolDescriptor
    {
        std::string id;
        std::string displayName;
        std::string description;
        ToolRisk risk{ ToolRisk::ReadOnly };
        ToolConsent consent{ ToolConsent::RequiresConfirmation };
        std::vector<ToolParameterDescriptor> parameters;
    };


    // Generic request envelope used at the ToolRegistry boundary.
    //
    // Values remain strings for this checkpoint so ToolRegistry does not depend
    // on JSON or a particular model provider's native tool-call protocol.
    struct ToolRequest
    {
        std::string toolId;
        std::unordered_map<std::string, std::string> arguments;
    };


    enum class ToolResponseMode
    {
        RequiresModelSynthesis,
        AuthoritativeCompletion
    };


    // Ephemeral, Rose-owned provenance for one exact source window read.
    //
    // The file contents themselves remain untrusted and stay in ToolResult::message.
    // This structure carries only coordinates plus a SHA-256 digest of the exact
    // logical preimage Rose observed, so a later line-range edit can prove it is
    // still patching that same source window without asking the control model to
    // echo the complete preimage back through its tiny routing protocol.
    struct SourceWindowEvidence
    {
        std::string path;
        std::size_t startLine{ 0 };
        std::size_t lineCount{ 0 };

        // Ordered SHA-256 hashes for each exact logical source line. These are
        // sufficient to derive provenance for any contiguous subrange without
        // retaining raw source bytes in trusted agent state.
        std::vector<std::string> lineSha256s;

        // Digest for the complete observed window.
        std::string sha256;
    };


    struct ToolResult
    {
        bool success{ true };
        std::string message;

        // Rose-owned structured metadata for the next agent control pass.
        //
        // IMPORTANT: tools may summarize/validate untrusted external output into
        // this field, but must never copy arbitrary raw output here. Agent routing
        // is allowed to trust the structure of this field in ways it does not trust
        // ordinary tool message text.
        std::string trustedMetadata{};

        // Optional typed source-window provenance. This is ephemeral agent-run
        // state only; later unrelated tool executions clear it.
        std::optional<SourceWindowEvidence> sourceWindowEvidence{};

        ToolResponseMode responseMode{
            ToolResponseMode::RequiresModelSynthesis
        };

        std::vector<artifacts::Artifact> artifacts;
    };

} // namespace rose::tools
