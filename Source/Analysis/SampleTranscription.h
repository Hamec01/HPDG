#pragma once

#include <vector>

#include "../Core/TrackType.h"

namespace bbg
{
struct TranscribedEvent
{
    TrackType lane = TrackType::Kick;
    int step = 0;
    int lengthSteps = 1;
    int velocity = 100;
    int pitch = 36;
    float confidence = 0.0f;
    bool ghost = false;
};

struct SampleTranscription
{
    std::vector<TranscribedEvent> drumEvents;
    std::vector<TranscribedEvent> bassEvents;

    bool hasDetectedDrums = false;
    bool hasDetectedBass = false;
};
} // namespace bbg