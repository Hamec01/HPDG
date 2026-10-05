#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

#include "../../Core/TrackType.h"

namespace bbg
{
// Techno works on 16 steps per bar (1/16 = 240 PPQ); hat rolls use 32nds.
// Model and formulas: docs/techno-engine.md.
namespace TechnoGrid
{
constexpr int kStepsPerBar = 16;
constexpr int kPpqPerStep = 240;

// Metric weight w(s) (Lerdahl-Jackendoff hierarchy as in Longuet-Higgins & Lee).
inline float metricWeight(int step)
{
    step = ((step % kStepsPerBar) + kStepsPerBar) % kStepsPerBar;
    if (step == 0) return 1.00f;
    if (step == 8) return 0.75f;
    if (step % 4 == 0) return 0.60f;
    if (step % 2 == 0) return 0.35f;
    return 0.15f;
}

inline bool isBeat(int step) { return step % 4 == 0; }
} // namespace TechnoGrid

enum class TechnoSubstyle
{
    PeakTime = 0,
    Hypnotic,
    Minimal,
    Detroit,
    Dub,
    Acid,
    Hard,
    Count
};

enum class TechnoRole
{
    KickAxis,
    KickPickup,
    Rumble,
    Clap,
    ClapDisplaced,
    HatCarrier,
    OffbeatOpen,
    RideCarrier,
    Perc,
    HatRoll,
    Crash
};

const char* toString(TechnoRole role);

enum class TechnoHatMode
{
    Eighths = 0,
    Sixteenths,
    SixteenthsNoBeat,  // every 16th except the beats (the kick owns them)
    Offbeat,           // only the "and"s
    Count
};

const char* toString(TechnoHatMode mode);

enum class TechnoBarRole
{
    Main,       // A
    Variation,  // A': a small mutation of A
    Fill        // F: the phrase end
};

const char* toString(TechnoBarRole role);

// Percussion cycle: Euclidean E(k, n) with rotation, or a polymeter of `length` steps that
// restarts every bar (grouping dissonance against the 4/4 axis).
struct TechnoPercCycle
{
    bool polymeter = false;
    int k = 3;
    int n = 8;
    int rotation = 0;
    int length = 3;           // polymeter length in steps
    std::vector<int> pulses;  // polymeter: positions inside the cycle that hit

    std::vector<int> hitsInBar() const; // 0..15
    juce::String describe() const;
};

struct TechnoEvent
{
    TrackType lane = TrackType::Kick;
    int bar = 0;
    int step = 0;         // 0..15
    int subTick = 0;      // extra PPQ inside the step (32nd rolls: 0 or 120)
    int velocity = 100;
    int micro = 0;        // swing / humanize offset in PPQ
    int length = 1;       // steps
    TechnoRole role = TechnoRole::HatCarrier;
    bool ghost = false;
};

struct TechnoStyleProfile
{
    const char* name = "Peak Time";
    TechnoSubstyle substyle = TechnoSubstyle::PeakTime;

    float bpmDefault = 132.0f;
    int bpmMin = 128;
    int bpmMax = 135;
    float swingDefault = 51.0f;
    float densityDefault = 0.55f;
    float humanizeDefault = 0.15f;

    // Drums (section 3 of the document).
    float pickupKick = 0.20f;     // p_pick
    float dropKick = 0.45f;       // p_drop on the phrase's last beat
    float rumble = 0.0f;          // p_rumble per 16th after a beat
    float clap = 0.95f;           // p_clap on 2 / 4
    float clapDisplaced = 0.15f;  // p_disp in answer bars
    float offbeatOpen = 0.85f;    // p_offOpen
    std::array<float, static_cast<size_t>(TechnoHatMode::Count)> hatWeights {};
    float ride = 0.0f;            // chance the ride carries eighths
    float perc = 0.7f;            // chance of a perc layer
    float percPolymeter = 0.25f;  // polymeter instead of Euclid
    std::vector<std::array<int, 2>> euclids; // (k, n) candidates
    float hatRoll = 0.5f;         // 32nd roll at bar 4 / 8 ends
    float crash = 0.6f;
    float fill = 0.7f;

    // Scorer targets, calibrated on the grammar's own candidate distribution (300 candidates per
    // style, see the core test "Techno deterministic, varied, calibration"): S in weighted LHL
    // units per bar, D in weighted onsets per 16th, R = mean Jaccard of neighbouring bars.
    float syncTarget = 5.0f;        // S*
    float densityTarget = 0.85f;    // D*
    float repetitionTarget = 0.94f; // R*

    // Bass (section 5).
    std::array<float, 6> bassArchetypeWeights {}; // Offbeat, Rolling, Rumble, Acid, Pulse, Dub
    std::array<float, 7> degreeDistribution {};   // pi over I, VIII, V, b7, b3, IV, b2
    float degreeInertia = 0.45f;                  // rho
    int bassLow = 28;                             // register
    int bassHigh = 43;
};

const TechnoStyleProfile& getTechnoStyleProfile(int substyleIndex);
juce::StringArray getTechnoStyleNames();

struct TechnoScore
{
    float anchor = 0.0f;
    float interlock = 0.0f;
    float syncopation = 0.0f;
    float syncopationFit = 0.0f;
    float density = 0.0f;
    float densityFit = 0.0f;
    float repetition = 0.0f;
    float repetitionFit = 0.0f;
    float resolution = 1.0f;
    float penalties = 0.0f;
    float quality = 0.0f;
    bool passedGates = false;
    juce::String failedGate;
};

struct TechnoPattern
{
    int bars = 4;
    std::vector<TechnoEvent> events;
    std::vector<TechnoBarRole> barRoles;
    TechnoHatMode hatMode = TechnoHatMode::Sixteenths;
    bool rideOn = false;
    bool percOn = false;
    TechnoPercCycle perc;
    TechnoScore score;

    bool has(TrackType lane, int bar, int step) const;
};

struct TechnoGenerationParams
{
    int seed = 1;
    int bars = 4;
    int substyle = 0;
    float bpm = 132.0f;
    float density = 0.5f;
    float swingPercent = 51.0f;
    float humanize = 0.15f;
    int candidateCount = 48;
    float nearBestTolerance = 0.03f;
    float temperature = 0.012f;
};
} // namespace bbg
