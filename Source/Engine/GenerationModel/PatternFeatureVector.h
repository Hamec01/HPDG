#pragma once

#include <vector>

namespace bbg
{
enum class GenerationLaneFamily
{
    Kick,
    Snare,
    Hat,
    Bass,
    Ornament
};

enum class MusicalRole
{
    Unknown,
    Anchor,
    Backbeat,
    Pulse,
    Pickup,
    Response,
    Ghost,
    Accent,
    Fill,
    Transition,
    Ornament
};

struct GenerationFeatureEvent
{
    GenerationLaneFamily lane = GenerationLaneFamily::Ornament;
    MusicalRole role = MusicalRole::Unknown;
    int tick = 0;
    int velocity = 100;
    int microTimingPpq = 0;
};

struct PatternFeatureInput
{
    int bars = 1;
    int ticksPerBar = 64;
    std::vector<GenerationFeatureEvent> events;
};

struct PatternFeatureVector
{
    float density = 0.0f;
    float syncopation = 0.0f;
    float velocityLife = 0.0f;
    float timingActivity = 0.0f;
    float negativeSpace = 0.0f;
    float repetition = 0.0f;
    float interlock = 0.0f;
    float roleClarity = 0.0f;

    float surfaceNovelty() const;
};

class PatternFeatureExtractor
{
public:
    static PatternFeatureVector extract(const PatternFeatureInput& input);
};
} // namespace bbg
