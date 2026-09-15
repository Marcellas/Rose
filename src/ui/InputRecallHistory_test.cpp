#include "InputRecallHistory.h"

#include <cassert>
#include <iostream>
#include <string>
#include <string_view>

using rose::ui::InputRecallHistory;

int main()
{
    InputRecallHistory history{
        3,
        64
    };

    history.recordSubmitted("one");
    history.recordSubmitted("two");
    history.recordSubmitted("three");

    assert(history.entryCount() == 3);
    assert(history.retainedBytes() == 11);

    // Adjacent duplicates are suppressed.
    history.recordSubmitted("three");
    assert(history.entryCount() == 3);

    // Current draft is captured once when browsing starts.
    auto value =
        history.older("draft");

    assert(value.has_value());
    assert(*value == "three");
    assert(history.browsing());

    value =
        history.older("this must not replace the saved draft");

    assert(value.has_value());
    assert(*value == "two");

    value =
        history.older("");

    assert(value.has_value());
    assert(*value == "one");

    // Oldest entry clamps.
    value =
        history.older("");

    assert(value.has_value());
    assert(*value == "one");

    value =
        history.newer();

    assert(value.has_value());
    assert(*value == "two");

    value =
        history.newer();

    assert(value.has_value());
    assert(*value == "three");

    // Moving newer past the newest entry restores the original draft and exits.
    value =
        history.newer();

    assert(value.has_value());
    assert(*value == "draft");
    assert(!history.browsing());

    // New edit behavior: abandon browsing without deleting submitted history.
    value =
        history.older("new draft");

    assert(value.has_value());
    assert(*value == "three");

    history.cancelBrowsing();

    assert(!history.browsing());
    assert(history.entryCount() == 3);

    // Count bound trims oldest.
    history.recordSubmitted("four");
    assert(history.entryCount() == 3);

    value =
        history.older("");

    assert(value.has_value());
    assert(*value == "four");

    value =
        history.older("");

    assert(value.has_value());
    assert(*value == "three");

    value =
        history.older("");

    assert(value.has_value());
    assert(*value == "two");

    // Clear removes all prompt history and browse state.
    history.clear();
    assert(history.empty());
    assert(!history.browsing());
    assert(history.retainedBytes() == 0);

    std::cout << "InputRecallHistory tests passed.\n";
}
