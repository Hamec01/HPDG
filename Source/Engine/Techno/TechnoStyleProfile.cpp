#include "TechnoTypes.h"

#include <algorithm>

namespace bbg
{
const char* toString(TechnoRole role)
{
    switch (role)
    {
        case TechnoRole::KickAxis: return "techno_kick_axis";
        case TechnoRole::KickPickup: return "techno_kick_pickup";
        case TechnoRole::Rumble: return "techno_rumble";
        case TechnoRole::Clap: return "techno_clap";
        case TechnoRole::ClapDisplaced: return "techno_clap_displaced";
        case TechnoRole::HatCarrier: return "techno_hat_carrier";
        case TechnoRole::OffbeatOpen: return "techno_offbeat_open";
        case TechnoRole::RideCarrier: return "techno_ride";
        case TechnoRole::Perc: return "techno_perc";
        case TechnoRole::HatRoll: return "techno_hat_roll";
        case TechnoRole::Crash: return "techno_crash";
    }
    return "techno";
}

const char* toString(TechnoHatMode mode)
{
    switch (mode)
    {
        case TechnoHatMode::Eighths: return "eighths";
        case TechnoHatMode::Sixteenths: return "sixteenths";
        case TechnoHatMode::SixteenthsNoBeat: return "sixteenths off the beat";
        case TechnoHatMode::Offbeat: return "offbeat";
        default: break;
    }
    return "hats";
}

const char* toString(TechnoBarRole role)
{
    switch (role)
    {
        case TechnoBarRole::Main: return "A";
        case TechnoBarRole::Variation: return "A'";
        case TechnoBarRole::Fill: return "F";
    }
    return "?";
}

std::vector<int> TechnoPercCycle::hitsInBar() const
{
    std::vector<int> hits;
    if (polymeter)
    {
        // hit(s) <=> s mod L in P, restarted every bar.
        for (int s = 0; s < TechnoGrid::kStepsPerBar; ++s)
            if (std::find(pulses.begin(), pulses.end(), s % std::max(1, length)) != pulses.end())
                hits.push_back(s);
        return hits;
    }
    // Euclid (Bjorklund-equivalent): hit(i) <=> floor((i + r) k / n) != floor((i + r - 1) k / n),
    // an n-step cycle tiled over the bar.
    const int cycle = std::max(1, n);
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    for (int s = 0; s < TechnoGrid::kStepsPerBar; ++s)
    {
        const int i = s % cycle;
        if (floorDiv((i + rotation) * k, cycle) != floorDiv((i + rotation - 1) * k, cycle))
            hits.push_back(s);
    }
    return hits;
}

juce::String TechnoPercCycle::describe() const
{
    if (polymeter)
    {
        juce::String p;
        for (const int v : pulses)
            p << (p.isEmpty() ? "" : ",") << v;
        return "polymeter " + juce::String(length) + " {" + p + "}";
    }
    return "E(" + juce::String(k) + "," + juce::String(n) + ") rot " + juce::String(rotation);
}

namespace
{
TechnoStyleProfile makePeakTime()
{
    TechnoStyleProfile p;
    p.name = "Peak Time";
    p.substyle = TechnoSubstyle::PeakTime;
    p.bpmDefault = 132.0f; p.bpmMin = 128; p.bpmMax = 135;
    p.pickupKick = 0.18f; p.dropKick = 0.45f; p.rumble = 0.0f;
    p.clap = 0.95f; p.clapDisplaced = 0.12f; p.offbeatOpen = 0.9f;
    p.hatWeights = { 0.15f, 0.55f, 0.25f, 0.05f };
    p.ride = 0.25f; p.perc = 0.75f; p.percPolymeter = 0.2f;
    p.euclids = { { 5, 16 }, { 3, 8 }, { 7, 16 } };
    p.hatRoll = 0.55f; p.crash = 0.7f; p.fill = 0.75f;
    p.syncTarget = 5.0f; p.densityTarget = 0.85f; p.repetitionTarget = 0.94f;
    p.bassArchetypeWeights = { 0.45f, 0.40f, 0.10f, 0.0f, 0.05f, 0.0f };
    p.degreeDistribution = { 0.62f, 0.14f, 0.10f, 0.06f, 0.05f, 0.03f, 0.0f };
    p.degreeInertia = 0.45f;
    return p;
}

TechnoStyleProfile makeHypnotic()
{
    TechnoStyleProfile p;
    p.name = "Hypnotic";
    p.substyle = TechnoSubstyle::Hypnotic;
    p.bpmDefault = 131.0f; p.bpmMin = 128; p.bpmMax = 134;
    p.pickupKick = 0.08f; p.dropKick = 0.3f; p.rumble = 0.75f;
    p.clap = 0.4f; p.clapDisplaced = 0.05f; p.offbeatOpen = 0.6f;
    p.hatWeights = { 0.10f, 0.30f, 0.55f, 0.05f };
    p.ride = 0.35f; p.perc = 0.9f; p.percPolymeter = 0.7f;
    p.euclids = { { 5, 16 }, { 7, 16 }, { 3, 8 } };
    p.hatRoll = 0.25f; p.crash = 0.35f; p.fill = 0.45f;
    p.syncTarget = 5.3f; p.densityTarget = 1.0f; p.repetitionTarget = 0.96f;
    p.bassArchetypeWeights = { 0.15f, 0.30f, 0.50f, 0.0f, 0.05f, 0.0f };
    p.degreeDistribution = { 0.72f, 0.12f, 0.08f, 0.03f, 0.03f, 0.0f, 0.02f };
    p.degreeInertia = 0.55f;
    return p;
}

TechnoStyleProfile makeMinimal()
{
    TechnoStyleProfile p;
    p.name = "Minimal";
    p.substyle = TechnoSubstyle::Minimal;
    p.bpmDefault = 125.0f; p.bpmMin = 122; p.bpmMax = 128;
    p.swingDefault = 54.0f;
    p.pickupKick = 0.12f; p.dropKick = 0.25f; p.rumble = 0.0f;
    p.clap = 0.55f; p.clapDisplaced = 0.2f; p.offbeatOpen = 0.55f;
    p.hatWeights = { 0.50f, 0.15f, 0.15f, 0.20f };
    p.ride = 0.1f; p.perc = 0.8f; p.percPolymeter = 0.3f;
    p.euclids = { { 3, 8 }, { 5, 16 } };
    p.hatRoll = 0.15f; p.crash = 0.25f; p.fill = 0.4f;
    p.syncTarget = 3.4f; p.densityTarget = 0.58f; p.repetitionTarget = 0.96f;
    p.bassArchetypeWeights = { 0.35f, 0.05f, 0.05f, 0.0f, 0.55f, 0.0f };
    p.degreeDistribution = { 0.75f, 0.10f, 0.08f, 0.04f, 0.03f, 0.0f, 0.0f };
    p.degreeInertia = 0.55f;
    return p;
}

TechnoStyleProfile makeDetroit()
{
    TechnoStyleProfile p;
    p.name = "Detroit";
    p.substyle = TechnoSubstyle::Detroit;
    p.bpmDefault = 128.0f; p.bpmMin = 124; p.bpmMax = 132;
    p.swingDefault = 53.0f;
    p.pickupKick = 0.2f; p.dropKick = 0.4f; p.rumble = 0.0f;
    p.clap = 0.85f; p.clapDisplaced = 0.15f; p.offbeatOpen = 0.75f;
    p.hatWeights = { 0.25f, 0.55f, 0.15f, 0.05f };
    p.ride = 0.65f; p.perc = 0.7f; p.percPolymeter = 0.15f;
    p.euclids = { { 5, 16 }, { 3, 8 } };
    p.hatRoll = 0.4f; p.crash = 0.65f; p.fill = 0.65f;
    p.syncTarget = 4.7f; p.densityTarget = 0.9f; p.repetitionTarget = 0.94f;
    p.bassArchetypeWeights = { 0.50f, 0.30f, 0.0f, 0.05f, 0.10f, 0.05f };
    p.degreeDistribution = { 0.50f, 0.12f, 0.14f, 0.10f, 0.10f, 0.04f, 0.0f };
    p.degreeInertia = 0.40f;
    return p;
}

TechnoStyleProfile makeDub()
{
    TechnoStyleProfile p;
    p.name = "Dub";
    p.substyle = TechnoSubstyle::Dub;
    p.bpmDefault = 120.0f; p.bpmMin = 115; p.bpmMax = 125;
    p.swingDefault = 53.0f;
    p.pickupKick = 0.05f; p.dropKick = 0.2f; p.rumble = 0.15f;
    p.clap = 0.55f; p.clapDisplaced = 0.15f; p.offbeatOpen = 0.8f;
    p.hatWeights = { 0.30f, 0.15f, 0.15f, 0.40f };
    p.ride = 0.25f; p.perc = 0.65f; p.percPolymeter = 0.45f;
    p.euclids = { { 3, 8 }, { 5, 16 } };
    p.hatRoll = 0.1f; p.crash = 0.3f; p.fill = 0.35f;
    p.syncTarget = 3.2f; p.densityTarget = 0.55f; p.repetitionTarget = 0.97f;
    p.bassArchetypeWeights = { 0.10f, 0.0f, 0.05f, 0.0f, 0.30f, 0.55f };
    p.degreeDistribution = { 0.55f, 0.10f, 0.20f, 0.07f, 0.08f, 0.0f, 0.0f };
    p.degreeInertia = 0.5f;
    return p;
}

TechnoStyleProfile makeAcid()
{
    TechnoStyleProfile p;
    p.name = "Acid";
    p.substyle = TechnoSubstyle::Acid;
    p.bpmDefault = 133.0f; p.bpmMin = 128; p.bpmMax = 138;
    p.pickupKick = 0.15f; p.dropKick = 0.4f; p.rumble = 0.0f;
    p.clap = 0.8f; p.clapDisplaced = 0.12f; p.offbeatOpen = 0.85f;
    p.hatWeights = { 0.20f, 0.55f, 0.20f, 0.05f };
    p.ride = 0.15f; p.perc = 0.6f; p.percPolymeter = 0.25f;
    p.euclids = { { 5, 16 }, { 3, 8 } };
    p.hatRoll = 0.4f; p.crash = 0.55f; p.fill = 0.6f;
    p.syncTarget = 3.9f; p.densityTarget = 0.7f; p.repetitionTarget = 0.96f;
    p.bassArchetypeWeights = { 0.05f, 0.05f, 0.0f, 0.90f, 0.0f, 0.0f };
    p.degreeDistribution = { 0.42f, 0.20f, 0.10f, 0.10f, 0.10f, 0.03f, 0.05f };
    p.degreeInertia = 0.3f;
    p.bassLow = 33;
    p.bassHigh = 57;
    return p;
}

TechnoStyleProfile makeHard()
{
    TechnoStyleProfile p;
    p.name = "Hard";
    p.substyle = TechnoSubstyle::Hard;
    p.bpmDefault = 145.0f; p.bpmMin = 140; p.bpmMax = 150;
    p.swingDefault = 50.0f;
    p.pickupKick = 0.25f; p.dropKick = 0.5f; p.rumble = 0.55f;
    p.clap = 0.95f; p.clapDisplaced = 0.1f; p.offbeatOpen = 0.85f;
    p.hatWeights = { 0.10f, 0.60f, 0.30f, 0.0f };
    p.ride = 0.2f; p.perc = 0.75f; p.percPolymeter = 0.3f;
    p.euclids = { { 7, 16 }, { 3, 8 }, { 5, 16 } };
    p.hatRoll = 0.6f; p.crash = 0.7f; p.fill = 0.75f;
    p.syncTarget = 5.1f; p.densityTarget = 1.05f; p.repetitionTarget = 0.93f;
    p.bassArchetypeWeights = { 0.15f, 0.50f, 0.35f, 0.0f, 0.0f, 0.0f };
    p.degreeDistribution = { 0.70f, 0.14f, 0.06f, 0.03f, 0.03f, 0.0f, 0.04f };
    p.degreeInertia = 0.5f;
    return p;
}

const std::array<TechnoStyleProfile, static_cast<size_t>(TechnoSubstyle::Count)>& profiles()
{
    static const std::array<TechnoStyleProfile, static_cast<size_t>(TechnoSubstyle::Count)> table {
        makePeakTime(), makeHypnotic(), makeMinimal(), makeDetroit(), makeDub(), makeAcid(), makeHard()
    };
    return table;
}
} // namespace

const TechnoStyleProfile& getTechnoStyleProfile(int substyleIndex)
{
    const auto& table = profiles();
    return table[static_cast<size_t>(std::clamp(substyleIndex, 0, static_cast<int>(table.size()) - 1))];
}

juce::StringArray getTechnoStyleNames()
{
    juce::StringArray names;
    for (const auto& p : profiles())
        names.add(p.name);
    return names;
}

bool TechnoPattern::has(TrackType lane, int bar, int step) const
{
    return std::any_of(events.begin(), events.end(), [&](const TechnoEvent& e)
    {
        return e.lane == lane && e.bar == bar && e.step == step && e.subTick == 0;
    });
}
} // namespace bbg
