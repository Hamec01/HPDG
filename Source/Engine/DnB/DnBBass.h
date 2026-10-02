#pragma once

#include <random>
#include <vector>

#include "DnBTypes.h"
#include "../../Analysis/SampleAwareGenerationContext.h"
#include "../../Core/NoteEvent.h"

namespace bbg
{
// Drum & Bass bass line.
//
// Not a riff library and not a random note spray: the bass is generated *against* the drums
// (lock with the kicks, leave the backbone snare alone, answer inside the 2-bar phrase) and
// *against the sample* through the analyzer's lens (its chord roots per half bar, where its own
// low end is busy or quiet, whether it already carries a bass line). Several bass archetypes
// compete in one candidate search; the scorer keeps only lines inside the quality zone.
enum class DnBBassArchetype
{
    SubReese = 0, // long sub / reese notes, re-attacked on the 2-step kick, glides on chord changes
    Rolling,      // a hypnotic one-bar 16th motif on root / octave / fifth
    Stab,         // short stabs locked to kicks + off-beat answers, tension notes (neuro)
    Wobble,       // bouncy eighth "wub" hits jumping octaves, pitch dives into the downbeat (jump-up)
    DubSub,       // sparse booming sub with slides (jungle / breakbeat)
    MelodicSub,   // chord roots with passing / approach notes, legato (liquid)
    Count
};

const char* toString(DnBBassArchetype archetype);

// What the drums give the bass: main kicks and backbone snares per bar (64-lattice ticks).
struct DnBDrumFrame
{
    int bars = 4;
    std::vector<std::vector<int>> kicks;
    std::vector<std::vector<int>> snares;

    static DnBDrumFrame fromPattern(const DnBPattern& pattern);
    static DnBDrumFrame fromNotes(const std::vector<NoteEvent>& kickNotes, const std::vector<NoteEvent>& snareNotes, int bars);
};

// The analyzer's view of the loaded sample, mapped onto the pattern grid.
struct DnBSampleLens
{
    enum class Mode
    {
        None,    // no sample: key controls + style progression
        Counter, // the sample has little bass: hit where its low end is quiet, follow its roots
        Support  // the sample has its own bass: sub-support its roots, do not fight its attacks
    };

    bool valid = false;
    bool rootsFromSample = false;
    Mode mode = Mode::None;
    float bassStrength = 0.0f;              // 0 = no bass in the sample .. 1 = strong confident bass
    std::vector<int> halfBarRoot;           // pitch class per half bar, -1 = unknown
    std::vector<float> halfBarConfidence;
    std::vector<float> lowEnergy;           // per 16th step of the pattern, 0..1
    std::vector<bool> sampleBassOnset;      // per 16th step: the sample's bass starts a note here

    // patternBpm: the tempo the pattern plays at (a DnB pattern may run at double the sample's
    // tempo); 0 = the sample's own tempo.
    static DnBSampleLens build(const SampleAwareGenerationContext& context, int bars, double patternBpm = 0.0);
    juce::String describe() const;
};

struct DnBBassParams
{
    int seed = 1;
    int bars = 4;
    int substyle = 0;
    int keyRoot = 0;
    int scaleMode = 0;   // 0 minor, 1 major, 2 harmonic minor
    int amount = 0;      // bass-lane [1][2][3]: 0 Low, 1 More, 2 Full
    float density = 0.5f;
    int candidateCount = 48;
    float nearBestTolerance = 0.03f;
    float temperature = 0.012f;
};

struct DnBBassNote
{
    int start = 0;       // absolute lattice tick (bar * 64 + tick)
    int length = 4;      // lattice ticks
    int pitch = 36;
    int velocity = 100;
    bool glide = false;  // slides from the previous (still sounding) note
    bool tension = false; // deliberately outside the chord / key (b2, approach)
    const char* role = "dnb_bass";
};

struct DnBBassScore
{
    float kickLock = 0.0f;
    float kickLockFit = 0.0f;
    float snareClash = 0.0f;
    float harmonic = 0.0f;
    float density = 0.0f;
    float densityFit = 0.0f;
    float sustain = 0.0f;
    float sustainFit = 0.0f;
    float repetition = 0.0f;
    float repetitionFit = 0.0f;
    float variety = 1.0f;
    float sampleRoots = 1.0f;
    float sampleSpace = 1.0f;
    float penalties = 0.0f;
    float core = 0.0f;
    float secondary = 0.0f;
    float quality = 0.0f;
    bool passedGates = false;
    juce::String failedGate;
};

struct DnBBassLine
{
    DnBBassArchetype archetype = DnBBassArchetype::SubReese;
    std::vector<DnBBassNote> notes;
    std::vector<int> halfBarRoots;
    bool sampleLed = false;
    DnBBassScore score;
};

class DnBBassGenerator
{
public:
    static DnBBassLine search(const DnBBassParams& params, const DnBDrumFrame& drums, const DnBSampleLens& lens,
                              juce::String* report = nullptr);
    static DnBBassScore score(const DnBBassLine& line, const DnBBassParams& params, const DnBDrumFrame& drums,
                              const DnBSampleLens& lens);
    static std::vector<NoteEvent> toNotes(const DnBBassLine& line);
};
} // namespace bbg
