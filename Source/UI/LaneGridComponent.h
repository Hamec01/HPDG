#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/TrackInfo.h"

namespace bbg
{
class LaneGridComponent : public juce::Component
{
public:
    void setTrackType(TrackType type) { trackType = type; }
    TrackType getTrackType() const { return trackType; }

private:
    TrackType trackType = TrackType::Kick;
};
} // namespace bbg