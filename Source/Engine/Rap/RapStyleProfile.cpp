#include "RapStyleProfile.h"

namespace bbg
{
const std::array<RapStyleProfile, 7>& getRapProfiles()
{
    static const std::array<RapStyleProfile, 7> profiles = {{
        { "EastCoast", RapSubstyle::EastCoast, 53.0f, 1.00f, 0.95f, 0.10f, 0.08f, 0.08f, 0.68f, 0.04f, 0.10f, 0.10f, 92, 116, 98, 123, 60, 96, 34, 58, 48, 82 },
        { "WestCoast", RapSubstyle::WestCoast, 54.0f, 0.94f, 0.92f, 0.22f, 0.07f, 0.08f, 0.62f, 0.04f, 0.09f, 0.24f, 90, 112, 94, 118, 58, 94, 34, 56, 46, 78 },
        { "DirtySouthClassic", RapSubstyle::DirtySouthClassic, 52.0f, 1.18f, 1.05f, 0.14f, 0.06f, 0.12f, 0.62f, 0.03f, 0.12f, 0.12f, 94, 120, 96, 122, 60, 98, 34, 58, 48, 82 },
        { "GermanStreetRap", RapSubstyle::GermanStreetRap, 51.0f, 0.98f, 0.82f, 0.08f, 0.05f, 0.06f, 0.58f, 0.02f, 0.07f, 0.06f, 92, 116, 102, 124, 56, 88, 32, 54, 44, 74 },
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
