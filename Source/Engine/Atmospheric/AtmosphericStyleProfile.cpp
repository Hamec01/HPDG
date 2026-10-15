#include "AtmosphericTypes.h"

#include <algorithm>

namespace bbg
{
const char* toString(AtmosRole role)
{
    switch (role)
    {
        case AtmosRole::Kick: return "atmos_kick";
        case AtmosRole::KickExtra: return "atmos_kick_extra";
        case AtmosRole::Clap: return "atmos_clap";
        case AtmosRole::SnarePickup: return "atmos_snare_pickup";
        case AtmosRole::Hat: return "atmos_hat";
        case AtmosRole::OpenHat: return "atmos_open_hat";
    }
    return "atmos";
}

bool AtmosPattern::has(TrackType lane, int bar, int step) const
{
    return std::any_of(events.begin(), events.end(), [&](const AtmosEvent& e) { return e.lane == lane && e.bar == bar && e.step == step; });
}

namespace
{
// Profiles and traits: docs/audit/atmos/izzamuzzic_families.txt (16-step rows k / s / h, 16th 1 = index 0).
std::array<AtmosStyleProfile, static_cast<size_t>(AtmosphericSubstyle::Count)> makeProfiles()
{
    std::array<AtmosStyleProfile, static_cast<size_t>(AtmosphericSubstyle::Count)> p;

    auto& pulse = p[0]; // family A, 27 tracks / 761 bars
    pulse.name = "Pulse";
    pulse.substyle = AtmosphericSubstyle::Pulse;
    pulse.bpmDefault = 128.0f;
    pulse.bpmMin = 118;
    pulse.bpmMax = 132;
    pulse.kickProfile = { 0.72f, 0.00f, 0.03f, 0.00f, 0.50f, 0.00f, 0.04f, 0.00f, 0.70f, 0.00f, 0.03f, 0.00f, 0.51f, 0.00f, 0.04f, 0.02f };
    pulse.snareProfile = { 0.08f, 0.02f, 0.05f, 0.07f, 0.25f, 0.03f, 0.08f, 0.04f, 0.07f, 0.03f, 0.05f, 0.07f, 0.28f, 0.01f, 0.09f, 0.02f };
    pulse.hatProfile = { 0.21f, 0.03f, 0.45f, 0.14f, 0.32f, 0.05f, 0.42f, 0.09f, 0.22f, 0.04f, 0.43f, 0.16f, 0.27f, 0.06f, 0.41f, 0.15f };
    // Scorer targets are for the loop = the main section. The passport counts whole songs (2.6 kicks,
    // 1.1 snares, 3.6 hats per bar including sections without clap / hats); where the clap plays it
    // is on both 5 and 13, where the kick plays it is "four" or "half".
    pulse.kicksPerBar = 3.8f;
    pulse.snaresPerBar = 2.3f;
    pulse.hatsPerBar = 5.0f;
    pulse.kickOffBeat = 0.0f;
    pulse.snareOff24 = 0.15f;
    pulse.kickRepeat = 0.90f;
    pulse.bassNotesPerBar = 2.4f;
    pulse.bassPitchChangesPerBar = 1.6f;
    pulse.bassMedianBeats = 0.96f;
    pulse.bassLongShare = 0.26f;
    pulse.intervalMix = { 0.22f, 0.29f, 0.14f, 0.19f, 0.07f };
    pulse.kickRelation = { 0.23f, 0.26f, 0.20f, 0.31f };
    pulse.bassRegister = 34;

    auto& two = p[1]; // family B, 9 tracks / 249 bars
    two.name = "Two-Step";
    two.substyle = AtmosphericSubstyle::TwoStep;
    two.bpmDefault = 170.0f;
    two.bpmMin = 120;
    two.bpmMax = 174;
    two.kickProfile = { 0.65f, 0.00f, 0.03f, 0.02f, 0.04f, 0.01f, 0.03f, 0.06f, 0.10f, 0.02f, 0.67f, 0.04f, 0.08f, 0.05f, 0.03f, 0.03f };
    two.snareProfile = { 0.08f, 0.02f, 0.12f, 0.02f, 0.72f, 0.00f, 0.04f, 0.12f, 0.04f, 0.15f, 0.02f, 0.06f, 0.72f, 0.03f, 0.14f, 0.09f };
    two.hatProfile = { 0.45f, 0.07f, 0.35f, 0.16f, 0.30f, 0.02f, 0.34f, 0.26f, 0.55f, 0.24f, 0.43f, 0.11f, 0.31f, 0.04f, 0.31f, 0.11f };
    two.kicksPerBar = 1.8f;
    two.snaresPerBar = 2.1f;
    two.hatsPerBar = 3.5f;
    two.kickOffBeat = 0.54f;
    two.snareOff24 = 0.35f;
    two.kickRepeat = 0.50f;
    two.bassNotesPerBar = 1.5f;
    two.bassPitchChangesPerBar = 1.0f;
    two.bassMedianBeats = 0.79f;
    two.bassLongShare = 0.11f;
    two.intervalMix = { 0.32f, 0.25f, 0.19f, 0.12f, 0.02f };
    two.kickRelation = { 0.30f, 0.14f, 0.08f, 0.47f };
    two.bassRegister = 34;

    auto& broken = p[2]; // family D1, 6 tracks / 181 bars
    broken.name = "Broken";
    broken.substyle = AtmosphericSubstyle::Broken;
    broken.bpmDefault = 128.0f;
    broken.bpmMin = 75;
    broken.bpmMax = 150;
    broken.kickProfile = { 0.25f, 0.14f, 0.16f, 0.06f, 0.19f, 0.23f, 0.13f, 0.10f, 0.17f, 0.17f, 0.15f, 0.05f, 0.20f, 0.30f, 0.02f, 0.23f };
    broken.snareProfile = { 0.19f, 0.14f, 0.20f, 0.18f, 0.59f, 0.12f, 0.21f, 0.30f, 0.20f, 0.14f, 0.11f, 0.22f, 0.57f, 0.12f, 0.22f, 0.25f };
    broken.hatProfile = { 0.19f, 0.25f, 0.30f, 0.29f, 0.20f, 0.23f, 0.41f, 0.14f, 0.24f, 0.31f, 0.29f, 0.24f, 0.29f, 0.16f, 0.44f, 0.29f };
    // Loop targets: the passport's 0.11 kick repeat is a song re-composed over minutes (and partly
    // transcription noise, tempo conf 0.75); a loop that re-composes every bar sounds like unconnected
    // hits (maintainer, 2026-10-14), so the loop keeps its motif and answers it.
    broken.kicksPerBar = 3.3f;
    broken.snaresPerBar = 3.7f;
    broken.hatsPerBar = 4.3f;
    broken.kickOffBeat = 0.68f;
    broken.snareOff24 = 0.45f;
    broken.kickRepeat = 0.50f;
    broken.bassNotesPerBar = 2.3f;
    broken.bassPitchChangesPerBar = 2.1f;
    broken.bassMedianBeats = 1.27f;
    broken.bassLongShare = 0.35f;
    broken.intervalMix = { 0.06f, 0.32f, 0.35f, 0.18f, 0.01f };
    broken.kickRelation = { 0.34f, 0.11f, 0.16f, 0.39f };
    broken.bassRegister = 36;

    for (auto& s : p)
    {
        float total = 0.0f;
        for (const auto v : s.intervalMix)
            total += v;
        for (auto& v : s.intervalMix)
            v /= total;
        total = 0.0f;
        for (const auto v : s.kickRelation)
            total += v;
        for (auto& v : s.kickRelation)
            v /= total;
    }
    return p;
}
} // namespace

const AtmosStyleProfile& getAtmosStyleProfile(int substyleIndex)
{
    static const auto profiles = makeProfiles();
    return profiles[static_cast<size_t>(std::clamp(substyleIndex, 0, static_cast<int>(profiles.size()) - 1))];
}

juce::StringArray getAtmosStyleNames()
{
    juce::StringArray names;
    for (int i = 0; i < static_cast<int>(AtmosphericSubstyle::Count); ++i)
        names.add(getAtmosStyleProfile(i).name);
    return names;
}
} // namespace bbg
