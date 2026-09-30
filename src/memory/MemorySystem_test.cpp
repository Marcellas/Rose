#include "memory/ConversationMemoryCandidateTracker.h"
#include "memory/FileMemoryStore.h"
#include "memory/LexicalMemoryRetriever.h"
#include "memory/MemoryFactNormalizer.h"
#include "memory/MemoryRepository.h"
#include "persistence/IConversationStore.h"
#include "tools/RememberMemoryTool.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    struct TestConversationStore final : rose::persistence::IConversationStore
    {
        std::vector<rose::persistence::StoredConversationTurn> turns;
        std::vector<rose::persistence::StoredConversationTurn> loadTurns() override
        {
            return turns;
        }
        void appendTurn(std::string_view, std::string_view) override {}
        void clear() override { turns.clear(); }
    };

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
    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / "rose-memory-system-test.rosemem";

    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    try
    {
        {
            rose::memory::FileMemoryStore store{ path };
            rose::memory::MemoryRepository repository{ store };

            const auto first = repository.remember(
                "My dog's name is Fido.",
                rose::memory::MemoryKind::ExplicitUser,
                "unit-test");

            require(first.created, "first memory should be created");
            require(first.record.id == 1, "first memory id should be 1");

            const auto duplicate = repository.remember(
                "my DOG'S name is fido.",
                rose::memory::MemoryKind::ExplicitUser,
                "unit-test");

            require(!duplicate.created, "case-insensitive duplicate should be suppressed");
            require(repository.size() == 1, "duplicate should not grow repository");

            (void)repository.remember(
                "I prefer dark roast coffee.",
                rose::memory::MemoryKind::ExplicitUser,
                "unit-test");

            require(
                repository.records().back().semanticKey == "preference:coffee",
                "coffee preference should receive a semantic key");

            rose::memory::LexicalMemoryRetriever retriever{ repository };

            const auto dogMatches = retriever.retrieve(
                "What is my dog's name?");

            require(!dogMatches.empty(), "dog query should retrieve a memory");
            require(
                dogMatches.front().record.content == "My dog's name is Fido.",
                "dog query should rank the dog memory first");

            const auto unrelated = retriever.retrieve(
                "What is the weather tomorrow?");

            require(unrelated.empty(), "unrelated query should not retrieve memory");
        }

        // Prove durability independently from the original repository instance.
        {
            rose::memory::FileMemoryStore store{ path };
            rose::memory::MemoryRepository repository{ store };

            require(repository.size() == 2, "memories should survive repository restart");
            require(repository.activeSize() == 2, "both initial memories should be active");

            rose::memory::LexicalMemoryRetriever retriever{ repository };
            const auto coffeeMatches = retriever.retrieve(
                "Which coffee do I prefer?");

            require(!coffeeMatches.empty(), "coffee query should retrieve persisted memory");
            require(
                coffeeMatches.front().record.content
                    == "I prefer dark roast coffee.",
                "persisted coffee memory should rank first");

            rose::memory::ConversationMemoryCandidateTracker candidateTracker{
                repository
            };

            // Semantic paraphrases of an already-durable value should be
            // recognized and discarded rather than becoming duplicate candidates.
            const auto paraphraseObservation =
                candidateTracker.observeUserMessage(
                    "Dark roast coffee is my preference.");

            require(
                paraphraseObservation.candidatesDiscardedAsDurable == 1,
                "semantic paraphrase should match the durable preference");
            require(
                candidateTracker.candidates().empty(),
                "durable semantic equivalent should not remain temporary");

            // A changed value for the same semantic slot is allowed to become a
            // candidate, then conservatively promote after a later repetition.
            const auto changedFirst =
                candidateTracker.observeUserMessage(
                    "Medium roast coffee is my preference.");

            require(
                changedFirst.candidatesAdded == 1,
                "changed preference should become a temporary candidate");

            const auto changedSecond =
                candidateTracker.observeUserMessage(
                    "I prefer medium roast coffee.");

            require(
                changedSecond.candidatesPromoted == 1,
                "repeated paraphrase of changed preference should promote");
            require(
                repository.size() == 3,
                "changed preference should append one durable journal record");
            require(
                repository.activeSize() == 2,
                "new preference should supersede the older preference");
            require(
                repository.records()[1].supersededById
                    == repository.records()[2].id,
                "old coffee preference should point at its replacement");

            const auto updatedCoffeeMatches = retriever.retrieve(
                "Which coffee do I prefer?");

            require(!updatedCoffeeMatches.empty(), "updated coffee preference should retrieve");
            require(
                updatedCoffeeMatches.front().record.content
                    == "Medium roast coffee is my preference.",
                "retrieval should ignore the superseded coffee preference");

            // One ordinary stable-looking statement becomes only temporary.
            const auto firstObservation =
                candidateTracker.observeUserMessage(
                    "I use Visual Studio for Rose.");

            require(
                firstObservation.candidatesAdded == 1,
                "first stable statement should create one temporary candidate");
            require(
                candidateTracker.candidates().size() == 1,
                "single observation must remain temporary");
            require(
                repository.size() == 3,
                "single automatic observation must not write long-term memory");

            // Repeating the same sentence twice inside one submission is still
            // only one user-turn observation and must not auto-promote.
            rose::memory::ConversationMemoryCandidateTracker sameTurnTracker{
                repository
            };

            const auto sameTurnObservation =
                sameTurnTracker.observeUserMessage(
                    "I prefer tea. I prefer tea.");

            require(
                sameTurnObservation.candidatesAdded == 1
                    && sameTurnObservation.candidatesPromoted == 0,
                "duplicate text in one turn must count as one observation");
            require(
                sameTurnTracker.candidates().size() == 1,
                "same-turn duplicate must remain temporary");

            // Repetition is the conservative automatic promotion signal.
            const auto secondObservation =
                candidateTracker.observeUserMessage(
                    "I use Visual Studio for Rose.");

            require(
                secondObservation.candidatesReinforced == 1,
                "repeated statement should reinforce its temporary candidate");
            require(
                secondObservation.candidatesPromoted == 1,
                "repeated statement should promote to durable memory");
            require(
                candidateTracker.candidates().empty(),
                "promoted candidate should leave temporary memory");
            require(
                repository.size() == 4,
                "automatic promotion should append one durable record");
            require(
                repository.records().back().kind
                    == rose::memory::MemoryKind::ConversationDerived,
                "automatic promotion should use conversation-derived kind");

            // A one-off candidate may be promoted explicitly by the user/developer.
            (void)candidateTracker.observeUserMessage(
                "Rose should remain local-first.");

            require(
                candidateTracker.candidates().size() == 1,
                "project preference should be visible as a temporary candidate");

            const std::uint64_t candidateId =
                candidateTracker.candidates().front().id;

            const auto manualPromotion =
                candidateTracker.promoteCandidate(candidateId);

            require(
                manualPromotion.has_value()
                    && manualPromotion->created,
                "manual candidate promotion should persist the candidate");
            require(
                repository.size() == 5,
                "manual promotion should grow durable memory");

            // /clear calls this boundary: temporary candidates disappear while
            // durable records remain untouched.
            (void)candidateTracker.observeUserMessage(
                "I prefer compact diagnostic logs.");

            require(
                !candidateTracker.candidates().empty(),
                "temporary candidate should exist before clear");

            candidateTracker.clearTemporaryMemory();

            require(
                candidateTracker.candidates().empty(),
                "temporary clear should remove session candidates");
            require(
                repository.size() == 5,
                "temporary clear must not delete durable memories");
        }

        // Semantic active/superseded state is reconstructed from the existing
        // v0.1 append-only journal; no persistence migration is required.
        {
            rose::memory::FileMemoryStore store{ path };
            rose::memory::MemoryRepository repository{ store };

            require(repository.size() == 5, "restart should preserve journal history");
            require(repository.activeSize() == 4, "restart should reconstruct supersession");

            rose::memory::LexicalMemoryRetriever retriever{ repository };
            const auto matches = retriever.retrieve("coffee preference");

            require(!matches.empty(), "coffee preference should remain retrievable after restart");
            require(
                matches.front().record.content
                    == "Medium roast coffee is my preference.",
                "restart retrieval should use only the current preference");
        }

        {
            rose::memory::FileMemoryStore store{ path };
            rose::memory::MemoryRepository repository{ store };
            TestConversationStore discussion;
            discussion.turns.push_back({
                "Timeline:\n- October 21, 2023: LIMDU ended.\n"
                "- December 1, 2023: Collision.\n"
                "- June 19, 2025: Retirement date.\n",
                "Invented assistant conclusion about the six files.", 0
            });
            rose::tools::RememberMemoryTool tool{ repository, &discussion };
            const rose::tools::ToolRequest request{
                .toolId = "remember_memory",
                .arguments = {{ "content",
                    "Remember the entire history of the timeline that was provided "
                    "and a summary about the six files." }}
            };
            const auto result = tool.execute(request);
            require(result.success && result.message.find("3 distinct")
                != std::string::npos, "timeline should report actual coverage");
            const std::string& saved = repository.records().back().content;
            require(saved.find("December 1, 2023: Collision")
                != std::string::npos, "timeline facts must be saved");
            require(saved.find("Remember the entire history") == std::string::npos
                && saved.find("Invented assistant") == std::string::npos,
                "memory must not save the instruction or assistant conclusion");
            require(result.message.find("No six-file summary")
                != std::string::npos, "unread source files must not be catalogued");

            discussion.turns.clear();
            bool rejected{ false };
            try { (void)tool.execute({ "remember_memory",
                {{ "content", "Remember the entire history of this timeline." }} }); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "missing source timeline must not save a command");

            std::string longTimeline = "Timeline:\n";
            for (int day = 1; day <= 48; ++day)
                longTimeline += "- January " + std::to_string(day)
                    + ", 2040: Detailed event concerning the medical record, "
                      "the correspondence, and the board sequence for entry "
                    + std::to_string(day) + ".\n";
            discussion.turns.push_back({ longTimeline, "", 0 });
            const auto longResult = tool.execute({ "remember_memory",
                {{ "content", "Remember the entire timeline." }} });
            require(longResult.success && longResult.message.find("memory part(s)")
                != std::string::npos, "long timeline must be stored in parts");
            rose::memory::LexicalMemoryRetriever indexed{ repository };
            const auto recalled = indexed.retrieve("timeline correspondence 2040",
                { .maximumResults = 5, .maximumCombinedContentBytes = 4096,
                  .minimumScore = 0.20 });
            require(!recalled.empty() && recalled.front().record.content.size() < 4096,
                "long timeline parts must remain retrievable within the context budget");

            const auto compound = tool.execute({ "remember_memory",
                {{ "content",
                    "Read C:\\Reports\\large.pdf and submit facts to memory.\n"
                    "- December 1, 2023: Collision.\n"
                    "- April 24, 2024: First board decision.\n"
                    "- July 8, 2025: Retirement was backdated.\n" }} });
            require(compound.success, "a combined document and memory request should save its timeline");
            const std::string& compoundSaved = repository.records().back().content;
            require(compoundSaved.find("December 1, 2023: Collision") != std::string::npos
                && compoundSaved.find("July 8, 2025: Retirement was backdated") != std::string::npos,
                "the dated user entries must be preserved");
            require(compoundSaved.find("large.pdf") == std::string::npos
                && compoundSaved.find("submit facts to memory") == std::string::npos,
                "a document instruction must not be stored as a verified memory");
        }

        std::filesystem::remove(path, ignored);

        std::cout << "Rose MemorySystem tests: PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::filesystem::remove(path, ignored);

        std::cerr
            << "Rose MemorySystem tests: FAIL: "
            << exception.what()
            << '\n';

        return 1;
    }
}
