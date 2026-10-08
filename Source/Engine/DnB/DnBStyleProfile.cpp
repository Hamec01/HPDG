#include "DnBTypes.h"

#include <algorithm>

namespace bbg
{
const char* toString(DnBRole role)
{
    switch (role)
    {
        case DnBRole::DownbeatAnchor: return "dnb_downbeat_anchor";
        case DnBRole::SnareBackbeat: return "dnb_snare_backbeat";
        case DnBRole::KickAnchor: return "dnb_kick_anchor";
        case DnBRole::KickPickup: return "dnb_kick_pickup";
        case DnBRole::KickResponse: return "dnb_kick_response";
        case DnBRole::KickSyncopation: return "dnb_kick_syncopation";
        case DnBRole::GhostPreSnare: return "dnb_ghost_pre_snare";
        case DnBRole::GhostPostSnare: return "dnb_ghost_post_snare";
        case DnBRole::GhostKick: return "dnb_ghost_kick";
        case DnBRole::HatCarrier: return "dnb_hat_carrier";
        case DnBRole::HatAccent: return "dnb_hat_accent";
        case DnBRole::HatPickup: return "dnb_hat_pickup";
        case DnBRole::RideCarrier: return "dnb_ride_carrier";
        case DnBRole::OpenHatLift: return "dnb_open_hat_lift";
        case DnBRole::BreakDetail: return "dnb_break_detail";
        case DnBRole::Fill: return "dnb_fill";
        case DnBRole::Turnaround: return "dnb_turnaround";
        case DnBRole::CrashMarker: return "dnb_crash_marker";
    }
    return "dnb";
}

const char* toString(DnBCarrierMode mode)
{
    switch (mode)
    {
        case DnBCarrierMode::EighthOffbeat: return "eighth offbeat";
        case DnBCarrierMode::EighthRolling: return "eighth rolling";
        case DnBCarrierMode::SixteenthShaker: return "sixteenth shaker";
        case DnBCarrierMode::BrokenSixteenth: return "broken sixteenth";
        case DnBCarrierMode::RideDriven: return "ride driven";
        case DnBCarrierMode::SparseBreakHat: return "sparse break hat";
        default: break;
    }
    return "carrier";
}

const char* toString(DnBBarRole role)
{
    switch (role)
    {
        case DnBBarRole::Statement: return "statement";
        case DnBBarRole::Repeat: return "repeat";
        case DnBBarRole::Response: return "response";
        case DnBBarRole::Development: return "development";
        case DnBBarRole::Fill: return "fill";
    }
    return "bar";
}

namespace
{
DnBStyleProfile makeModern()
{
    DnBStyleProfile p;
    p.name = "Modern";
    p.substyle = DnBSubstyle::Modern;
    p.displacedSnareRate = 0.30f; // docs/audit/DNB_STAGE.md step 3
    // Clean 2-step backbone, medium kick syncopation, low-medium ghosts, low microtiming.
    p.carrierWeights = { 0.05f, 0.55f, 0.15f, 0.20f, 0.00f, 0.05f }; // offbeat-only hat: 0 / 32 FAZE hat stems, 1 / 26 Freaky (DNB_STAGE.md step 5)
    p.topologyWeights = { 0.25f, 0.30f, 0.15f, 0.15f, 0.05f, 0.10f };
    p.syncTarget = 0.03f;
    p.negativeSpaceTarget = 0.37f;
    p.forwardTarget = 0.30f;
    return p;
}

DnBStyleProfile makeRoller()
{
    DnBStyleProfile p;
    p.name = "Roller";
    p.substyle = DnBSubstyle::Roller;
    p.displacedSnareRate = 0.12f; // docs/audit/DNB_STAGE.md step 3
    // Continuous forward motion, hypnotic repetition (AAAA is welcome).
    p.kickSyncopation = 0.70f;
    p.maxKicks = 4;
    p.answerDropsDownbeat = 0.35f;
    p.ghostAmount = 0.50f;
    p.ghostPreBias = 0.70f;
    p.carrierWeights = { 0.05f, 0.30f, 0.35f, 0.20f, 0.10f, 0.00f }; // offbeat-only hat: 0 / 32 FAZE hat stems, 1 / 26 Freaky (DNB_STAGE.md step 5)
    p.hatAccentRate = 0.35f;
    p.openHatRate = 0.20f;
    p.breakDetail = 0.15f;
    p.fillRate = 0.30f;
    p.variation = 0.30f;
    p.microtiming = 0.30f;
    p.repetitionTarget = 0.88f;
    p.topologyWeights = { 0.45f, 0.30f, 0.10f, 0.10f, 0.00f, 0.05f };
    p.syncTarget = 0.06f;
    p.negativeSpaceTarget = 0.30f;
    p.forwardTarget = 0.38f;
    return p;
}

DnBStyleProfile makeLiquid()
{
    DnBStyleProfile p;
    p.name = "Liquid";
    p.substyle = DnBSubstyle::Liquid;
    p.displacedSnareRate = 0.15f; // docs/audit/DNB_STAGE.md step 3
    p.bpmDefault = 172.0f;
    p.bpmMin = 170;
    p.bpmMax = 174;
    p.swingDefault = 52.0f;
    p.humanizeDefault = 0.30f;
    // Softer dynamics, more space, fewer aggressive gestures.
    p.kickSyncopation = 0.40f;
    p.maxKicks = 3;
    p.twoStepAnchor = 0.80f;
    p.ghostAmount = 0.50f;
    p.ghostPreBias = 0.60f;
    p.ghostRatioMin = 0.20f;
    p.ghostRatioMax = 0.45f;
    p.carrierWeights = { 0.10f, 0.40f, 0.15f, 0.10f, 0.25f, 0.00f }; // offbeat-only hat: 0 / 32 FAZE hat stems, 1 / 26 Freaky (DNB_STAGE.md step 5)
    p.hatAccentRate = 0.20f;
    p.openHatRate = 0.30f;
    p.breakDetail = 0.05f;
    p.ghostKickRate = 0.10f;
    p.fillRate = 0.35f;
    p.accentContrast = 0.50f;
    p.microtiming = 0.35f;
    p.repetitionTarget = 0.78f;
    p.topologyWeights = { 0.25f, 0.35f, 0.15f, 0.15f, 0.05f, 0.05f };
    p.syncTarget = 0.02f;
    p.negativeSpaceTarget = 0.38f;
    p.forwardTarget = 0.28f;
    return p;
}

DnBStyleProfile makeNeurofunk()
{
    DnBStyleProfile p;
    p.name = "Neurofunk";
    p.substyle = DnBSubstyle::Neurofunk;
    p.displacedSnareRate = 0.35f; // docs/audit/DNB_STAGE.md step 3
    p.swingDefault = 50.0f;
    p.humanizeDefault = 0.15f;
    // Very rigid snare, high kick syncopation, precise and controlled - aggressive != random.
    p.kickSyncopation = 0.80f;
    p.maxKicks = 4;
    p.answerDropsDownbeat = 0.30f;
    p.ghostAmount = 0.25f;
    p.ghostPreBias = 0.50f;
    p.ghostRatioMin = 0.30f;
    p.carrierWeights = { 0.05f, 0.35f, 0.25f, 0.30f, 0.00f, 0.05f }; // offbeat-only hat: 0 / 32 FAZE hat stems, 1 / 26 Freaky (DNB_STAGE.md step 5)
    p.hatAccentRate = 0.35f;
    p.openHatRate = 0.15f;
    p.fillRate = 0.40f;
    p.variation = 0.50f;
    p.accentContrast = 0.85f;
    p.microtiming = 0.10f;
    p.repetitionTarget = 0.72f;
    p.topologyWeights = { 0.20f, 0.30f, 0.20f, 0.15f, 0.10f, 0.05f };
    p.syncTarget = 0.06f;
    p.negativeSpaceTarget = 0.34f;
    p.forwardTarget = 0.35f;
    return p;
}

DnBStyleProfile makeJumpUp()
{
    DnBStyleProfile p;
    p.name = "Jump-Up";
    p.substyle = DnBSubstyle::JumpUp;
    p.displacedSnareRate = 0.10f; // docs/audit/DNB_STAGE.md step 3
    p.swingDefault = 50.0f;
    p.humanizeDefault = 0.15f;
    // Obvious BANG -> answer -> BANG: clear, repetitive, few ghosts.
    p.kickSyncopation = 0.35f;
    p.maxKicks = 3;
    p.answerDropsDownbeat = 0.30f;
    p.twoStepAnchor = 0.92f;
    p.ghostAmount = 0.15f;
    p.carrierWeights = { 0.10f, 0.60f, 0.15f, 0.10f, 0.00f, 0.05f }; // offbeat-only hat: 0 / 32 FAZE hat stems, 1 / 26 Freaky (DNB_STAGE.md step 5)
    p.hatAccentRate = 0.25f;
    p.openHatRate = 0.30f;
    p.breakDetail = 0.0f;
    p.ghostKickRate = 0.05f;
    p.fillRate = 0.35f;
    p.variation = 0.35f;
    p.accentContrast = 0.90f;
    p.microtiming = 0.10f;
    p.repetitionTarget = 0.86f;
    p.topologyWeights = { 0.45f, 0.35f, 0.10f, 0.10f, 0.00f, 0.00f };
    p.syncTarget = 0.02f;
    p.negativeSpaceTarget = 0.42f;
    p.forwardTarget = 0.25f;
    return p;
}

DnBStyleProfile makeBreakbeat()
{
    DnBStyleProfile p;
    p.name = "Breakbeat";
    p.substyle = DnBSubstyle::Breakbeat;
    p.displacedSnareRate = 0.0f; // docs/audit/DNB_STAGE.md step 3: kicks moved away from both packs
    p.bpmDefault = 170.0f;
    p.bpmMin = 164;
    p.bpmMax = 174;
    p.swingDefault = 53.0f;
    p.humanizeDefault = 0.35f;
    p.densityDefault = 0.60f;
    // Jungle-leaning: looser snare, busy kicks, many ghosts, break details, more variation.
    p.snareRigidity = 0.70f;
    p.kickSyncopation = 0.90f;
    p.maxKicks = 6;
    p.answerDropsDownbeat = 0.25f;
    p.twoStepAnchor = 0.60f;
    p.ghostAmount = 0.80f;
    p.ghostPreBias = 0.55f;
    p.ghostRatioMax = 0.60f;
    p.carrierWeights = { 0.00f, 0.20f, 0.25f, 0.40f, 0.05f, 0.10f };
    p.hatAccentRate = 0.40f;
    p.openHatRate = 0.25f;
    p.breakDetail = 0.60f;
    p.ghostKickRate = 0.25f;
    p.fillRate = 0.55f;
    p.variation = 0.60f;
    p.microtiming = 0.45f;
    p.repetitionTarget = 0.62f;
    p.topologyWeights = { 0.10f, 0.25f, 0.20f, 0.15f, 0.15f, 0.15f };
    p.syncTarget = 0.08f;
    p.negativeSpaceTarget = 0.25f;
    p.forwardTarget = 0.48f;
    return p;
}
} // namespace

const DnBStyleProfile& getDnBStyleProfile(int substyleIndex)
{
    static const std::array<DnBStyleProfile, static_cast<size_t>(DnBSubstyle::Count)> profiles {
        makeModern(), makeRoller(), makeLiquid(), makeNeurofunk(), makeJumpUp(), makeBreakbeat()
    };
    const int index = std::clamp(substyleIndex, 0, static_cast<int>(profiles.size()) - 1);
    return profiles[static_cast<size_t>(index)];
}

juce::StringArray getDnBStyleNames()
{
    juce::StringArray names;
    for (int i = 0; i < static_cast<int>(DnBSubstyle::Count); ++i)
        names.add(getDnBStyleProfile(i).name);
    return names;
}

bool DnBPattern::has(TrackType lane, int bar, int tick, bool includeGhosts) const
{
    return std::any_of(events.begin(), events.end(), [&](const DnBEvent& e)
    {
        return e.lane == lane && e.bar == bar && e.tick == tick && (includeGhosts || !e.ghost);
    });
}

std::vector<const DnBEvent*> DnBPattern::barEvents(int bar) const
{
    std::vector<const DnBEvent*> out;
    for (const auto& e : events)
        if (e.bar == bar)
            out.push_back(&e);
    return out;
}
} // namespace bbg
