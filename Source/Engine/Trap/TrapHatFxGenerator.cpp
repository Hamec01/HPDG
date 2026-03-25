#include "TrapHatFxGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"

namespace bbg
{
namespace
{
enum class TrapFxWindowType
{
    EndOfBar = 0,
    EndOfHalfBar,
    PreSnare,
    KickReaction,
    PhraseEdge,
    TransitionLift
};

enum class TrapHatFxBank
{
    Short32Roll = 0,
    Extended32Roll,
    Short64Burst,
    Ratchet2x,
    Ratchet3x,
    Ratchet4x,
    TripletFlutter,
    TripletEdgeBurst,
    RampUpBurst,
    RampDownBurst,
    PhraseEdgeBurst,
    PreSnareBurst,
    KickReactionBurst,
    JabBurst
};

enum class TrapFxSize
{
    Tiny = 0,
    Medium,
    Major
};

enum class HatVelocityContour
{
    Flat = 0,
    UpRamp,
    DownRamp,
    CenterPeak,
    AlternatingAccent,
    FadeBurst
};

enum class TrapHatFxPatternId
{
    Short32Roll_A = 0,
    Short32Roll_B,
    Short32Roll_C,
    Extended32Roll_A,
    Extended32Roll_B,
    Extended32Roll_C,
    Short64Burst_A,
    Short64Burst_B,
    Short64Burst_C,
    Ratchet2x_A,
    Ratchet2x_B,
    Ratchet3x_A,
    Ratchet3x_B,
    Ratchet4x_A,
    Ratchet4x_B,
    TripletFlutter_A,
    TripletFlutter_B,
    TripletEdgeBurst_A,
    TripletEdgeBurst_B,
    RampUpBurst_A,
    RampUpBurst_B,
    RampDownBurst_A,
    RampDownBurst_B,
    PhraseEdgeBurst_A,
    PhraseEdgeBurst_B,
    PhraseEdgeBurst_C,
    PreSnareBurst_A,
    PreSnareBurst_B,
    KickReactionBurst_A,
    KickReactionBurst_B,
    JabBurst_A,
    JabBurst_B
};

struct TrapHatFxBankWeight
{
    TrapHatFxBank bank;
    int weight;
};

struct TrapHatFxStyleProfile
{
    std::vector<TrapHatFxBankWeight> banks;

    int maxTinyPerBar = 0;
    int maxMediumPerBar = 0;
    int maxMajorPerBar = 0;
    int maxMajorPerTwoBars = 0;

    bool allowEndOfBar = false;
    bool allowEndOfHalfBar = false;
    bool allowPreSnare = false;
    bool allowKickReaction = false;
    bool allowPhraseEdge = false;
    bool allowTransitionLift = false;
};

struct TrapHatFxStep
{
    int tickOffset = 0;
    int velocity = 80;
    int microOffsetTicks = 0;
    bool suppressBaseHatAtSameMoment = false;
};

struct TrapHatFxPattern
{
    TrapHatFxPatternId id;
    TrapHatFxBank bank;
    TrapFxSize size = TrapFxSize::Tiny;
    HatVelocityContour contour = HatVelocityContour::Flat;

    int nominalLengthTicks = 0;
    std::vector<TrapFxWindowType> allowedWindows;
    std::vector<TrapHatFxStep> steps;
};

struct FxWindow
{
    int tick = 0;
    int bar = 0;
    TrapFxWindowType type = TrapFxWindowType::EndOfBar;
    float baseWeight = 0.5f;
};

struct LocalTrapContext
{
    int localHiHatDensity = 0;
    bool hasOpenHat = false;
    bool hasStrongKick = false;
    bool hasStrongSubRestart = false;
    bool previousWindowHadMajorFx = false;
    float phraseEdgeStrength = 0.0f;
    int barIndex = 0;
    int beatPosition = 0;
};

float fxDensityBias(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.66f;
        case TrapSubstyle::CloudTrap: return 0.8f;
        case TrapSubstyle::RageTrap: return 1.46f;
        case TrapSubstyle::MemphisTrap: return 1.14f;
        case TrapSubstyle::LuxuryTrap: return 0.72f;
        default: return 1.0f;
    }
}

float hatFxModeMultiplier(TrapHatFxMode mode)
{
    switch (mode)
    {
        case TrapHatFxMode::VeryLow: return 0.58f;
        case TrapHatFxMode::Low: return 0.78f;
        case TrapHatFxMode::Medium: return 1.0f;
        case TrapHatFxMode::High: return 1.24f;
        default: return 1.0f;
    }
}

float fxPhraseEnergyArc(int bar, int bars, TrapPhraseRole role, TrapSubstyle substyle)
{
    const float span = static_cast<float>(std::max(1, bars - 1));
    const float x = static_cast<float>(bar) / static_cast<float>(span);
    float arc = 0.82f + 0.24f * std::sin(x * juce::MathConstants<float>::pi);

    if (x >= 0.66f)
        arc += (x - 0.66f) * 0.36f;

    if (role == TrapPhraseRole::Lift)
        arc += 0.12f;
    else if (role == TrapPhraseRole::Ending)
        arc += 0.18f;

    if (substyle == TrapSubstyle::DarkTrap || substyle == TrapSubstyle::LuxuryTrap)
        arc *= 0.92f;
    else if (substyle == TrapSubstyle::RageTrap)
        arc *= 1.08f;

    return std::clamp(arc, 0.62f, 1.32f);
}

TrapFxSize classifyFxSize(TrapHatFxBank bank)
{
    switch (bank)
    {
        case TrapHatFxBank::Short32Roll:
        case TrapHatFxBank::Ratchet2x:
        case TrapHatFxBank::JabBurst:
            return TrapFxSize::Tiny;

        case TrapHatFxBank::Extended32Roll:
        case TrapHatFxBank::Ratchet3x:
        case TrapHatFxBank::TripletFlutter:
        case TrapHatFxBank::TripletEdgeBurst:
        case TrapHatFxBank::RampUpBurst:
        case TrapHatFxBank::RampDownBurst:
        case TrapHatFxBank::PreSnareBurst:
        case TrapHatFxBank::KickReactionBurst:
            return TrapFxSize::Medium;

        case TrapHatFxBank::Short64Burst:
        case TrapHatFxBank::Ratchet4x:
        case TrapHatFxBank::PhraseEdgeBurst:
            return TrapFxSize::Major;

        default:
            return TrapFxSize::Medium;
    }
}

bool isTripletBank(TrapHatFxBank bank)
{
    return bank == TrapHatFxBank::TripletFlutter
        || bank == TrapHatFxBank::TripletEdgeBurst;
}

int countNotesInTickRange(const TrackState& track, int startTick, int endTick, bool includeGhost = true)
{
    int count = 0;
    for (const auto& n : track.notes)
    {
        if (!includeGhost && n.isGhost)
            continue;

        const int tick = HiResTiming::noteTick(n);
        if (tick >= startTick && tick <= endTick)
            ++count;
    }
    return count;
}

int countStrongAccentInRange(const TrackState* track, int startTick, int endTick, int velocityThreshold)
{
    if (track == nullptr)
        return 0;

    int count = 0;
    for (const auto& n : track->notes)
    {
        const int tick = HiResTiming::noteTick(n);
        if (tick < startTick || tick > endTick)
            continue;
        if (n.isGhost)
            continue;
        if (n.velocity >= velocityThreshold)
            ++count;
    }
    return count;
}

int localHatDensityLimit(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::DarkTrap: return 6;
        case TrapSubstyle::CloudTrap: return 7;
        case TrapSubstyle::RageTrap: return 10;
        case TrapSubstyle::MemphisTrap: return 8;
        case TrapSubstyle::LuxuryTrap: return 6;
        case TrapSubstyle::ATLClassic:
        default:
            return 8;
    }
}

bool isKickAnchor(const NoteEvent& note)
{
    const int s = note.step % 16;
    return s == 0 || s == 8;
}

bool windowVecContains(const std::vector<TrapFxWindowType>& windows, TrapFxWindowType type)
{
    return std::find(windows.begin(), windows.end(), type) != windows.end();
}

std::vector<TrapHatFxStep> makeSteps(const std::initializer_list<int>& offsets,
                                     const std::initializer_list<int>& velocities,
                                     bool suppress)
{
    std::vector<TrapHatFxStep> out;
    const auto count = std::min(offsets.size(), velocities.size());
    out.reserve(count);

    auto off = offsets.begin();
    auto vel = velocities.begin();
    for (size_t i = 0; i < count; ++i, ++off, ++vel)
        out.push_back({ *off, *vel, 0, suppress });

    return out;
}

const std::vector<TrapHatFxPattern>& getPatternLibrary()
{
    static const std::vector<TrapHatFxPattern> library = []
    {
        const int k32 = HiResTiming::kTicks1_32;
        const int k64 = HiResTiming::kTicks1_64;
        const int kTri = HiResTiming::kTicks1_24 / 2;

        return std::vector<TrapHatFxPattern>
        {
            { TrapHatFxPatternId::Short32Roll_A, TrapHatFxBank::Short32Roll, TrapFxSize::Tiny, HatVelocityContour::UpRamp, 3 * k32, { TrapFxWindowType::EndOfHalfBar, TrapFxWindowType::EndOfBar, TrapFxWindowType::PreSnare }, makeSteps({ 0, k32, 2 * k32 }, { 62, 74, 88 }, false) },
            { TrapHatFxPatternId::Short32Roll_B, TrapHatFxBank::Short32Roll, TrapFxSize::Tiny, HatVelocityContour::CenterPeak, 4 * k32, { TrapFxWindowType::EndOfHalfBar, TrapFxWindowType::EndOfBar, TrapFxWindowType::PreSnare }, makeSteps({ 0, k32, 2 * k32, 3 * k32 }, { 66, 82, 90, 74 }, false) },
            { TrapHatFxPatternId::Short32Roll_C, TrapHatFxBank::Short32Roll, TrapFxSize::Tiny, HatVelocityContour::DownRamp, 3 * k32, { TrapFxWindowType::EndOfHalfBar, TrapFxWindowType::EndOfBar, TrapFxWindowType::PreSnare }, makeSteps({ 0, k32, 2 * k32 }, { 90, 78, 64 }, false) },

            { TrapHatFxPatternId::Extended32Roll_A, TrapHatFxBank::Extended32Roll, TrapFxSize::Medium, HatVelocityContour::UpRamp, 5 * k32, { TrapFxWindowType::EndOfBar, TrapFxWindowType::PhraseEdge, TrapFxWindowType::TransitionLift }, makeSteps({ 0, k32, 2 * k32, 3 * k32, 4 * k32 }, { 58, 66, 74, 84, 94 }, false) },
            { TrapHatFxPatternId::Extended32Roll_B, TrapHatFxBank::Extended32Roll, TrapFxSize::Medium, HatVelocityContour::CenterPeak, 6 * k32, { TrapFxWindowType::EndOfBar, TrapFxWindowType::PhraseEdge, TrapFxWindowType::TransitionLift }, makeSteps({ 0, k32, 2 * k32, 3 * k32, 4 * k32, 5 * k32 }, { 60, 72, 84, 90, 80, 68 }, false) },
            { TrapHatFxPatternId::Extended32Roll_C, TrapHatFxBank::Extended32Roll, TrapFxSize::Medium, HatVelocityContour::FadeBurst, 5 * k32, { TrapFxWindowType::EndOfBar, TrapFxWindowType::PhraseEdge, TrapFxWindowType::TransitionLift }, makeSteps({ 0, k32, 2 * k32, 3 * k32, 4 * k32 }, { 92, 84, 74, 64, 52 }, false) },

            { TrapHatFxPatternId::Short64Burst_A, TrapHatFxBank::Short64Burst, TrapFxSize::Major, HatVelocityContour::UpRamp, 5 * k64, { TrapFxWindowType::PhraseEdge, TrapFxWindowType::EndOfBar }, makeSteps({ 0, k64, 2 * k64, 3 * k64, 4 * k64 }, { 58, 66, 76, 88, 98 }, true) },
            { TrapHatFxPatternId::Short64Burst_B, TrapHatFxBank::Short64Burst, TrapFxSize::Major, HatVelocityContour::AlternatingAccent, 6 * k64, { TrapFxWindowType::PhraseEdge, TrapFxWindowType::EndOfBar, TrapFxWindowType::KickReaction }, makeSteps({ 0, k64, 2 * k64, 3 * k64, 4 * k64, 5 * k64 }, { 92, 58, 92, 60, 90, 56 }, true) },
            { TrapHatFxPatternId::Short64Burst_C, TrapHatFxBank::Short64Burst, TrapFxSize::Major, HatVelocityContour::FadeBurst, 5 * k64, { TrapFxWindowType::PhraseEdge, TrapFxWindowType::EndOfBar }, makeSteps({ 0, k64, 2 * k64, 3 * k64, 4 * k64 }, { 96, 84, 72, 60, 48 }, true) },

            { TrapHatFxPatternId::Ratchet2x_A, TrapHatFxBank::Ratchet2x, TrapFxSize::Tiny, HatVelocityContour::AlternatingAccent, 2 * k64, { TrapFxWindowType::EndOfHalfBar, TrapFxWindowType::PreSnare, TrapFxWindowType::KickReaction, TrapFxWindowType::EndOfBar }, makeSteps({ 0, k64 }, { 88, 62 }, false) },
            { TrapHatFxPatternId::Ratchet2x_B, TrapHatFxBank::Ratchet2x, TrapFxSize::Tiny, HatVelocityContour::UpRamp, 2 * k64, { TrapFxWindowType::EndOfHalfBar, TrapFxWindowType::PreSnare, TrapFxWindowType::KickReaction, TrapFxWindowType::EndOfBar }, makeSteps({ 0, k64 }, { 68, 90 }, false) },

            { TrapHatFxPatternId::Ratchet3x_A, TrapHatFxBank::Ratchet3x, TrapFxSize::Medium, HatVelocityContour::UpRamp, 3 * k64, { TrapFxWindowType::PreSnare, TrapFxWindowType::PhraseEdge, TrapFxWindowType::EndOfBar }, makeSteps({ 0, k64, 2 * k64 }, { 64, 78, 94 }, false) },
            { TrapHatFxPatternId::Ratchet3x_B, TrapHatFxBank::Ratchet3x, TrapFxSize::Medium, HatVelocityContour::AlternatingAccent, 3 * k64, { TrapFxWindowType::PreSnare, TrapFxWindowType::PhraseEdge, TrapFxWindowType::EndOfBar }, makeSteps({ 0, k64, 2 * k64 }, { 90, 60, 86 }, false) },

            { TrapHatFxPatternId::Ratchet4x_A, TrapHatFxBank::Ratchet4x, TrapFxSize::Major, HatVelocityContour::UpRamp, 4 * k64, { TrapFxWindowType::PhraseEdge, TrapFxWindowType::EndOfBar }, makeSteps({ 0, k64, 2 * k64, 3 * k64 }, { 58, 70, 84, 98 }, true) },
            { TrapHatFxPatternId::Ratchet4x_B, TrapHatFxBank::Ratchet4x, TrapFxSize::Major, HatVelocityContour::AlternatingAccent, 4 * k64, { TrapFxWindowType::PhraseEdge, TrapFxWindowType::EndOfBar }, makeSteps({ 0, k64, 2 * k64, 3 * k64 }, { 92, 60, 88, 62 }, true) },

            { TrapHatFxPatternId::TripletFlutter_A, TrapHatFxBank::TripletFlutter, TrapFxSize::Medium, HatVelocityContour::UpRamp, 3 * kTri, { TrapFxWindowType::EndOfHalfBar, TrapFxWindowType::TransitionLift, TrapFxWindowType::EndOfBar }, makeSteps({ 0, kTri, 2 * kTri }, { 62, 76, 90 }, false) },
            { TrapHatFxPatternId::TripletFlutter_B, TrapHatFxBank::TripletFlutter, TrapFxSize::Medium, HatVelocityContour::CenterPeak, 4 * kTri, { TrapFxWindowType::EndOfHalfBar, TrapFxWindowType::TransitionLift, TrapFxWindowType::EndOfBar }, makeSteps({ 0, kTri, 2 * kTri, 3 * kTri }, { 60, 80, 88, 70 }, false) },

            { TrapHatFxPatternId::TripletEdgeBurst_A, TrapHatFxBank::TripletEdgeBurst, TrapFxSize::Medium, HatVelocityContour::DownRamp, 3 * kTri, { TrapFxWindowType::PreSnare, TrapFxWindowType::PhraseEdge }, makeSteps({ 0, kTri, 2 * kTri }, { 92, 78, 62 }, false) },
            { TrapHatFxPatternId::TripletEdgeBurst_B, TrapHatFxBank::TripletEdgeBurst, TrapFxSize::Medium, HatVelocityContour::AlternatingAccent, 4 * kTri, { TrapFxWindowType::PreSnare, TrapFxWindowType::PhraseEdge }, makeSteps({ 0, kTri, 2 * kTri, 3 * kTri }, { 90, 58, 84, 56 }, false) },

            { TrapHatFxPatternId::RampUpBurst_A, TrapHatFxBank::RampUpBurst, TrapFxSize::Medium, HatVelocityContour::UpRamp, 4 * k32, { TrapFxWindowType::EndOfBar, TrapFxWindowType::TransitionLift, TrapFxWindowType::PreSnare, TrapFxWindowType::PhraseEdge }, makeSteps({ 0, k32, 2 * k32, 2 * k32 + k64, 3 * k32 }, { 56, 64, 72, 84, 96 }, false) },
            { TrapHatFxPatternId::RampUpBurst_B, TrapHatFxBank::RampUpBurst, TrapFxSize::Medium, HatVelocityContour::UpRamp, 2 * k32, { TrapFxWindowType::EndOfBar, TrapFxWindowType::TransitionLift, TrapFxWindowType::PreSnare, TrapFxWindowType::PhraseEdge }, makeSteps({ 0, k32, k32 + k64, 2 * k32 }, { 60, 70, 82, 96 }, false) },

            { TrapHatFxPatternId::RampDownBurst_A, TrapHatFxBank::RampDownBurst, TrapFxSize::Medium, HatVelocityContour::DownRamp, 2 * k32, { TrapFxWindowType::EndOfBar, TrapFxWindowType::PhraseEdge }, makeSteps({ 0, k64, 2 * k64, 2 * k32 }, { 96, 82, 68, 54 }, false) },
            { TrapHatFxPatternId::RampDownBurst_B, TrapHatFxBank::RampDownBurst, TrapFxSize::Medium, HatVelocityContour::FadeBurst, 3 * k32, { TrapFxWindowType::EndOfBar, TrapFxWindowType::PhraseEdge }, makeSteps({ 0, k32, 2 * k32, 3 * k32 }, { 90, 74, 60, 44 }, false) },

            { TrapHatFxPatternId::PhraseEdgeBurst_A, TrapHatFxBank::PhraseEdgeBurst, TrapFxSize::Major, HatVelocityContour::UpRamp, 3 * k32 + k64, { TrapFxWindowType::PhraseEdge }, makeSteps({ 0, k32, 2 * k32, 2 * k32 + k64, 3 * k32, 3 * k32 + k64 }, { 58, 68, 78, 86, 94, 104 }, true) },
            { TrapHatFxPatternId::PhraseEdgeBurst_B, TrapHatFxBank::PhraseEdgeBurst, TrapFxSize::Major, HatVelocityContour::CenterPeak, 2 * k32 + k64, { TrapFxWindowType::PhraseEdge }, makeSteps({ 0, k64, 2 * k64, 2 * k32, 2 * k32 + k64 }, { 64, 82, 98, 84, 70 }, true) },
            { TrapHatFxPatternId::PhraseEdgeBurst_C, TrapHatFxBank::PhraseEdgeBurst, TrapFxSize::Major, HatVelocityContour::AlternatingAccent, 4 * k64, { TrapFxWindowType::PhraseEdge }, makeSteps({ 0, k64, 2 * k64, 3 * k64, 4 * k64 }, { 94, 58, 90, 60, 98 }, true) },

            { TrapHatFxPatternId::PreSnareBurst_A, TrapHatFxBank::PreSnareBurst, TrapFxSize::Medium, HatVelocityContour::UpRamp, 3 * k32, { TrapFxWindowType::PreSnare }, makeSteps({ 0, k32, 2 * k32 }, { 60, 78, 96 }, false) },
            { TrapHatFxPatternId::PreSnareBurst_B, TrapHatFxBank::PreSnareBurst, TrapFxSize::Medium, HatVelocityContour::AlternatingAccent, 2 * k32, { TrapFxWindowType::PreSnare }, makeSteps({ 0, k64, 2 * k64 }, { 86, 58, 92 }, false) },

            { TrapHatFxPatternId::KickReactionBurst_A, TrapHatFxBank::KickReactionBurst, TrapFxSize::Medium, HatVelocityContour::UpRamp, 3 * k64, { TrapFxWindowType::KickReaction }, makeSteps({ 0, k64, 3 * k64 }, { 62, 78, 92 }, false) },
            { TrapHatFxPatternId::KickReactionBurst_B, TrapHatFxBank::KickReactionBurst, TrapFxSize::Medium, HatVelocityContour::CenterPeak, 3 * k64, { TrapFxWindowType::KickReaction }, makeSteps({ 0, k32, 3 * k64 }, { 68, 90, 72 }, false) },

            { TrapHatFxPatternId::JabBurst_A, TrapHatFxBank::JabBurst, TrapFxSize::Tiny, HatVelocityContour::AlternatingAccent, 2 * k64, { TrapFxWindowType::KickReaction, TrapFxWindowType::EndOfHalfBar, TrapFxWindowType::EndOfBar, TrapFxWindowType::PhraseEdge }, makeSteps({ 0, k64 }, { 94, 62 }, false) },
            { TrapHatFxPatternId::JabBurst_B, TrapHatFxBank::JabBurst, TrapFxSize::Tiny, HatVelocityContour::DownRamp, k32, { TrapFxWindowType::KickReaction, TrapFxWindowType::EndOfHalfBar, TrapFxWindowType::EndOfBar, TrapFxWindowType::PhraseEdge }, makeSteps({ 0, k32 }, { 88, 58 }, false) }
        };
    }();

    return library;
}

float patternPreferenceWeight(TrapSubstyle substyle, TrapHatFxPatternId id)
{
    // Default neutral.
    float w = 1.0f;

    switch (substyle)
    {
        case TrapSubstyle::ATLClassic:
            if (id == TrapHatFxPatternId::Short32Roll_A || id == TrapHatFxPatternId::Short32Roll_B
                || id == TrapHatFxPatternId::Extended32Roll_A || id == TrapHatFxPatternId::Ratchet2x_A
                || id == TrapHatFxPatternId::Ratchet3x_A || id == TrapHatFxPatternId::PreSnareBurst_A
                || id == TrapHatFxPatternId::RampUpBurst_A || id == TrapHatFxPatternId::PhraseEdgeBurst_A)
                return 1.65f;
            if (id == TrapHatFxPatternId::Short64Burst_A || id == TrapHatFxPatternId::TripletFlutter_A)
                return 0.75f;
            if (id == TrapHatFxPatternId::Ratchet4x_B)
                return 0.2f;
            return w;

        case TrapSubstyle::DarkTrap:
            if (id == TrapHatFxPatternId::Short32Roll_C || id == TrapHatFxPatternId::TripletEdgeBurst_A
                || id == TrapHatFxPatternId::RampDownBurst_A || id == TrapHatFxPatternId::PhraseEdgeBurst_B
                || id == TrapHatFxPatternId::JabBurst_A)
                return 1.7f;
            if (id == TrapHatFxPatternId::PhraseEdgeBurst_C)
                return 1.2f;
            if (id == TrapHatFxPatternId::Short64Burst_C || id == TrapHatFxPatternId::Ratchet2x_B)
                return 0.6f;
            if (id == TrapHatFxPatternId::Ratchet4x_A || id == TrapHatFxPatternId::Ratchet4x_B)
                return 0.1f;
            return w;

        case TrapSubstyle::CloudTrap:
            if (id == TrapHatFxPatternId::Short32Roll_B || id == TrapHatFxPatternId::Extended32Roll_B
                || id == TrapHatFxPatternId::TripletFlutter_A || id == TrapHatFxPatternId::TripletFlutter_B
                || id == TrapHatFxPatternId::RampUpBurst_B || id == TrapHatFxPatternId::RampDownBurst_B)
                return 1.7f;
            if (id == TrapHatFxPatternId::PhraseEdgeBurst_B || id == TrapHatFxPatternId::Ratchet2x_B)
                return 0.72f;
            if (id == TrapHatFxPatternId::Short64Burst_B || id == TrapHatFxPatternId::JabBurst_A)
                return 0.2f;
            return w;

        case TrapSubstyle::RageTrap:
            if (id == TrapHatFxPatternId::Extended32Roll_A || id == TrapHatFxPatternId::Extended32Roll_C
                || id == TrapHatFxPatternId::Short64Burst_A || id == TrapHatFxPatternId::Short64Burst_B
                || id == TrapHatFxPatternId::Ratchet3x_B || id == TrapHatFxPatternId::Ratchet4x_A
                || id == TrapHatFxPatternId::RampUpBurst_A || id == TrapHatFxPatternId::PhraseEdgeBurst_C
                || id == TrapHatFxPatternId::KickReactionBurst_A || id == TrapHatFxPatternId::PreSnareBurst_B)
                return 1.75f;
            if (id == TrapHatFxPatternId::TripletFlutter_A || id == TrapHatFxPatternId::RampDownBurst_A)
                return 0.6f;
            return w;

        case TrapSubstyle::MemphisTrap:
            if (id == TrapHatFxPatternId::Short32Roll_C || id == TrapHatFxPatternId::TripletEdgeBurst_B
                || id == TrapHatFxPatternId::KickReactionBurst_B || id == TrapHatFxPatternId::JabBurst_A
                || id == TrapHatFxPatternId::JabBurst_B || id == TrapHatFxPatternId::PhraseEdgeBurst_B
                || id == TrapHatFxPatternId::Ratchet3x_A)
                return 1.7f;
            if (id == TrapHatFxPatternId::Short64Burst_C || id == TrapHatFxPatternId::TripletFlutter_A)
                return 0.72f;
            return w;

        case TrapSubstyle::LuxuryTrap:
            if (id == TrapHatFxPatternId::Short32Roll_B || id == TrapHatFxPatternId::Extended32Roll_B
                || id == TrapHatFxPatternId::RampUpBurst_B || id == TrapHatFxPatternId::RampDownBurst_B
                || id == TrapHatFxPatternId::PhraseEdgeBurst_B || id == TrapHatFxPatternId::PreSnareBurst_A)
                return 1.72f;
            if (id == TrapHatFxPatternId::Short64Burst_A || id == TrapHatFxPatternId::Ratchet2x_B)
                return 0.68f;
            if (id == TrapHatFxPatternId::JabBurst_A || id == TrapHatFxPatternId::Ratchet4x_A || id == TrapHatFxPatternId::Ratchet4x_B)
                return 0.18f;
            return w;

        default:
            return w;
    }
}

TrapHatFxStyleProfile createStyleProfile(TrapSubstyle substyle)
{
    TrapHatFxStyleProfile p;

    switch (substyle)
    {
        case TrapSubstyle::ATLClassic:
            p.banks = {
                { TrapHatFxBank::Short32Roll, 24 }, { TrapHatFxBank::Extended32Roll, 16 }, { TrapHatFxBank::Short64Burst, 7 },
                { TrapHatFxBank::Ratchet2x, 14 }, { TrapHatFxBank::Ratchet3x, 9 }, { TrapHatFxBank::Ratchet4x, 3 },
                { TrapHatFxBank::TripletFlutter, 10 }, { TrapHatFxBank::TripletEdgeBurst, 8 }, { TrapHatFxBank::RampUpBurst, 11 },
                { TrapHatFxBank::RampDownBurst, 8 }, { TrapHatFxBank::PhraseEdgeBurst, 10 }, { TrapHatFxBank::PreSnareBurst, 12 },
                { TrapHatFxBank::KickReactionBurst, 10 }, { TrapHatFxBank::JabBurst, 6 }
            };
            p.maxTinyPerBar = 2; p.maxMediumPerBar = 1; p.maxMajorPerBar = 1; p.maxMajorPerTwoBars = 2;
            p.allowEndOfBar = true; p.allowEndOfHalfBar = true; p.allowPreSnare = true; p.allowKickReaction = false; p.allowPhraseEdge = true; p.allowTransitionLift = false;
            break;

        case TrapSubstyle::DarkTrap:
            p.banks = {
                { TrapHatFxBank::Short32Roll, 18 }, { TrapHatFxBank::Extended32Roll, 8 }, { TrapHatFxBank::Short64Burst, 2 },
                { TrapHatFxBank::Ratchet2x, 7 }, { TrapHatFxBank::Ratchet3x, 3 }, { TrapHatFxBank::Ratchet4x, 0 },
                { TrapHatFxBank::TripletFlutter, 6 }, { TrapHatFxBank::TripletEdgeBurst, 10 }, { TrapHatFxBank::RampUpBurst, 5 },
                { TrapHatFxBank::RampDownBurst, 9 }, { TrapHatFxBank::PhraseEdgeBurst, 16 }, { TrapHatFxBank::PreSnareBurst, 8 },
                { TrapHatFxBank::KickReactionBurst, 6 }, { TrapHatFxBank::JabBurst, 10 }
            };
            p.maxTinyPerBar = 1; p.maxMediumPerBar = 1; p.maxMajorPerBar = 1; p.maxMajorPerTwoBars = 1;
            p.allowEndOfBar = true; p.allowEndOfHalfBar = false; p.allowPreSnare = true; p.allowKickReaction = false; p.allowPhraseEdge = true; p.allowTransitionLift = false;
            break;

        case TrapSubstyle::CloudTrap:
            p.banks = {
                { TrapHatFxBank::Short32Roll, 16 }, { TrapHatFxBank::Extended32Roll, 12 }, { TrapHatFxBank::Short64Burst, 3 },
                { TrapHatFxBank::Ratchet2x, 8 }, { TrapHatFxBank::Ratchet3x, 4 }, { TrapHatFxBank::Ratchet4x, 1 },
                { TrapHatFxBank::TripletFlutter, 16 }, { TrapHatFxBank::TripletEdgeBurst, 10 }, { TrapHatFxBank::RampUpBurst, 12 },
                { TrapHatFxBank::RampDownBurst, 10 }, { TrapHatFxBank::PhraseEdgeBurst, 9 }, { TrapHatFxBank::PreSnareBurst, 7 },
                { TrapHatFxBank::KickReactionBurst, 6 }, { TrapHatFxBank::JabBurst, 3 }
            };
            p.maxTinyPerBar = 2; p.maxMediumPerBar = 1; p.maxMajorPerBar = 1; p.maxMajorPerTwoBars = 2;
            p.allowEndOfBar = true; p.allowEndOfHalfBar = true; p.allowPreSnare = true; p.allowKickReaction = false; p.allowPhraseEdge = true; p.allowTransitionLift = true;
            break;

        case TrapSubstyle::RageTrap:
            p.banks = {
                { TrapHatFxBank::Short32Roll, 18 }, { TrapHatFxBank::Extended32Roll, 16 }, { TrapHatFxBank::Short64Burst, 14 },
                { TrapHatFxBank::Ratchet2x, 10 }, { TrapHatFxBank::Ratchet3x, 14 }, { TrapHatFxBank::Ratchet4x, 11 },
                { TrapHatFxBank::TripletFlutter, 7 }, { TrapHatFxBank::TripletEdgeBurst, 5 }, { TrapHatFxBank::RampUpBurst, 15 },
                { TrapHatFxBank::RampDownBurst, 6 }, { TrapHatFxBank::PhraseEdgeBurst, 14 }, { TrapHatFxBank::PreSnareBurst, 12 },
                { TrapHatFxBank::KickReactionBurst, 13 }, { TrapHatFxBank::JabBurst, 8 }
            };
            p.maxTinyPerBar = 2; p.maxMediumPerBar = 2; p.maxMajorPerBar = 1; p.maxMajorPerTwoBars = 3;
            p.allowEndOfBar = true; p.allowEndOfHalfBar = true; p.allowPreSnare = true; p.allowKickReaction = true; p.allowPhraseEdge = true; p.allowTransitionLift = true;
            break;

        case TrapSubstyle::MemphisTrap:
            p.banks = {
                { TrapHatFxBank::Short32Roll, 17 }, { TrapHatFxBank::Extended32Roll, 10 }, { TrapHatFxBank::Short64Burst, 6 },
                { TrapHatFxBank::Ratchet2x, 9 }, { TrapHatFxBank::Ratchet3x, 7 }, { TrapHatFxBank::Ratchet4x, 2 },
                { TrapHatFxBank::TripletFlutter, 11 }, { TrapHatFxBank::TripletEdgeBurst, 12 }, { TrapHatFxBank::RampUpBurst, 7 },
                { TrapHatFxBank::RampDownBurst, 8 }, { TrapHatFxBank::PhraseEdgeBurst, 13 }, { TrapHatFxBank::PreSnareBurst, 8 },
                { TrapHatFxBank::KickReactionBurst, 11 }, { TrapHatFxBank::JabBurst, 13 }
            };
            p.maxTinyPerBar = 2; p.maxMediumPerBar = 1; p.maxMajorPerBar = 1; p.maxMajorPerTwoBars = 2;
            p.allowEndOfBar = true; p.allowEndOfHalfBar = true; p.allowPreSnare = true; p.allowKickReaction = true; p.allowPhraseEdge = true; p.allowTransitionLift = false;
            break;

        case TrapSubstyle::LuxuryTrap:
        default:
            p.banks = {
                { TrapHatFxBank::Short32Roll, 17 }, { TrapHatFxBank::Extended32Roll, 11 }, { TrapHatFxBank::Short64Burst, 4 },
                { TrapHatFxBank::Ratchet2x, 8 }, { TrapHatFxBank::Ratchet3x, 4 }, { TrapHatFxBank::Ratchet4x, 1 },
                { TrapHatFxBank::TripletFlutter, 8 }, { TrapHatFxBank::TripletEdgeBurst, 5 }, { TrapHatFxBank::RampUpBurst, 9 },
                { TrapHatFxBank::RampDownBurst, 8 }, { TrapHatFxBank::PhraseEdgeBurst, 12 }, { TrapHatFxBank::PreSnareBurst, 10 },
                { TrapHatFxBank::KickReactionBurst, 7 }, { TrapHatFxBank::JabBurst, 4 }
            };
            p.maxTinyPerBar = 1; p.maxMediumPerBar = 1; p.maxMajorPerBar = 1; p.maxMajorPerTwoBars = 2;
            p.allowEndOfBar = true; p.allowEndOfHalfBar = true; p.allowPreSnare = true; p.allowKickReaction = false; p.allowPhraseEdge = true; p.allowTransitionLift = true;
            break;
    }

    return p;
}

bool isWindowAllowedByProfile(const TrapHatFxStyleProfile& p,
                              TrapSubstyle substyle,
                              TrapFxWindowType type,
                              std::mt19937& rng)
{
    const bool allowed =
        (type == TrapFxWindowType::EndOfBar && p.allowEndOfBar)
        || (type == TrapFxWindowType::EndOfHalfBar && p.allowEndOfHalfBar)
        || (type == TrapFxWindowType::PreSnare && p.allowPreSnare)
        || (type == TrapFxWindowType::KickReaction && p.allowKickReaction)
        || (type == TrapFxWindowType::PhraseEdge && p.allowPhraseEdge)
        || (type == TrapFxWindowType::TransitionLift && p.allowTransitionLift);

    if (!allowed)
        return false;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    if (substyle == TrapSubstyle::DarkTrap && type == TrapFxWindowType::PreSnare)
        return chance(rng) < 0.42f;
    if (substyle == TrapSubstyle::CloudTrap && type == TrapFxWindowType::PreSnare)
        return chance(rng) < 0.2f;
    if (substyle == TrapSubstyle::MemphisTrap && type == TrapFxWindowType::EndOfHalfBar)
        return chance(rng) < 0.48f;
    if (substyle == TrapSubstyle::MemphisTrap && type == TrapFxWindowType::PreSnare)
        return chance(rng) < 0.22f;
    if (substyle == TrapSubstyle::LuxuryTrap && type == TrapFxWindowType::EndOfHalfBar)
        return chance(rng) < 0.28f;
    if (substyle == TrapSubstyle::LuxuryTrap && type == TrapFxWindowType::TransitionLift)
        return chance(rng) < 0.18f;

    return true;
}

bool isBankAllowedInWindow(TrapHatFxBank bank, TrapFxWindowType windowType, TrapSubstyle substyle)
{
    switch (windowType)
    {
        case TrapFxWindowType::EndOfBar:
            return bank == TrapHatFxBank::Short32Roll
                || bank == TrapHatFxBank::Extended32Roll
                || bank == TrapHatFxBank::Short64Burst
                || bank == TrapHatFxBank::Ratchet2x
                || bank == TrapHatFxBank::Ratchet3x
                || bank == TrapHatFxBank::RampUpBurst
                || bank == TrapHatFxBank::RampDownBurst
                || bank == TrapHatFxBank::PhraseEdgeBurst
                || bank == TrapHatFxBank::JabBurst;

        case TrapFxWindowType::EndOfHalfBar:
            return bank == TrapHatFxBank::Short32Roll
                || bank == TrapHatFxBank::Ratchet2x
                || bank == TrapHatFxBank::TripletFlutter
                || bank == TrapHatFxBank::JabBurst
                || bank == TrapHatFxBank::RampUpBurst;

        case TrapFxWindowType::PreSnare:
            return bank == TrapHatFxBank::Short32Roll
                || bank == TrapHatFxBank::Ratchet2x
                || bank == TrapHatFxBank::Ratchet3x
                || bank == TrapHatFxBank::PreSnareBurst
                || bank == TrapHatFxBank::TripletEdgeBurst
                || bank == TrapHatFxBank::RampUpBurst;

        case TrapFxWindowType::KickReaction:
            if (bank == TrapHatFxBank::Short64Burst)
                return substyle == TrapSubstyle::RageTrap;
            return bank == TrapHatFxBank::KickReactionBurst
                || bank == TrapHatFxBank::Short32Roll
                || bank == TrapHatFxBank::Ratchet2x
                || bank == TrapHatFxBank::JabBurst
                || bank == TrapHatFxBank::TripletFlutter;

        case TrapFxWindowType::PhraseEdge:
            if (bank == TrapHatFxBank::Ratchet4x)
                return substyle == TrapSubstyle::RageTrap;
            return bank == TrapHatFxBank::PhraseEdgeBurst
                || bank == TrapHatFxBank::Extended32Roll
                || bank == TrapHatFxBank::Short64Burst
                || bank == TrapHatFxBank::RampUpBurst
                || bank == TrapHatFxBank::RampDownBurst
                || bank == TrapHatFxBank::Ratchet3x;

        case TrapFxWindowType::TransitionLift:
            return bank == TrapHatFxBank::RampUpBurst
                || bank == TrapHatFxBank::Short32Roll
                || bank == TrapHatFxBank::Extended32Roll
                || bank == TrapHatFxBank::TripletFlutter
                || bank == TrapHatFxBank::PhraseEdgeBurst;

        default:
            return false;
    }
}

int findStyleWeight(const TrapHatFxStyleProfile& profile, TrapHatFxBank bank)
{
    for (const auto& w : profile.banks)
        if (w.bank == bank)
            return w.weight;
    return 0;
}

float scoreFxWindow(const FxWindow& window,
                    TrapSubstyle substyle,
                    TrapPhraseRole role,
                    const TrackState& hatTrack,
                    const TrackState* openHatTrack,
                    const TrackState* kickTrack,
                    const TrackState* subTrack)
{
    float score = window.baseWeight;

    if (role == TrapPhraseRole::Ending)
        score += 0.22f;
    if (role == TrapPhraseRole::Lift)
        score += 0.12f;

    const int span = HiResTiming::kTicks1_16;
    const int start = window.tick - span;
    const int end = window.tick + span;

    const int localHatHits = countNotesInTickRange(hatTrack, start, end, true);
    if (localHatHits > localHatDensityLimit(substyle))
        score -= 0.42f;

    score -= static_cast<float>(countStrongAccentInRange(openHatTrack, start, end, 70)) * 0.26f;
    score -= static_cast<float>(countStrongAccentInRange(kickTrack, start, end, 96)) * 0.22f;
    score -= static_cast<float>(countStrongAccentInRange(subTrack, start, end, 96)) * 0.16f;

    if (substyle == TrapSubstyle::RageTrap)
        score += 0.1f;
    if (substyle == TrapSubstyle::DarkTrap && window.type == TrapFxWindowType::PhraseEdge)
        score += 0.12f;

    return score;
}

std::vector<FxWindow> detectFxWindows(const TrackState* kickTrack,
                                      const TrackState* snareTrack,
                                      const std::vector<TrapPhraseRole>& phrase,
                                      int bars,
                                      bool allowKickReaction)
{
    std::vector<FxWindow> windows;
    windows.reserve(static_cast<size_t>(bars * 6));

    for (int bar = 0; bar < bars; ++bar)
    {
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        const int barEnd = barStart + HiResTiming::kTicksPerBar4_4;
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;

        windows.push_back({ barEnd - HiResTiming::kTicks1_8, bar, TrapFxWindowType::EndOfBar, 0.9f });
        windows.push_back({ barStart + HiResTiming::kTicksPerQuarter * 2 - HiResTiming::kTicks1_16,
                            bar,
                            TrapFxWindowType::EndOfHalfBar,
                            0.65f });

        if (role == TrapPhraseRole::Ending)
            windows.push_back({ barEnd - HiResTiming::kTicks1_16, bar, TrapFxWindowType::PhraseEdge, 1.0f });
        if (role == TrapPhraseRole::Lift)
            windows.push_back({ barEnd - HiResTiming::kTicks1_8, bar, TrapFxWindowType::TransitionLift, 0.74f });
    }

    if (snareTrack != nullptr)
    {
        for (const auto& sn : snareTrack->notes)
        {
            if (sn.isGhost)
                continue;
            const int tick = std::max(0, HiResTiming::noteTick(sn) - HiResTiming::kTicks1_16);
            const int bar = std::clamp(tick / HiResTiming::kTicksPerBar4_4, 0, std::max(0, bars - 1));
            windows.push_back({ tick, bar, TrapFxWindowType::PreSnare, 0.88f });
        }
    }

    if (kickTrack != nullptr && allowKickReaction)
    {
        for (const auto& k : kickTrack->notes)
        {
            if (isKickAnchor(k))
                continue;
            const int tick = std::max(0, HiResTiming::noteTick(k) + HiResTiming::kTicks1_32);
            const int bar = std::clamp(tick / HiResTiming::kTicksPerBar4_4, 0, std::max(0, bars - 1));
            windows.push_back({ tick, bar, TrapFxWindowType::KickReaction, 0.76f });
        }
    }

    std::sort(windows.begin(), windows.end(), [](const FxWindow& a, const FxWindow& b)
    {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return a.baseWeight > b.baseWeight;
    });

    windows.erase(std::unique(windows.begin(), windows.end(), [](const FxWindow& a, const FxWindow& b)
    {
        return a.bar == b.bar && a.type == b.type && std::abs(a.tick - b.tick) < HiResTiming::kTicks1_64;
    }), windows.end());

    return windows;
}

int hatFxMicroJitter(TrapSubstyle substyle, std::mt19937& rng)
{
    int minJ = -4;
    int maxJ = 5;
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: minJ = -4; maxJ = 5; break;
        case TrapSubstyle::DarkTrap: minJ = -2; maxJ = 8; break;
        case TrapSubstyle::CloudTrap: minJ = -5; maxJ = 6; break;
        case TrapSubstyle::RageTrap: minJ = -3; maxJ = 4; break;
        case TrapSubstyle::MemphisTrap: minJ = -3; maxJ = 4; break;
        case TrapSubstyle::LuxuryTrap: minJ = -2; maxJ = 3; break;
        default: break;
    }

    std::uniform_int_distribution<int> dist(minJ, maxJ);
    return dist(rng);
}

int gridPullStrength(TrapSubstyle substyle, TrapFxWindowType type)
{
    int pull = 4;
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: pull = 3; break;
        case TrapSubstyle::DarkTrap: pull = 4; break;
        case TrapSubstyle::CloudTrap: pull = 4; break;
        case TrapSubstyle::RageTrap: pull = 2; break;
        case TrapSubstyle::MemphisTrap: pull = 2; break;
        case TrapSubstyle::LuxuryTrap: pull = 2; break;
        default: break;
    }

    if (type == TrapFxWindowType::PreSnare || type == TrapFxWindowType::PhraseEdge)
        pull = std::min(pull, 2);

    return pull;
}

int pullTickToGrid(int tick, int division, int maxPull)
{
    const int gridTick = HiResTiming::quantizeTicks(tick, division);
    const int delta = std::clamp(gridTick - tick, -maxPull, maxPull);
    return tick + delta;
}

TrapFxSize clampAllowedSizeByContext(TrapFxSize requested,
                                     const LocalTrapContext& ctx,
                                     TrapSubstyle substyle)
{
    TrapFxSize allowed = requested;

    if (ctx.localHiHatDensity > localHatDensityLimit(substyle))
        allowed = TrapFxSize::Tiny;

    const int strongCount = static_cast<int>(ctx.hasOpenHat) + static_cast<int>(ctx.hasStrongKick) + static_cast<int>(ctx.hasStrongSubRestart);
    if (strongCount >= 2)
        allowed = TrapFxSize::Tiny;
    else if (strongCount == 1 && allowed == TrapFxSize::Major)
        allowed = TrapFxSize::Medium;

    if (ctx.previousWindowHadMajorFx && substyle != TrapSubstyle::RageTrap && allowed == TrapFxSize::Major)
        allowed = TrapFxSize::Medium;

    return allowed;
}

std::optional<const TrapHatFxPattern*> choosePatternForBank(TrapHatFxBank bank,
                                                            TrapSubstyle substyle,
                                                            TrapFxWindowType windowType,
                                                            std::mt19937& rng)
{
    const auto& library = getPatternLibrary();
    std::vector<std::pair<const TrapHatFxPattern*, float>> candidates;

    for (const auto& p : library)
    {
        if (p.bank != bank)
            continue;
        if (!windowVecContains(p.allowedWindows, windowType))
            continue;

        const float w = patternPreferenceWeight(substyle, p.id);
        if (w > 0.01f)
            candidates.push_back({ &p, w });
    }

    if (candidates.empty())
        return std::nullopt;

    float sum = 0.0f;
    for (const auto& c : candidates)
        sum += c.second;

    std::uniform_real_distribution<float> pick(0.0f, std::max(0.01f, sum));
    float r = pick(rng);
    for (const auto& c : candidates)
    {
        r -= c.second;
        if (r <= 0.0f)
            return c.first;
    }

    return candidates.front().first;
}

std::optional<TrapHatFxBank> chooseTrapHatFxBank(TrapSubstyle substyle,
                                                 TrapFxWindowType windowType,
                                                 TrapFxSize maxAllowedSize,
                                                 const LocalTrapContext& ctx,
                                                 const TrapHatFxStyleProfile& profile,
                                                 std::mt19937& rng)
{
    static constexpr std::array<TrapHatFxBank, 14> allBanks = {
        TrapHatFxBank::Short32Roll,
        TrapHatFxBank::Extended32Roll,
        TrapHatFxBank::Short64Burst,
        TrapHatFxBank::Ratchet2x,
        TrapHatFxBank::Ratchet3x,
        TrapHatFxBank::Ratchet4x,
        TrapHatFxBank::TripletFlutter,
        TrapHatFxBank::TripletEdgeBurst,
        TrapHatFxBank::RampUpBurst,
        TrapHatFxBank::RampDownBurst,
        TrapHatFxBank::PhraseEdgeBurst,
        TrapHatFxBank::PreSnareBurst,
        TrapHatFxBank::KickReactionBurst,
        TrapHatFxBank::JabBurst
    };

    std::vector<std::pair<TrapHatFxBank, float>> candidates;

    for (const auto bank : allBanks)
    {
        const auto size = classifyFxSize(bank);
        if (static_cast<int>(size) > static_cast<int>(maxAllowedSize))
            continue;

        if (!isBankAllowedInWindow(bank, windowType, substyle))
            continue;

        const int baseWeight = findStyleWeight(profile, bank);
        if (baseWeight <= 0)
            continue;

        float weight = static_cast<float>(baseWeight);

        if (ctx.localHiHatDensity > 8 && size != TrapFxSize::Tiny)
            weight *= 0.55f;
        if ((ctx.hasOpenHat || ctx.hasStrongKick || ctx.hasStrongSubRestart) && size == TrapFxSize::Major)
            weight *= 0.32f;
        if (ctx.previousWindowHadMajorFx && size == TrapFxSize::Major)
            weight *= (substyle == TrapSubstyle::RageTrap ? 0.56f : 0.1f);

        if (windowType == TrapFxWindowType::PhraseEdge)
        {
            if (bank == TrapHatFxBank::PhraseEdgeBurst || bank == TrapHatFxBank::RampUpBurst || bank == TrapHatFxBank::RampDownBurst)
                weight *= (1.0f + ctx.phraseEdgeStrength * 0.35f);
        }

        if (weight > 0.01f)
            candidates.push_back({ bank, weight });
    }

    if (candidates.empty())
        return std::nullopt;

    float sum = 0.0f;
    for (const auto& c : candidates)
        sum += c.second;

    std::uniform_real_distribution<float> pick(0.0f, std::max(0.01f, sum));
    float r = pick(rng);
    for (const auto& c : candidates)
    {
        r -= c.second;
        if (r <= 0.0f)
            return c.first;
    }

    return candidates.front().first;
}

void suppressOneBaseHatHitNearTick(TrackState& hatTrack, int tick, int tolerance)
{
    if (hatTrack.notes.empty())
        return;

    int bestIdx = -1;
    int bestDist = std::numeric_limits<int>::max();

    for (size_t i = 0; i < hatTrack.notes.size(); ++i)
    {
        const int ht = HiResTiming::noteTick(hatTrack.notes[i]);
        const int d = std::abs(ht - tick);
        if (d <= tolerance && d < bestDist)
        {
            bestDist = d;
            bestIdx = static_cast<int>(i);
        }
    }

    if (bestIdx >= 0)
        hatTrack.notes.erase(hatTrack.notes.begin() + bestIdx);
}

void dedupeAndSortNotes(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        if (a.microOffset != b.microOffset)
            return a.microOffset < b.microOffset;
        if (a.pitch != b.pitch)
            return a.pitch < b.pitch;
        return a.velocity > b.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        return a.step == b.step && a.microOffset == b.microOffset && a.pitch == b.pitch;
    }), notes.end());
}
} // namespace

void TrapHatFxGenerator::generate(TrackState& hatFxTrack,
                                  TrackState& hatTrack,
                                  const TrackState* kickTrack,
                                  const TrackState* snareTrack,
                                  const TrackState* openHatTrack,
                                  const TrackState* subTrack,
                                  const TrapStyleProfile& style,
                                  float fxIntensity,
                                  const std::vector<TrapPhraseRole>& phrase,
                                  std::mt19937& rng) const
{
    hatFxTrack.notes.clear();

    const auto* info = TrackRegistry::find(hatFxTrack.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 44;
    const auto spec = getTrapStyleSpec(style.substyle);
    const auto styleProfile = createStyleProfile(style.substyle);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const int barsFromPhrase = static_cast<int>(phrase.size());
    const int barsFromHat = hatTrack.notes.empty() ? 0 : (hatTrack.notes.back().step / 16) + 1;
    const int bars = std::max(1, std::max(barsFromPhrase, barsFromHat));

    const float baseFxRaw = fxIntensity * style.hatFxIntensity * fxDensityBias(style.substyle) * hatFxModeMultiplier(spec.hatFxMode);
    const float baseFx = std::clamp(baseFxRaw, spec.hatFxDensityMin, spec.hatFxDensityMax);

    const auto windows = detectFxWindows(kickTrack,
                                         snareTrack,
                                         phrase,
                                         bars,
                                         spec.preferKickReactionBursts);

    std::vector<int> tinyPerBar(static_cast<size_t>(bars), 0);
    std::vector<int> mediumPerBar(static_cast<size_t>(bars), 0);
    std::vector<int> majorPerBar(static_cast<size_t>(bars), 0);
    std::vector<int> majorPerTwoBars(static_cast<size_t>((bars + 1) / 2), 0);

    int lastAcceptedWindowTick = std::numeric_limits<int>::min();
    TrapFxSize lastAcceptedSize = TrapFxSize::Tiny;

    for (const auto& window : windows)
    {
        if (window.bar < 0 || window.bar >= bars)
            continue;

        if (!isWindowAllowedByProfile(styleProfile, style.substyle, window.type, rng))
            continue;

        const auto role = window.bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(window.bar)] : TrapPhraseRole::Base;
        const float score = scoreFxWindow(window,
                                          style.substyle,
                                          role,
                                          hatTrack,
                                          openHatTrack,
                                          kickTrack,
                                          subTrack);
        if (score < 0.24f)
            continue;

        const int span = HiResTiming::kTicks1_16;
        const int start = window.tick - span;
        const int end = window.tick + span;

        LocalTrapContext ctx;
        ctx.localHiHatDensity = countNotesInTickRange(hatTrack, start, end, true);
        ctx.hasOpenHat = countStrongAccentInRange(openHatTrack, start, end, 70) > 0;
        ctx.hasStrongKick = countStrongAccentInRange(kickTrack, start, end, 96) > 0;
        ctx.hasStrongSubRestart = countStrongAccentInRange(subTrack, start, end, 96) > 0;
        ctx.previousWindowHadMajorFx = (lastAcceptedSize == TrapFxSize::Major)
            && (lastAcceptedWindowTick != std::numeric_limits<int>::min())
            && (std::abs(window.tick - lastAcceptedWindowTick) < HiResTiming::kTicks1_8);
        ctx.phraseEdgeStrength = role == TrapPhraseRole::Ending ? 1.0f : (role == TrapPhraseRole::Lift ? 0.5f : 0.2f);
        ctx.barIndex = window.bar;
        ctx.beatPosition = (window.tick % HiResTiming::kTicksPerBar4_4) / HiResTiming::kTicksPerQuarter;

        if ((ctx.hasOpenHat || ctx.hasStrongKick || ctx.hasStrongSubRestart) && chance(rng) < 0.35f)
            continue;

        float insertionGate = baseFx * score;
        insertionGate *= fxPhraseEnergyArc(window.bar, bars, role, style.substyle);
        if (role == TrapPhraseRole::Ending)
            insertionGate += 0.08f;
        if (!spec.preferFastBursts)
            insertionGate *= 0.86f;
        if (chance(rng) > std::clamp(insertionGate, 0.02f, 0.94f))
            continue;

        TrapFxSize maxAllowedSize = clampAllowedSizeByContext(TrapFxSize::Major, ctx, style.substyle);
        const size_t twoBarBucket = static_cast<size_t>(window.bar / 2);
        if (majorPerTwoBars[twoBarBucket] >= styleProfile.maxMajorPerTwoBars && maxAllowedSize == TrapFxSize::Major)
            maxAllowedSize = TrapFxSize::Medium;

        std::optional<TrapHatFxBank> selectedBank;
        std::optional<const TrapHatFxPattern*> selectedPattern;
        TrapFxSize selectedSize = TrapFxSize::Tiny;

        for (int attempt = 0; attempt < 4; ++attempt)
        {
            selectedBank = chooseTrapHatFxBank(style.substyle,
                                               window.type,
                                               maxAllowedSize,
                                               ctx,
                                               styleProfile,
                                               rng);
            if (!selectedBank.has_value())
                break;

            selectedSize = classifyFxSize(*selectedBank);
            const auto barIdx = static_cast<size_t>(window.bar);

            if (selectedSize == TrapFxSize::Tiny && tinyPerBar[barIdx] >= styleProfile.maxTinyPerBar)
            {
                selectedBank.reset();
                continue;
            }
            if (selectedSize == TrapFxSize::Medium && mediumPerBar[barIdx] >= styleProfile.maxMediumPerBar)
            {
                maxAllowedSize = TrapFxSize::Tiny;
                continue;
            }
            if (selectedSize == TrapFxSize::Major)
            {
                if (majorPerBar[barIdx] >= styleProfile.maxMajorPerBar)
                {
                    maxAllowedSize = TrapFxSize::Medium;
                    continue;
                }
                if (ctx.previousWindowHadMajorFx)
                {
                    const bool allowRageAdjacent = style.substyle == TrapSubstyle::RageTrap && chance(rng) < 0.3f;
                    if (!allowRageAdjacent)
                    {
                        maxAllowedSize = TrapFxSize::Medium;
                        continue;
                    }
                }
            }

            selectedPattern = choosePatternForBank(*selectedBank, style.substyle, window.type, rng);
            if (!selectedPattern.has_value())
            {
                selectedBank.reset();
                continue;
            }

            if (static_cast<int>(selectedPattern.value()->size) > static_cast<int>(maxAllowedSize))
            {
                if (maxAllowedSize == TrapFxSize::Major)
                    maxAllowedSize = TrapFxSize::Medium;
                else
                    maxAllowedSize = TrapFxSize::Tiny;
                continue;
            }

            break;
        }

        if (!selectedBank.has_value() || !selectedPattern.has_value())
            continue;

        const auto* pattern = *selectedPattern;

        int overlapHits = 0;
        for (const auto& step : pattern->steps)
        {
            const int t = window.tick + step.tickOffset;
            overlapHits += countNotesInTickRange(hatTrack,
                                                 t - HiResTiming::kTicks1_64,
                                                 t + HiResTiming::kTicks1_64,
                                                 true);
        }
        if (overlapHits > static_cast<int>(pattern->steps.size()) * 3)
            continue;

        for (const auto& step : pattern->steps)
        {
            int tick = window.tick + step.tickOffset + step.microOffsetTicks + hatFxMicroJitter(style.substyle, rng);
            const int division = isTripletBank(*selectedBank) ? HiResTiming::kTicks1_24 : HiResTiming::kTicks1_64;
            tick = pullTickToGrid(tick, division, gridPullStrength(style.substyle, window.type));

            if (step.suppressBaseHatAtSameMoment || selectedSize == TrapFxSize::Major)
                suppressOneBaseHatHitNearTick(hatTrack, tick, HiResTiming::kTicks1_64);

            const int velocity = std::clamp(step.velocity, style.hatVelocityMin, style.hatVelocityMax);
            HiResTiming::addNoteAtTick(hatFxTrack,
                                       pitch,
                                       tick,
                                       velocity,
                                       velocity <= 56,
                                       bars);
        }

        const auto barIdx = static_cast<size_t>(window.bar);
        if (selectedSize == TrapFxSize::Tiny)
            tinyPerBar[barIdx] += 1;
        else if (selectedSize == TrapFxSize::Medium)
            mediumPerBar[barIdx] += 1;
        else
        {
            majorPerBar[barIdx] += 1;
            majorPerTwoBars[twoBarBucket] += 1;
        }

        lastAcceptedWindowTick = window.tick;
        lastAcceptedSize = selectedSize;
    }

    dedupeAndSortNotes(hatFxTrack.notes);
    dedupeAndSortNotes(hatTrack.notes);

    const int maxNotes = bars * 72;
    if (static_cast<int>(hatFxTrack.notes.size()) > maxNotes)
        hatFxTrack.notes.resize(static_cast<size_t>(maxNotes));
}
} // namespace bbg
