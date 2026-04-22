#include "RapStyleProfile.h"

namespace bbg
{
const std::array<RapStyleProfile, 7>& getRapProfiles()
{
    static const std::array<RapStyleProfile, 7> profiles = {{
        { "EastCoast", RapSubstyle::EastCoast, 54.0f, 0.92f, 0.84f, 0.035f, 0.035f, 0.04f, 0.18f, 0.0f, 0.06f, 0.08f, 88, 118, 98, 124, 50, 86, 28, 48, 42, 70 },
        { "WestCoast", RapSubstyle::WestCoast, 55.5f, 0.90f, 0.88f, 0.16f, 0.06f, 0.06f, 0.50f, 0.0f, 0.12f, 0.30f, 88, 114, 92, 118, 54, 88, 30, 52, 44, 74 },
        { "DirtySouthClassic", RapSubstyle::DirtySouthClassic, 54.5f, 1.12f, 1.10f, 0.22f, 0.045f, 0.10f, 0.78f, 0.0f, 0.20f, 0.22f, 88, 118, 94, 120, 56, 94, 32, 58, 48, 82 },
        { "GermanStreetRap", RapSubstyle::GermanStreetRap, 51.5f, 0.96f, 0.86f, 0.025f, 0.025f, 0.025f, 0.34f, 0.0f, 0.025f, 0.025f, 92, 118, 100, 124, 54, 84, 30, 52, 42, 68 },
        { "RussianRap", RapSubstyle::RussianRap, 50.5f, 0.88f, 0.78f, 0.06f, 0.05f, 0.05f, 0.52f, 0.01f, 0.06f, 0.04f, 90, 112, 104, 124, 54, 84, 30, 50, 42, 68 },
        { "RnBRap", RapSubstyle::RnBRap, 54.5f, 0.82f, 0.88f, 0.14f, 0.10f, 0.06f, 0.56f, 0.05f, 0.08f, 0.30f, 86, 106, 92, 114, 54, 88, 32, 56, 42, 70 },
        { "HardcoreRap", RapSubstyle::HardcoreRap, 52.5f, 1.22f, 1.08f, 0.10f, 0.05f, 0.10f, 0.74f, 0.02f, 0.12f, 0.10f, 96, 122, 104, 126, 62, 100, 34, 56, 48, 82 }
    }};

    return profiles;
}

const RapStyleProfile& getRapProfile(int index)
{
    const auto& profiles = getRapProfiles();
    if (index < 0 || index >= static_cast<int>(profiles.size()))
        return profiles.front();

    return profiles[static_cast<size_t>(index)];
}
} // namespace bbg
