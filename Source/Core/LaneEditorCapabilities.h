#pragma once

#include <optional>

#include "TrackType.h"

namespace bbg
{
enum class AlternateLaneEditor
{
    None,
    PianoRoll
};

struct LaneEditorCapabilities
{
    bool supportsPitchEditing = false;
    bool rendersNotePitchInGrid = false;
    int minPitch = 0;
    int maxPitch = 127;
    AlternateLaneEditor alternateEditor = AlternateLaneEditor::None;

    bool usesAlternateEditor() const noexcept
    {
        return alternateEditor != AlternateLaneEditor::None;
    }
};

inline LaneEditorCapabilities makeLaneEditorCapabilities(std::optional<TrackType> runtimeTrackType)
{
    LaneEditorCapabilities capabilities;

    if (!runtimeTrackType.has_value())
        return capabilities;

    switch (*runtimeTrackType)
    {
        case TrackType::Sub808:
            capabilities.supportsPitchEditing = true;
            capabilities.rendersNotePitchInGrid = true;
            capabilities.minPitch = 24;
            capabilities.maxPitch = 84;
            capabilities.alternateEditor = AlternateLaneEditor::PianoRoll;
            break;
        default:
            break;
    }

    return capabilities;
}
} // namespace bbg