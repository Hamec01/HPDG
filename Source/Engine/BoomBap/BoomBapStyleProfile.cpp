#include "BoomBapStyleProfile.h"

namespace bbg
{
namespace
{
constexpr int kClassicMask = 1 << 0;
constexpr int kDustyMask = 1 << 1;
constexpr int kJazzyMask = 1 << 2;
constexpr int kAggressiveMask = 1 << 3;
constexpr int kLaidBackMask = 1 << 4;
constexpr int kGoldMask = 1 << 5;
constexpr int kRussianUndergroundMask = 1 << 6;
constexpr int kLofiRapMask = 1 << 7;
} // namespace

const std::array<BoomBapStyleProfile, 8>& getBoomBapProfiles()
{
    static const std::array<BoomBapStyleProfile, 8> profiles = {{
        { "Classic", BoomBapSubstyle::Classic, 57.0f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.22f, 0.20f, 0.24f, 1.00f, 0.07f, 0.24f, 102, 0.20f, 0.80f, 0.04f, 0.16f, 18, 10, 22, 8, 5, 15, 88, 110, 92, 112, 84, 104, 58, 98, 82, 106, 48, 82, 35, 56 },
        { "Dusty", BoomBapSubstyle::Dusty, 58.0f, 0.82f, 0.72f, 0.86f, 0.66f, 0.68f, 0.17f, 0.12f, 0.16f, 0.92f, 0.05f, 0.34f, 168, 0.65f, 0.74f, 0.08f, 0.18f, 24, 14, 30, 9, 7, 20, 84, 104, 88, 106, 80, 98, 52, 90, 76, 100, 42, 74, 33, 52 },
        { "Jazzy", BoomBapSubstyle::Jazzy, 62.0f, 0.90f, 0.96f, 1.05f, 1.18f, 1.14f, 0.25f, 0.26f, 0.30f, 0.88f, 0.10f, 0.40f, 184, 0.75f, 0.38f, 0.34f, 0.28f, 18, 11, 24, 9, 6, 18, 84, 106, 90, 108, 80, 102, 54, 94, 78, 104, 44, 80, 35, 56 },
        { "Aggressive", BoomBapSubstyle::Aggressive, 55.0f, 1.24f, 1.12f, 0.84f, 0.74f, 0.70f, 0.14f, 0.12f, 0.14f, 1.00f, 0.03f, 0.18f, 96, 0.05f, 0.88f, 0.03f, 0.09f, 12, 8, 16, 6, 4, 12, 94, 118, 96, 118, 92, 112, 62, 106, 86, 112, 50, 84, 36, 56 },
        { "LaidBack", BoomBapSubstyle::LaidBack, 60.0f, 0.88f, 0.86f, 0.94f, 0.80f, 0.74f, 0.20f, 0.16f, 0.18f, 0.95f, 0.07f, 0.30f, 162, 0.60f, 0.72f, 0.10f, 0.18f, 28, 16, 34, 8, 6, 20, 84, 106, 90, 108, 82, 102, 56, 94, 78, 104, 44, 78, 34, 54 },
        { "BoomBapGold", BoomBapSubstyle::BoomBapGold, 59.0f, 1.02f, 1.04f, 0.98f, 0.92f, 0.86f, 0.20f, 0.21f, 0.19f, 1.00f, 0.08f, 0.33f, 176, 0.72f, 0.62f, 0.10f, 0.28f, 19, 11, 24, 8, 5, 14, 92, 116, 98, 120, 88, 110, 62, 102, 80, 100, 46, 76, 32, 54 },
        { "RussianUnderground", BoomBapSubstyle::RussianUnderground, 56.0f, 0.78f, 0.66f, 0.84f, 0.52f, 0.48f, 0.12f, 0.10f, 0.11f, 0.84f, 0.05f, 0.22f, 170, 0.82f, 0.78f, 0.03f, 0.19f, 20, 12, 18, 7, 4, 12, 90, 112, 100, 122, 86, 106, 54, 88, 72, 92, 42, 66, 28, 48 },
        { "LofiRap", BoomBapSubstyle::LofiRap, 56.0f, 0.74f, 0.82f, 0.62f, 0.48f, 0.42f, 0.08f, 0.07f, 0.10f, 0.82f, 0.04f, 0.12f, 148, 0.55f, 0.72f, 0.07f, 0.21f, 16, 12, 18, 10, 7, 16, 70, 105, 76, 108, 66, 92, 58, 82, 74, 94, 38, 76, 28, 52 }
    }};

    return profiles;
}

const BoomBapStyleProfile& getBoomBapProfile(int index)
{
    const auto& profiles = getBoomBapProfiles();
    if (index < 0 || index >= static_cast<int>(profiles.size()))
        return profiles.front();

    return profiles[static_cast<size_t>(index)];
}

int getSubstyleMask(BoomBapSubstyle substyle)
{
    switch (substyle)
    {
        case BoomBapSubstyle::Classic: return kClassicMask;
        case BoomBapSubstyle::Dusty: return kDustyMask;
        case BoomBapSubstyle::Jazzy: return kJazzyMask;
        case BoomBapSubstyle::Aggressive: return kAggressiveMask;
        case BoomBapSubstyle::LaidBack: return kLaidBackMask;
        case BoomBapSubstyle::BoomBapGold: return kGoldMask;
        case BoomBapSubstyle::RussianUnderground: return kRussianUndergroundMask;
        case BoomBapSubstyle::LofiRap: return kLofiRapMask;
        default: return kClassicMask;
    }
}

float interpretedReferenceTempo(const BoomBapStyleProfile& style)
{
    const float ref = static_cast<float>(style.grooveReferenceBpm);
    return ref >= 150.0f ? ref * 0.5f : ref;
}
} // namespace bbg
