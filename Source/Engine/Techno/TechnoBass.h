#pragma once

#include <random>
#include <vector>

#include "TechnoTypes.h"
#include "../../Analysis/SampleAwareGenerationContext.h"
#include "../../Core/NoteEvent.h"

namespace bbg
{
// Techno bass (docs/techno-engine.md, section 5): a 1-bar motif (Acid: 2 bars) whose attacks sit
// between the kicks (sidechain model g(d) = 1 - exp(-d / tau)), pitched by a root-returning
// Markov chain over the style's degree distribution, repeated over the phrase with small
// mutations in A' bars and an approach in the fill bar. Archetypes compete in a candidate search.
enum class TechnoBassArchetype
{
    Offbeat = 0,
    Rolling,
    Rumble,
    Acid,
    Pulse,
    Dub,
    Count
};

const char* toString(TechnoBassArchetype archetype);

// The loaded sample's roots per pattern bar (Guide mode), from its transcribed bass line.
struct TechnoSampleLens
{
    bool valid = false;
    std::vector<int> barRoot;   // pitch class per bar, -1 = unknown
    float bassStrength = 0.0f;  // share of the loop the sample's own bass sounds (0..1)

    static TechnoSampleLens build(const SampleAwareGenerationContext& context, int bars, double patternBpm);
    juce::String describe() const;
};

struct TechnoBassParams
{
    int seed = 1;
    int bars = 4;
    int substyle = 0;
    int keyRoot = 0;
    int scaleMode = 0;
    int amount = 1;      // lane buttons [1][2][3] = 0, 1, 2
    float density = 0.5f;
    int candidateCount = 32;
    float nearBestTolerance = 0.03f;
    float temperature = 0.015f;
};

struct TechnoBassNote
{
    int step = 0;         // absolute 16th (bar * 16 + step)
    int lengthTicks = 240;
    int pitch = 36;
    int velocity = 100;
    bool glide = false;   // slides into the next note (303 slide)
    bool accent = false;
    const char* role = "techno_bass";
};

struct TechnoBassLine
{
    TechnoBassArchetype archetype = TechnoBassArchetype::Offbeat;
    std::vector<TechnoBassNote> notes;
    std::vector<int> barRoots;
    bool sampleRoots = false;
    float kickClash = 0.0f;
    float rootShare = 0.0f;
    float rootFit = 0.0f;
    float densityFit = 0.0f;
    float repetition = 0.0f;
    float repetitionFit = 0.0f;
    float sampleFit = 1.0f;
    float quality = 0.0f;
    bool passedGates = false;
};

class TechnoBassGenerator
{
public:
    // kickSteps: the kick's 16th positions per bar (the drums the bass plays against).
    static TechnoBassLine search(const TechnoBassParams& params, const std::vector<std::vector<int>>& kickSteps,
                                 const TechnoSampleLens& lens, juce::String* report = nullptr);
    static std::vector<NoteEvent> toNotes(const TechnoBassLine& line);
};
} // namespace bbg
