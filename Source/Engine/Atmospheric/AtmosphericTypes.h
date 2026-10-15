#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

#include "../../Core/TrackType.h"

namespace bbg
{
// Atmospheric (docs/audit/atmos/ATMOS_ENGINE_SPEC.md): 16 steps per bar (1/16 = 240 PPQ).
// Every target number comes from the Izzamuzzic passport (docs/audit/atmos/ATMOS_PASSPORT.md).
namespace AtmosGrid
{
constexpr int kStepsPerBar = 16;
constexpr int kPpqPerStep = 240;

inline bool isBeat(int step) { return step % 4 == 0; }
inline bool isEighth(int step) { return step % 2 == 0; }
} // namespace AtmosGrid

enum class AtmosphericSubstyle
{
    Pulse = 0, // family A: straight kick, clap 2 / 4 by sections, off-beat hats, sidechain bass
    TwoStep,   // family B: kick 1 + 11, snare 5 + 13, sparse bass
    Broken,    // family D1: 2 / 4 anchor, kicks and claps re-placed bar by bar, melodic bass
    Count
};

enum class AtmosRole
{
    Kick,
    KickExtra,
    Clap,
    SnarePickup,
    Hat,
    OpenHat
};

const char* toString(AtmosRole role);

struct AtmosEvent
{
    TrackType lane = TrackType::Kick;
    int bar = 0;
    int step = 0; // 0..15
    int velocity = 100;
    int micro = 0; // humanize offset in PPQ
    AtmosRole role = AtmosRole::Kick;
    bool ghost = false;
};

struct AtmosStyleProfile
{
    const char* name = "Pulse";
    AtmosphericSubstyle substyle = AtmosphericSubstyle::Pulse;

    float bpmDefault = 128.0f;
    int bpmMin = 118;
    int bpmMax = 132;
    float swingDefault = 50.0f;
    float densityDefault = 0.50f;
    float humanizeDefault = 0.12f;

    // Passport position profiles (share of bars with a hit on 16th 0..15).
    std::array<float, 16> kickProfile {};
    std::array<float, 16> snareProfile {};
    std::array<float, 16> hatProfile {};

    // Passport traits (scorer targets).
    float kicksPerBar = 2.6f;
    float snaresPerBar = 1.1f;
    float hatsPerBar = 3.6f;
    float kickOffBeat = 0.0f;   // share of kicks off the beat
    float snareOff24 = 0.5f;    // share of snares / claps off 2 / 4
    float kickRepeat = 0.54f;   // kick bar = previous bar

    // Bass (spec section 4).
    float bassNotesPerBar = 2.4f;
    float bassPitchChangesPerBar = 1.6f;
    float bassMedianBeats = 0.96f;
    float bassLongShare = 0.26f;                 // notes >= 2 beats
    std::array<float, 5> intervalMix {};         // repeat, step 1-2, third 3-4, fourth / fifth 5-7, octave
    std::array<float, 4> kickRelation {};        // on, after (<= 2 16ths), before (<= 2 16ths), away
    int bassRegister = 34;                       // MIDI centre (A#1)
};

const AtmosStyleProfile& getAtmosStyleProfile(int substyleIndex);
juce::StringArray getAtmosStyleNames();

struct AtmosTraits
{
    float kicksPerBar = 0.0f;
    float snaresPerBar = 0.0f;
    float hatsPerBar = 0.0f;
    float kickOffBeat = 0.0f;
    float snareOff24 = 0.0f;
    float kickRepeat = 0.0f;
    float motifShare = 1.0f; // mean overlap of each bar's kick + snare positions with bar 1
};

struct AtmosScore
{
    AtmosTraits traits;
    float fit = 0.0f;
    float quality = 0.0f;
    bool passedGates = false;
    juce::String failedGate;
};

struct AtmosPattern
{
    int bars = 4;
    std::vector<AtmosEvent> events;
    juce::String plan; // human-readable section plan (debug report)
    AtmosScore score;

    bool has(TrackType lane, int bar, int step) const;
};

struct AtmosGenerationParams
{
    int seed = 1;
    int bars = 4;
    int substyle = 0;
    float bpm = 128.0f;
    float density = 0.5f;
    float humanize = 0.12f;
    int candidateCount = 48;
    float nearBestTolerance = 0.10f;
    float temperature = 0.05f;
};
} // namespace bbg
