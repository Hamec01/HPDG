#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

#include "../../Core/TrackType.h"

namespace bbg
{
// Drum & Bass works on a 64-part lattice per bar (1/64 notes, 60 PPQ ticks each):
//   beat = 16, eighth = 8, sixteenth = 4, thirty-second = 2.
// Snare backbone on beats 2 and 4 = ticks 16 and 48.
namespace DnBGrid
{
constexpr int kTicksPerBar = 64;
constexpr int kPpqPerTick = 60;
constexpr int kSnare2 = 16;
constexpr int kSnare4 = 48;

// Metric strength m(t): bar start > beats > eighths > sixteenths > 32nds > 64ths.
inline float metricStrength(int tick)
{
    tick = ((tick % kTicksPerBar) + kTicksPerBar) % kTicksPerBar;
    if (tick == 0) return 1.00f;
    if (tick % 16 == 0) return 0.90f;
    if (tick % 8 == 0) return 0.60f;
    if (tick % 4 == 0) return 0.35f;
    if (tick % 2 == 0) return 0.18f;
    return 0.10f;
}
} // namespace DnBGrid

enum class DnBSubstyle
{
    Modern = 0,
    Roller,
    Liquid,
    Neurofunk,
    JumpUp,
    Breakbeat,
    Count
};

// Every generated note says why it exists; the scorer judges notes by their role.
enum class DnBRole
{
    DownbeatAnchor,
    SnareBackbeat,
    KickAnchor,
    KickPickup,
    KickResponse,
    KickSyncopation,
    GhostPreSnare,
    GhostPostSnare,
    GhostKick,
    HatCarrier,
    HatAccent,
    HatPickup,
    RideCarrier,
    OpenHatLift,
    BreakDetail,
    Fill,
    Turnaround,
    CrashMarker
};

const char* toString(DnBRole role);

// Secondary notes must justify themselves (A/B contribution test in the scorer).
inline bool isSecondaryRole(DnBRole role)
{
    return role == DnBRole::HatAccent || role == DnBRole::HatPickup || role == DnBRole::OpenHatLift
        || role == DnBRole::BreakDetail || role == DnBRole::GhostKick;
}

enum class DnBCarrierMode
{
    EighthOffbeat = 0,
    EighthRolling,
    SixteenthShaker,
    BrokenSixteenth,
    RideDriven,
    SparseBreakHat,
    Count
};

const char* toString(DnBCarrierMode mode);

enum class DnBBarRole
{
    Statement,
    Repeat,
    Response,
    Development,
    Fill
};

const char* toString(DnBBarRole role);

struct DnBEvent
{
    TrackType lane = TrackType::Kick;
    int bar = 0;
    int tick = 0;        // 0..63 on the bar lattice
    int velocity = 100;
    int micro = 0;       // PPQ offset (swing + pocket + humanize)
    DnBRole role = DnBRole::HatCarrier;
    bool ghost = false;
    int length = 2;      // in lattice ticks
    int anchorBar = -1;  // the strong event this note belongs to (ghosts, pickups)
    int anchorTick = -1;

    int absoluteTick() const noexcept { return bar * DnBGrid::kTicksPerBar + tick; }
};

struct DnBStyleProfile
{
    const char* name = "Modern";
    DnBSubstyle substyle = DnBSubstyle::Modern;

    float bpmDefault = 174.0f;
    int bpmMin = 172;
    int bpmMax = 176;
    float swingDefault = 51.0f;
    float densityDefault = 0.50f;
    float humanizeDefault = 0.25f;

    float snareRigidity = 1.0f;     // 1 = strict 2 & 4; lower = break-derived extra / displaced snares
    float kickSyncopation = 0.55f;  // drive towards weak-position kicks
    int minKicks = 2;
    int maxKicks = 3;               // played loops: ~2 kicks per bar (0 + the 2-step kick)
    float answerDropsDownbeat = 0.5f; // the phrase's second bar plays only the 2-step kick (K0 K10 | K10)
    float displacedSnareRate = 0.0f; // the phrase's second bar moves the second snare an 8th: 4 + 10 / 4 + 14
    float twoStepAnchor = 0.85f;    // chance the bar is built on the classic 2-step second kick
    float ghostAmount = 0.35f;
    float ghostPreBias = 0.65f;     // pre-snare vs post-snare ghosts
    float ghostRatioMin = 0.25f;    // ghost / anchor velocity
    float ghostRatioMax = 0.55f;
    std::array<float, static_cast<size_t>(DnBCarrierMode::Count)> carrierWeights {};
    float hatPickupRate = 0.40f;    // 16th pickups (16ths 3 / 9) in an eighth carrier
    float hatAccentRate = 0.30f;
    float openHatRate = 0.25f;
    float breakDetail = 0.10f;
    float ghostKickRate = 0.15f;
    float fillRate = 0.45f;
    float variation = 0.45f;        // chance the answer bar of a phrase is a response, not a repeat
    float accentContrast = 0.70f;
    float microtiming = 0.25f;

    // Scorer targets (style-specific inverted-U centres), calibrated on the grammar's own
    // candidate distribution (Liquid / Jump-Up least syncopated ... Breakbeat most).
    float syncTarget = 0.30f;
    float negativeSpaceTarget = 0.35f;
    float repetitionTarget = 0.75f;
    float forwardTarget = 0.55f;

    // Phrase topology weights: AAAA, AABA, AABB, ABAA, ABAC, AABC.
    std::array<float, 6> topologyWeights {};
};

const DnBStyleProfile& getDnBStyleProfile(int substyleIndex);
juce::StringArray getDnBStyleNames();

struct DnBScore
{
    float anchorClarity = 0.0f;
    float meterStability = 0.0f;
    float interlock = 0.0f;
    float syncopation = 0.0f;
    float syncopationFit = 0.0f;
    float forwardMotion = 0.0f;
    float forwardFit = 0.0f;
    float ghostContext = 1.0f;
    float velocityHierarchy = 1.0f;
    float negativeSpace = 0.0f;
    float negativeSpaceFit = 0.0f;
    float repetition = 0.0f;
    float repetitionFit = 0.0f;
    float variationFit = 1.0f;
    float timingPlausibility = 1.0f;
    float phraseResolution = 1.0f;
    float contrast = 0.0f;
    float penalties = 0.0f;
    float core = 0.0f;
    float secondary = 0.0f;
    float quality = 0.0f;
    bool passedGates = false;
    juce::String failedGate;
};

struct DnBPattern
{
    int bars = 4;
    std::vector<DnBEvent> events;
    std::vector<DnBBarRole> barRoles;
    std::vector<char> barLetters; // phrase topology letter of each bar ('A', 'B', ...)
    juce::String topology;
    DnBCarrierMode carrier = DnBCarrierMode::EighthRolling;
    DnBScore score;
    int prunedSecondary = 0;

    bool has(TrackType lane, int bar, int tick, bool includeGhosts = true) const;
    std::vector<const DnBEvent*> barEvents(int bar) const;
};
} // namespace bbg
