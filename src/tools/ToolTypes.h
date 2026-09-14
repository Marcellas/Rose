#pragma once

#include "artifacts/Artifact.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace rose::tools
{

    // Broad risk categories used by the future permission/policy layer.
    // The registry describes risk; it does not decide whether execution is allowed.
    enum class ToolRisk
    {
        ReadOnly,
        LocalWrite,
        Destructive,
        ExternalEffect
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
        std::vector<ToolParameterDescriptor> parameters;
    };


    // Generic request envelope used at the ToolRegistry boundary.
    //
    // Values remain strings for this first checkpoint so ToolRegistry itself does
    // not need to know about JSON or a particular model's tool-call protocol.
    // Individual tools validate and convert their own arguments.
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
