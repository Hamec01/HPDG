#pragma once

#include "Sub808Types.h"
#include "TrackState.h"

namespace bbg
{
inline std::vector<Sub808NoteEvent> sub808NotesForRead(const TrackState& track)
{
    if (!track.sub808Notes.empty())
        return track.sub808Notes;

    return toSub808NoteEvents(track.notes);
}
} // namespace bbg