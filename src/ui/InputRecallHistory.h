#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>


namespace rose::ui
{

    // -------------------------------------------------------------------------
    // InputRecallHistory
    // -------------------------------------------------------------------------
    //
    // Session-local history of submitted composer text.
    //
    // This is intentionally separate from TextEditHistory:
    //
    //   TextEditHistory   -> undo/redo inside the current draft
    //   InputRecallHistory -> recall previously submitted prompts
    //
    // The canonical composer string remains owned by SdlChatWindow. This object
    // only owns bounded copies of previously submitted prompts plus one temporary
    // draft while the user is browsing backward through history.
    class InputRecallHistory final
    {
    public:
        explicit InputRecallHistory(
            const std::size_t maximumEntries = 128,
            const std::size_t maximumRetainedBytes =
                2u * 1024u * 1024u)
            : maximumEntries_{
                maximumEntries
            }
            , maximumRetainedBytes_{
                maximumRetainedBytes
            }
        {
        }


        [[nodiscard]]
        bool empty() const noexcept
        {
            return entries_.empty();
        }


        [[nodiscard]]
        bool browsing() const noexcept
        {
            return browsingIndex_.has_value();
        }


        [[nodiscard]]
        std::size_t entryCount() const noexcept
        {
            return entries_.size();
        }


        [[nodiscard]]
        std::size_t retainedBytes() const noexcept
        {
            return retainedBytes_;
        }


        void clear() noexcept
        {
            entries_.clear();

            retainedBytes_ =
                0;

            cancelBrowsing();
        }


        // Record one submitted prompt.
        //
        // Adjacent duplicates are suppressed. Repeating an older command after
        // other commands is still meaningful and is therefore retained.
        void recordSubmitted(
            const std::string_view text)
        {
            cancelBrowsing();


            if (
                text.empty()
                || maximumEntries_ == 0
                || maximumRetainedBytes_ == 0)
            {
                return;
            }


            if (
                !entries_.empty()
                && entries_.back() == text)
            {
                return;
            }


            // A single prompt larger than the entire recall budget remains a valid
            // submission, but it is not retained for future recall.
            if (text.size() > maximumRetainedBytes_)
            {
                return;
            }


            entries_.emplace_back(
                text);

            retainedBytes_ +=
                text.size();


            trimToBounds();
        }


        // Enter history browsing (if necessary) and move one entry older.
        //
        // `currentDraft` is captured exactly once when browsing begins. Moving
        // forward past the newest retained entry restores that draft.
        [[nodiscard]]
        std::optional<std::string_view> older(
            const std::string_view currentDraft)
        {
            if (entries_.empty())
            {
                return std::nullopt;
            }


            if (!browsingIndex_.has_value())
            {
                savedDraft_.assign(
                    currentDraft);

                browsingIndex_ =
                    entries_.size();
            }


            if (*browsingIndex_ > 0)
            {
                --(*browsingIndex_);
            }


            return entries_[
                *browsingIndex_];
        }


        // Move one entry newer. Moving beyond the newest retained prompt restores
        // the draft that was present before browsing began and exits browse mode.
        [[nodiscard]]
        std::optional<std::string_view> newer()
        {
            if (!browsingIndex_.has_value())
            {
                return std::nullopt;
            }


            const std::size_t current =
                *browsingIndex_;


            if (current + 1 < entries_.size())
            {
                ++(*browsingIndex_);

                return entries_[
                    *browsingIndex_];
            }


            restoredDraftBuffer_ =
                std::move(
                    savedDraft_);

            browsingIndex_.reset();

            return restoredDraftBuffer_;
        }


        // Any real edit to a recalled prompt makes that text the user's new draft.
        // Abandon the old saved-draft/navigation position, but keep submitted
        // history intact for the next Ctrl+Up.
        void cancelBrowsing() noexcept
        {
            browsingIndex_.reset();

            savedDraft_.clear();
            restoredDraftBuffer_.clear();
        }


    private:
        void trimToBounds() noexcept
        {
            while (
                !entries_.empty()
                && (
                    entries_.size() > maximumEntries_
                    || retainedBytes_
                        > maximumRetainedBytes_))
            {
                retainedBytes_ -=
                    entries_.front().size();

                entries_.pop_front();
            }
        }


        std::size_t maximumEntries_{ 0 };
        std::size_t maximumRetainedBytes_{ 0 };

        // Oldest -> newest.
        std::deque<std::string> entries_;

        std::size_t retainedBytes_{ 0 };

        // Index into entries_ while browsing. When browsing begins it is
        // initialized one-past-the-end and then decremented to the newest entry.
        std::optional<std::size_t> browsingIndex_;

        // Draft that existed immediately before the first Ctrl+Up.
        std::string savedDraft_;

        // `newer()` returns string_view, so when leaving browse mode we move the
        // saved draft here to keep the returned view alive through the caller's
        // immediate assignment.
        std::string restoredDraftBuffer_;
    };

} // namespace rose::ui
