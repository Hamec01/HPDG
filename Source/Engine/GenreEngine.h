#pragma once

#include "../Core/PatternProject.h"

namespace bbg
{
class GenreEngine
{
public:
    virtual ~GenreEngine() = default;

    // PatternProject is the musical state boundary. Processor/UI parameter state stays in APVTS.
    virtual void generate(PatternProject& project) = 0;
    virtual void regenerateTrack(PatternProject& project, TrackType trackType) = 0;
};
} // namespace bbg
