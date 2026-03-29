#pragma once

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

#include "../Core/PatternProject.h"
#include "../Core/PatternProjectSerialization.h"

namespace bbg
{
class EditorHistoryController
{
public:
    static constexpr size_t maxHistoryEntries = 128;

    void observeProjectState(const PatternProject& project)
    {
        lastObservedProjectState = project;
    }

    const std::optional<PatternProject>& getLastObservedProjectState() const
    {
        return lastObservedProjectState;
    }

    template <typename ScheduleCommitFn>
    void pushProjectHistoryState(const PatternProject& before,
                                 const PatternProject& after,
                                 ScheduleCommitFn&& scheduleCommit)
    {
        lastObservedProjectState = after;

        if (suppressHistory || projectsEquivalent(before, after))
            return;

        if (!pendingHistoryBefore.has_value())
            pendingHistoryBefore = before;
        pendingHistoryAfter = after;

        if (pendingHistoryCommitScheduled)
            return;

        pendingHistoryCommitScheduled = true;
        std::forward<ScheduleCommitFn>(scheduleCommit)();
    }

    void commitPendingProjectHistoryState()
    {
        pendingHistoryCommitScheduled = false;
        if (!pendingHistoryBefore.has_value() || !pendingHistoryAfter.has_value())
            return;

        const auto before = *pendingHistoryBefore;
        const auto after = *pendingHistoryAfter;
        pendingHistoryBefore.reset();
        pendingHistoryAfter.reset();

        if (suppressHistory || projectsEquivalent(before, after))
            return;

        undoHistory.push_back(before);
        redoHistory.clear();

        if (undoHistory.size() > maxHistoryEntries)
            undoHistory.erase(undoHistory.begin(),
                              undoHistory.begin() + static_cast<std::ptrdiff_t>(undoHistory.size() - maxHistoryEntries));
    }

    bool performUndo(const PatternProject& current, PatternProject& snapshot)
    {
        if (undoHistory.empty())
            return false;

        snapshot = undoHistory.back();
        undoHistory.pop_back();
        redoHistory.push_back(current);
        suppressHistory = true;
        lastObservedProjectState = snapshot;
        return true;
    }

    bool performRedo(const PatternProject& current, PatternProject& snapshot)
    {
        if (redoHistory.empty())
            return false;

        snapshot = redoHistory.back();
        redoHistory.pop_back();
        undoHistory.push_back(current);
        suppressHistory = true;
        lastObservedProjectState = snapshot;
        return true;
    }

    void endSuppressedHistoryAction()
    {
        suppressHistory = false;
    }

    bool projectsEquivalent(const PatternProject& lhs, const PatternProject& rhs) const
    {
        auto left = lhs;
        auto right = rhs;
        left.generationCounter = 0;
        right.generationCounter = 0;
        return PatternProjectSerialization::serialize(left).isEquivalentTo(PatternProjectSerialization::serialize(right));
    }

    const std::vector<PatternProject>& getUndoHistory() const
    {
        return undoHistory;
    }

    const std::vector<PatternProject>& getRedoHistory() const
    {
        return redoHistory;
    }

private:
    std::vector<PatternProject> undoHistory;
    std::vector<PatternProject> redoHistory;
    std::optional<PatternProject> pendingHistoryBefore;
    std::optional<PatternProject> pendingHistoryAfter;
    std::optional<PatternProject> lastObservedProjectState;
    bool pendingHistoryCommitScheduled = false;
    bool suppressHistory = false;
};
} // namespace bbg