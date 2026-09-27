#include "ui/ChatBridge.h"

#include <cassert>
#include <iostream>
#include <string>
#include <utility>
#include <variant>

int main()
{
    rose::ui::ChatBridge bridge;

    bridge.submitUserMessage("hello");
    bridge.submitUiCommand(
        rose::ui::UiWorkerCommand{
            .type = rose::ui::UiWorkerCommandType::ActivateDiscussion,
            .targetId = "d-test"
        });

    auto first = bridge.waitForWorkerRequest();
    assert(first.has_value());
    assert(std::holds_alternative<rose::input::UserSubmission>(*first));
    assert(std::get<rose::input::UserSubmission>(*first).text == "hello");

    auto second = bridge.waitForWorkerRequest();
    assert(second.has_value());
    assert(std::holds_alternative<rose::ui::UiWorkerCommand>(*second));
    assert(
        std::get<rose::ui::UiWorkerCommand>(*second).targetId
        == "d-test");

    rose::workspace::WorkspaceSnapshot snapshot;
    rose::workspace::DiscussionRecord discussion;
    discussion.id = "d-test";
    discussion.title = "Test Discussion";
    snapshot.discussions.push_back(
        std::move(discussion));
    snapshot.activeDiscussionId = "d-test";

    bridge.publishWorkspaceSnapshot(snapshot);

    auto update = bridge.workspaceSnapshotSince(0);
    assert(update.has_value());
    assert(update->version != 0);
    assert(update->snapshot.activeDiscussionId == "d-test");
    assert(update->snapshot.discussions.size() == 1);

    assert(!bridge.workspaceSnapshotSince(update->version).has_value());

    rose::ui::ChatEvent transcriptEvent;
    transcriptEvent.type =
        rose::ui::ChatEventType::ConversationReplaced;
    transcriptEvent.transcriptTurns.push_back(
        rose::ui::ChatTranscriptTurn{
            .userText = "question",
            .assistantText = "answer"
        });

    bridge.postEvent(
        std::move(transcriptEvent));

    auto event = bridge.tryPopEvent();
    assert(event.has_value());
    assert(event->type == rose::ui::ChatEventType::ConversationReplaced);
    assert(event->transcriptTurns.size() == 1);
    assert(event->transcriptTurns.front().assistantText == "answer");

    bridge.requestShutdown();
    assert(!bridge.waitForWorkerRequest().has_value());

    std::cout << "Rose ChatBridge workspace UI tests: PASS\n";
    return 0;
}
