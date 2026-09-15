#include "TextEditHistory.h"

#include <cassert>
#include <iostream>
#include <optional>
#include <string>

using rose::ui::TextEditHistory;

int main()
{
    TextEditHistory history{
        16,
        1024
    };

    std::string text;

    TextEditHistory::SelectionState state{
        .cursorByteOffset = 0,
        .anchorByteOffset = std::nullopt
    };

    // Sequential typing should coalesce into one undo group.
    history.record(
        TextEditHistory::Kind::Typing,
        0,
        "",
        "a",
        state,
        { 1, std::nullopt });

    text = "a";
    state = { 1, std::nullopt };

    history.record(
        TextEditHistory::Kind::Typing,
        1,
        "",
        "b",
        state,
        { 2, std::nullopt });

    text = "ab";
    state = { 2, std::nullopt };

    assert(history.undoEntryCount() == 1);

    auto undoState =
        history.undo(text);

    assert(undoState.has_value());
    assert(text.empty());
    assert(undoState->cursorByteOffset == 0);
    assert(history.canRedo());

    auto redoState =
        history.redo(text);

    assert(redoState.has_value());
    assert(text == "ab");
    assert(redoState->cursorByteOffset == 2);

    // Break the typing group, then replace one selected byte.
    history.breakCoalescing();

    TextEditHistory::SelectionState replaceBefore{
        .cursorByteOffset = 2,
        .anchorByteOffset = 1
    };

    text = "ab";

    // Replace "b" with "X".
    text.replace(1, 1, "X");

    history.record(
        TextEditHistory::Kind::Typing,
        1,
        "b",
        "X",
        replaceBefore,
        { 2, std::nullopt });

    undoState =
        history.undo(text);

    assert(undoState.has_value());
    assert(text == "ab");
    assert(undoState->anchorByteOffset.has_value());
    assert(*undoState->anchorByteOffset == 1);

    redoState =
        history.redo(text);

    assert(redoState.has_value());
    assert(text == "aX");

    // New edit after undo must invalidate the redo branch.
    undoState =
        history.undo(text);

    assert(text == "ab");

    text += "!";
    history.record(
        TextEditHistory::Kind::Typing,
        2,
        "",
        "!",
        { 2, std::nullopt },
        { 3, std::nullopt });

    assert(!history.canRedo());

    // Repeated backspace should coalesce and restore original order.
    history.breakCoalescing();

    text = "abcd";
    history.clear();

    // Remove d.
    text.erase(3, 1);
    history.record(
        TextEditHistory::Kind::Backspace,
        3,
        "d",
        "",
        { 4, std::nullopt },
        { 3, std::nullopt });

    // Remove c.
    text.erase(2, 1);
    history.record(
        TextEditHistory::Kind::Backspace,
        2,
        "c",
        "",
        { 3, std::nullopt },
        { 2, std::nullopt });

    assert(text == "ab");
    assert(history.undoEntryCount() == 1);

    undoState =
        history.undo(text);

    assert(undoState.has_value());
    assert(text == "abcd");

    std::cout << "TextEditHistory tests passed.\n";
}
