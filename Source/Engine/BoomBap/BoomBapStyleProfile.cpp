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

const std::array<BoomBapStyleProfile, 6>& getBoomBapProfiles()
{
    static const std::array<BoomBapStyleProfile, 6> profiles = {{
        { "Classic", BoomBapSubstyle::Classic, 57.0f, 0.92f, 0.96f, 0.34f, 0.48f, 0.42f, 0.08f, 0.08f, 0.08f, 0.28f, 0.015f, 0.14f, 90, 0.0f, 0.94f, 0.01f, 0.05f, 12, 8, 14, 6, 6, 8, 88, 112, 94, 114, 72, 96, 56, 96, 78, 100, 44, 74, 22, 38, 0.0f, 0.0f, 0.18f, 0.16f, 0.14f, 0.02f, 0.18f, 0.08f },
        { "Dusty", BoomBapSubstyle::Dusty, 59.0f, 0.76f, 0.86f, 0.50f, 0.45f, 0.48f, 0.10f, 0.08f, 0.14f, 0.54f, 0.03f, 0.26f, 172, 0.72f, 0.70f, 0.16f, 0.22f, 20, 16, 24, 6, 10, 14, 82, 104, 84, 106, 76, 94, 50, 88, 70, 92, 38, 70, 30, 50, 0.0f, 0.04f, 0.24f, 0.18f, 0.24f, 0.32f, 0.42f, 0.05f },
        { "Jazzy", BoomBapSubstyle::Jazzy, 63.0f, 0.64f, 0.70f, 0.96f, 0.62f, 0.86f, 0.14f, 0.10f, 0.22f, 0.68f, 0.10f, 0.52f, 188, 0.88f, 0.12f, 0.72f, 0.16f, 10, 12, 16, 6, 7, 18, 48, 92, 72, 104, 58, 86, 54, 92, 66, 92, 42, 82, 32, 58, 0.02f, 0.08f, 0.28f, 0.20f, 0.54f, 0.92f, 0.58f, 0.22f },
        { "BoomBapGold", BoomBapSubstyle::BoomBapGold, 58.5f, 1.06f, 1.02f, 0.52f, 0.30f, 0.38f, 0.06f, 0.05f, 0.08f, 0.74f, 0.035f, 0.18f, 92, 0.02f, 0.92f, 0.02f, 0.06f, 9, 11, 14, 6, 7, 10, 92, 116, 98, 122, 82, 106, 54, 96, 70, 92, 38, 70, 28, 46, 0.0f, 0.02f, 0.16f, 0.10f, 0.22f, 0.03f, 0.24f, 0.05f },
        { "RussianUnderground", BoomBapSubstyle::RussianUnderground, 56.5f, 0.88f, 0.72f, 0.36f, 0.20f, 0.30f, 0.06f, 0.035f, 0.07f, 0.18f, 0.018f, 0.14f, 164, 0.86f, 0.92f, 0.0f, 0.06f, 18, 20, 22, 7, 5, 9, 92, 116, 101, 124, 70, 92, 46, 86, 62, 82, 36, 62, 26, 42, -0.08f, -0.01f, 0.10f, 0.03f, 0.14f, 0.0f, 0.04f, 0.02f },
        { "LofiRap", BoomBapSubstyle::LofiRap, 57.0f, 0.62f, 0.66f, 0.50f, 0.18f, 0.28f, 0.04f, 0.025f, 0.06f, 0.20f, 0.025f, 0.10f, 156, 0.72f, 0.94f, 0.0f, 0.08f, 18, 18, 20, 10, 9, 18, 70, 100, 78, 108, 58, 84, 44, 78, 60, 82, 34, 62, 24, 44, -0.14f, 0.02f, 0.08f, 0.025f, 0.10f, 0.0f, 0.05f, 0.015f }
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
