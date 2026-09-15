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
    // TextEditHistory
    // -------------------------------------------------------------------------
    //
    // Bounded delta history for one editable UTF-8 byte string.
    //
    // The owner keeps the canonical text. History stores only the changed byte
    // ranges needed to undo/redo an edit:
    //
    //     start
    //     removed bytes
    //     inserted bytes
    //     cursor/selection state before and after
    //
    // This deliberately avoids full-string snapshots. Editing one byte in a large
    // composer therefore does not duplicate the entire composer in history.
    //
    // All offsets are byte offsets because Rose's SDL_ttf editor already uses
    // UTF-8 byte offsets everywhere else.
    class TextEditHistory final
    {
    public:
        enum class Kind
        {
            Typing,
            Backspace,
            DeleteForward,
            Paste,
            Cut,
            Other
        };


        struct SelectionState
        {
            std::size_t cursorByteOffset{ 0 };
            std::optional<std::size_t> anchorByteOffset;

            bool operator==(
                const SelectionState&) const = default;
        };


        explicit TextEditHistory(
            const std::size_t maximumEntries = 256,
            const std::size_t maximumRetainedBytes =
                8u * 1024u * 1024u)
            : maximumEntries_{
                maximumEntries
            }
            , maximumRetainedBytes_{
                maximumRetainedBytes
            }
        {
        }


        [[nodiscard]]
        bool canUndo() const noexcept
        {
            return !undo_.empty();
        }


        [[nodiscard]]
        bool canRedo() const noexcept
        {
            return !redo_.empty();
        }


        [[nodiscard]]
        std::size_t retainedBytes() const noexcept
        {
            return retainedBytes_;
        }


        [[nodiscard]]
        std::size_t undoEntryCount() const noexcept
        {
            return undo_.size();
        }


        void clear() noexcept
        {
            undo_.clear();
            redo_.clear();

            retainedBytes_ =
                0;

            coalescingOpen_ =
                false;
        }


        // Cursor movement, mouse selection, Select All, etc. should end the current
        // typing/backspace group even though they do not themselves create history.
        void breakCoalescing() noexcept
        {
            coalescingOpen_ =
                false;
        }


        void record(
            const Kind kind,
            const std::size_t startByteOffset,
            std::string removedText,
            std::string insertedText,
            const SelectionState before,
            const SelectionState after)
        {
            if (
                removedText.empty()
                && insertedText.empty())
            {
                return;
            }


            Entry entry{
                .kind = kind,
                .startByteOffset = startByteOffset,
                .removedText = std::move(removedText),
                .insertedText = std::move(insertedText),
                .before = before,
                .after = after
            };


            const std::size_t entryBytes =
                retainedTextBytes(
                    entry);


            // Strict memory bound. If one individual edit is larger than the whole
            // history budget, keep the edit itself but deliberately make it
            // non-undoable rather than violating the configured cap.
            if (
                maximumRetainedBytes_ == 0
                || entryBytes > maximumRetainedBytes_)
            {
                clear();
                return;
            }


            clearRedo();


            if (
                coalescingOpen_
                && !undo_.empty()
                && tryCoalesce(
                    undo_.back(),
                    entry))
            {
                coalescingOpen_ =
                    isCoalescible(
                        kind);

                trimToBounds();
                return;
            }


            undo_.push_back(
                std::move(entry));

            retainedBytes_ +=
                entryBytes;

            coalescingOpen_ =
                isCoalescible(
                    kind);

            trimToBounds();
        }


        [[nodiscard]]
        std::optional<SelectionState> undo(
            std::string& text)
        {
            if (undo_.empty())
            {
                return std::nullopt;
            }


            Entry entry =
                std::move(
                    undo_.back());

            undo_.pop_back();


            if (!applyUndo(
                text,
                entry))
            {
                // If canonical text somehow diverged from history, stale edits are
                // unsafe to replay. Drop the history instead of guessing.
                clear();
                return std::nullopt;
            }


            redo_.push_back(
                std::move(entry));

            coalescingOpen_ =
                false;

            return
                redo_.back().before;
        }


        [[nodiscard]]
        std::optional<SelectionState> redo(
            std::string& text)
        {
            if (redo_.empty())
            {
                return std::nullopt;
            }


            Entry entry =
                std::move(
                    redo_.back());

            redo_.pop_back();


            if (!applyRedo(
                text,
                entry))
            {
                clear();
                return std::nullopt;
            }


            undo_.push_back(
                std::move(entry));

            coalescingOpen_ =
                false;

            return
                undo_.back().after;
        }


    private:
        struct Entry
        {
            Kind kind{ Kind::Other };

            std::size_t startByteOffset{ 0 };

            std::string removedText;
            std::string insertedText;

            SelectionState before;
            SelectionState after;
        };


        [[nodiscard]]
        static bool isCoalescible(
            const Kind kind) noexcept
        {
            return
                kind == Kind::Typing
                || kind == Kind::Backspace
                || kind == Kind::DeleteForward;
        }


        [[nodiscard]]
        static std::size_t retainedTextBytes(
            const Entry& entry) noexcept
        {
            return
                entry.removedText.size()
                + entry.insertedText.size();
        }


        void clearRedo() noexcept
        {
            for (const Entry& entry : redo_)
            {
                retainedBytes_ -=
                    retainedTextBytes(
                        entry);
            }

            redo_.clear();
        }


        [[nodiscard]]
        bool tryCoalesce(
            Entry& previous,
            const Entry& current)
        {
            if (
                previous.kind != current.kind
                || !isCoalescible(
                    current.kind)
                || previous.after != current.before)
            {
                return false;
            }


            const std::size_t previousBytes =
                retainedTextBytes(
                    previous);

            Entry merged =
                previous;


            switch (current.kind)
            {
            case Kind::Typing:
                // Sequential typing advances to the end of the text inserted by
                // the previous event. The first event may also have replaced a
                // selection, so retain previous.removedText unchanged.
                if (
                    current.startByteOffset
                    != previous.startByteOffset
                        + previous.insertedText.size()
                    || !current.removedText.empty())
                {
                    return false;
                }

                merged.insertedText +=
                    current.insertedText;

                break;


            case Kind::Backspace:
                // Repeated Backspace walks left. Prepend newly removed bytes so the
                // stored removedText remains in original left-to-right order.
                if (
                    current.startByteOffset
                        + current.removedText.size()
                    != previous.startByteOffset
                    || !current.insertedText.empty())
                {
                    return false;
                }

                merged.startByteOffset =
                    current.startByteOffset;

                merged.removedText =
                    current.removedText
                    + merged.removedText;

                break;


            case Kind::DeleteForward:
                // Repeated Delete stays at the same caret position and consumes
                // bytes to the right.
                if (
                    current.startByteOffset
                    != previous.startByteOffset
                    || !current.insertedText.empty())
                {
                    return false;
                }

                merged.removedText +=
                    current.removedText;

                break;


            default:
                return false;
            }


            merged.after =
                current.after;


            const std::size_t mergedBytes =
                retainedTextBytes(
                    merged);


            if (mergedBytes > maximumRetainedBytes_)
            {
                return false;
            }


            previous =
                std::move(
                    merged);

            retainedBytes_ -=
                previousBytes;

            retainedBytes_ +=
                mergedBytes;

            return true;
        }


        [[nodiscard]]
        static bool rangeMatches(
            const std::string& text,
            const std::size_t start,
            const std::string_view expected) noexcept
        {
            return
                start <= text.size()
                && expected.size()
                    <= text.size() - start
                && std::string_view{
                    text.data() + start,
                    expected.size()
                }
                == expected;
        }


        [[nodiscard]]
        static bool applyUndo(
            std::string& text,
            const Entry& entry)
        {
            if (!rangeMatches(
                text,
                entry.startByteOffset,
                entry.insertedText))
            {
                return false;
            }


            text.replace(
                entry.startByteOffset,
                entry.insertedText.size(),
                entry.removedText);

            return true;
        }


        [[nodiscard]]
        static bool applyRedo(
            std::string& text,
            const Entry& entry)
        {
            if (!rangeMatches(
                text,
                entry.startByteOffset,
                entry.removedText))
            {
                return false;
            }


            text.replace(
                entry.startByteOffset,
                entry.removedText.size(),
                entry.insertedText);

            return true;
        }


        void trimToBounds() noexcept
        {
            while (
                !undo_.empty()
                && (
                    undo_.size() > maximumEntries_
                    || retainedBytes_
                        > maximumRetainedBytes_))
            {
                retainedBytes_ -=
                    retainedTextBytes(
                        undo_.front());

                undo_.pop_front();
            }
        }


        std::size_t maximumEntries_{ 0 };
        std::size_t maximumRetainedBytes_{ 0 };

        std::deque<Entry> undo_;
        std::deque<Entry> redo_;

        std::size_t retainedBytes_{ 0 };

        bool coalescingOpen_{ false };
    };

} // namespace rose::ui
