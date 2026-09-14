#pragma once

#include "artifacts/Artifact.h"

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


    struct ToolResult
    {
        bool success{ true };
        std::string message;
        std::vector<artifacts::Artifact> artifacts;
    };

} // namespace rose::tools
