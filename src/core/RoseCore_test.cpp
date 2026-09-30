#include "core/RoseCore.h"
#include "logging/Logger.h"
#include "model/IModelProvider.h"
#include "persistence/IConversationStore.h"
#include "policy/ContentPolicy.h"

#include <stdexcept>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error{ message };
    }

    class Store final : public rose::persistence::IConversationStore
    {
    public:
        std::vector<rose::persistence::StoredConversationTurn> turns;
        auto loadTurns() -> std::vector<rose::persistence::StoredConversationTurn> override
        { return turns; }
        void appendTurn(std::string_view user, std::string_view assistant) override
        { turns.push_back({ std::string{ user }, std::string{ assistant }, 0 }); }
        void clear() override { turns.clear(); }
    };

    class LimitedProvider final : public rose::model::IModelProvider
    {
    public:
        int calls{ 0 };
        int capacity{ 8192 };
        rose::model::ModelContextUsage inspectContext(
            const rose::model::ModelRequest& request) const override
        {
            return { static_cast<int>(request.messages.size()) * 40,
                capacity, request.maxGeneratedTokens };
        }
        rose::model::ModelResponse generate(
            const rose::model::ModelRequest& request) override
        {
            if (calls++ == 0)
                return { "First part.", {}, 1536,
                    rose::model::ModelFinishReason::TokenLimit };
            check(request.messages.back().content.find("Continue the answer")
                != std::string::npos, "continuation prompt missing");
            return { "Second part.", {}, 30,
                rose::model::ModelFinishReason::EndOfGeneration };
        }
    };

    class EvidenceProvider final : public rose::model::IModelProvider
    {
    public:
        int evidenceCalls{ 0 };
        rose::model::ModelContextUsage inspectContext(
            const rose::model::ModelRequest& request) const override
        {
            std::size_t bytes{ 0 };
            for (const auto& message : request.messages)
                bytes += message.content.size();
            return { static_cast<int>(bytes / 4u), 3000,
                request.maxGeneratedTokens };
        }
        rose::model::ModelResponse generate(
            const rose::model::ModelRequest& request) override
        {
            if (request.messages.front().content.find("document")
                != std::string::npos)
            {
                ++evidenceCalls;
                return { "Condensed page-labelled evidence.", {}, 20,
                    rose::model::ModelFinishReason::EndOfGeneration };
            }
            check(request.messages.back().content.find(
                "Condensed page-labelled evidence") != std::string::npos,
                "final answer did not receive the reduced tool evidence");
            return { "Scoped answer from reviewed evidence.", {}, 30,
                rose::model::ModelFinishReason::EndOfGeneration };
        }
    };

    class ReviewProvider final : public rose::model::IModelProvider
    {
    public:
        int calls{ 0 };
        rose::model::ModelContextUsage inspectContext(
            const rose::model::ModelRequest& request) const override
        { return { 1000, 8192, request.maxGeneratedTokens }; }
        rose::model::ModelResponse generate(
            const rose::model::ModelRequest& request) override
        {
            ++calls;
            if (request.messages.front().content.starts_with("Privately check"))
            {
                check(request.maxGeneratedTokens == 256,
                    "private review must have a finite token budget");
                return { "Possible gap: source snippet is incomplete.", {}, 20,
                    rose::model::ModelFinishReason::EndOfGeneration };
            }
            check(request.messages.back().content.find("<rose_private_review>")
                != std::string::npos, "final synthesis missed private review");
            return { "The cited snippet is incomplete.", {}, 30,
                rose::model::ModelFinishReason::EndOfGeneration };
        }
    };
}

int main()
{
    rose::logging::Logger logger;
    rose::policy::ContentPolicy policy;
    Store store;
    auto provider = std::make_unique<LimitedProvider>();
    auto* raw = provider.get();
    rose::core::RoseCore core{ std::move(provider), logger, store, policy };
    std::string streamed;
    auto response = core.processMessage("Explain the full sequence", {},
        [&streamed](std::string_view text) { streamed += text; }, {});
    check(raw->calls == 2 && response.text == "First part.\nSecond part.",
        "token-limit reply should continue automatically");
    check(streamed == response.text && store.turns.size() == 1
        && store.turns[0].assistantText == response.text,
        "complete answer should stream and persist as one turn");

    Store smallStore;
    auto smallProvider = std::make_unique<LimitedProvider>();
    smallProvider->capacity = 1650; // first request fits; continuation does not
    auto* small = smallProvider.get();
    rose::core::RoseCore bounded{ std::move(smallProvider), logger, smallStore,
        policy };
    const auto paused = bounded.processMessage("Large reply");
    check(small->calls == 1 && paused.text.find("Response paused")
        != std::string::npos && smallStore.turns.size() == 1,
        "context-limited reply must tell the user and persist its partial text");
    Store evidenceStore;
    auto evidenceProvider = std::make_unique<EvidenceProvider>();
    auto* evidence = evidenceProvider.get();
    rose::core::RoseCore reviewing{ std::move(evidenceProvider), logger,
        evidenceStore, policy };
    const auto review = reviewing.processMessage("Review the named documents",
        std::string(30000, 'x'), {}, {});
    check(evidence->evidenceCalls > 0
        && review.text == "Scoped answer from reviewed evidence."
        && evidenceStore.turns.size() == 1,
        "large tool evidence should be reduced before final response");
    Store reviewStore;
    auto reviewProvider = std::make_unique<ReviewProvider>();
    auto* reviewingProvider = reviewProvider.get();
    rose::core::RoseCore reasoning{ std::move(reviewProvider), logger,
        reviewStore, policy };
    const auto reasoned = reasoning.processMessage("Analyze this result",
        "<rose_tool_observation>\ntool_id=search_online\nsuccess=true\n"
        "message=Snippet only\n</rose_tool_observation>", {}, {});
    check(reviewingProvider->calls == 2
        && reasoned.text == "The cited snippet is incomplete."
        && reviewStore.turns.size() == 1,
        "complex tool evidence should get exactly one bounded review pass");
    std::cout << "RoseCore continuation tests: PASS\n";
}
