#pragma once

#include <array>
#include <vector>

#include "../Analysis/SampleAnalysisBundle.h"
#include "../Core/NoteEvent.h"
#include "../Core/TrackType.h"

namespace bbg
{
struct ExtractedPatternData
{
    int bars = 0;
    std::array<std::vector<NoteEvent>, kTrackTypeCount> laneNotes {};
    std::vector<int> phraseBoundaries;

    bool hasLaneContent(TrackType lane) const
    {
        return !laneNotes[static_cast<size_t>(trackTypeIndex(lane))].empty();
    }

    bool hasAnyContent() const
    {
        for (const auto& lane : laneNotes)
        {
            if (!lane.empty())
                return true;
        }

        return false;
    }

    int populatedLaneCount() const
    {
        int count = 0;
        for (const auto& lane : laneNotes)
        {
            if (!lane.empty())
                ++count;
        }

        return count;
    }
};

class ExtractPatternBuilder
{
public:
    static ExtractedPatternData build(const SampleAnalysisBundle& bundle);
};
} // namespace bbg