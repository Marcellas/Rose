#include "agent/UserTaskContinuation.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    void require(
        const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            std::cerr
                << "UserTaskContinuation test failed: "
                << message
                << '\n';
            std::exit(1);
        }
    }
}

int main()
{
    rose::agent::UserTaskContinuationState state;
    const rose::agent::AgentRunProvenance provenance{
        .projectId = "project-rose",
        .discussionId = "discussion-1"
    };

    rose::agent::rememberUserTaskContinuation(
        state,
        "Create a .cpp source file at C:\\Users\\chris\\Desktop and design a program.",
        provenance);

    require(
        rose::agent::shouldReuseUserTaskContinuation(
            state,
            "Scientific, any object with parameters available to be entered.",
            provenance),
        "nearby user clarification should retain unresolved tool intent");

    const std::string combined =
        rose::agent::appendUserTaskContinuationText(
            state.userText,
            "Scientific, any object with parameters available to be entered.");

    require(
        combined.find("Create a .cpp source file") != std::string::npos
            && combined.find("Scientific, any object") != std::string::npos,
        "continuation text should contain only the prior/current user fragments");

    require(
        !rose::agent::shouldReuseUserTaskContinuation(
            state,
            "Never mind, cancel that request.",
            provenance),
        "explicit cancellation should clear continuation eligibility");

    require(
        !rose::agent::shouldReuseUserTaskContinuation(
            state,
            "Scientific inputs.",
            rose::agent::AgentRunProvenance{
                .projectId = "project-rose",
                .discussionId = "discussion-2"
            }),
        "continuation must not cross discussion provenance");

    for (std::size_t index{ 1 };
         index < rose::agent::maximumUserTaskContinuationTurns;
         ++index)
    {
        rose::agent::rememberUserTaskContinuation(
            state,
            state.userText,
            provenance);
    }

    require(
        !rose::agent::shouldReuseUserTaskContinuation(
            state,
            "Continue.",
            provenance),
        "continuation should expire at the bounded turn ceiling");

    std::cout << "Rose UserTaskContinuation tests: PASS\n";
    return 0;
}
