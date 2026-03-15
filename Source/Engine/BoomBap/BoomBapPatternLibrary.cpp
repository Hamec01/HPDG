#include "BoomBapPatternLibrary.h"

#include <algorithm>

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
constexpr int kAllMasks = kClassicMask | kDustyMask | kJazzyMask | kAggressiveMask | kLaidBackMask | kGoldMask | kRussianUndergroundMask | kLofiRapMask;

std::array<KickHitRole, 8> roles(KickHitRole a, KickHitRole b, KickHitRole c, KickHitRole d, KickHitRole e, KickHitRole f, KickHitRole g, KickHitRole h)
{
    return { a, b, c, d, e, f, g, h };
}

std::array<int, 16> hats(std::initializer_list<int> vals)
{
    std::array<int, 16> out {};
    int i = 0;
    for (const auto v : vals)
        out[static_cast<size_t>(i++)] = v;
    return out;
}

std::array<int, 16> noneSubstitutions()
{
    std::array<int, 16> out {};
    out.fill(-1);
    return out;
}
} // namespace

const std::vector<KickTemplateDefinition>& getBoomBapKickTemplates()
{
    static const std::vector<KickTemplateDefinition> templates = {
        { { 0, 3, 6, 8, 10, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kClassicMask, 0.2f, 0.8f, 0xF },
        { { 0, 4, 7, 10, 11, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kClassicMask, 0.3f, 1.0f, 0xE },
        { { 0, 2, 6, 8, 12, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kClassicMask | kAggressiveMask, 0.35f, 1.0f, 0xE },
        { { 0, 4, 6, 9, 12, 15, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kClassicMask | kLaidBackMask, 0.4f, 1.0f, 0xC },

        { { 0, 3, 10, 11, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kDustyMask, 0.0f, 0.6f, 0xF },
        { { 0, 5, 10, 14, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kDustyMask | kLaidBackMask, 0.1f, 0.7f, 0x9 },
        { { 0, 2, 9, 11, 14, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kDustyMask, 0.2f, 0.85f, 0xD },
        { { 0, 4, 8, 15, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kDustyMask, 0.0f, 0.55f, 0x9 },

        { { 0, 3, 7, 10, 12, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kJazzyMask, 0.25f, 0.9f, 0x6 },
        { { 0, 5, 8, 10, 13, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kJazzyMask | kClassicMask, 0.25f, 0.9f, 0xF },
        { { 0, 4, 7, 11, 14, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kJazzyMask, 0.3f, 1.0f, 0xA },
        { { 0, 2, 6, 10, 14, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kJazzyMask | kLaidBackMask, 0.3f, 1.0f, 0xC },

        { { 0, 2, 4, 6, 8, 10, 12, 14 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Pickup), kAggressiveMask, 0.6f, 1.0f, 0xE },
        { { 0, 3, 6, 7, 10, 12, 15, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup), kAggressiveMask, 0.55f, 1.0f, 0xF },
        { { 0, 4, 5, 8, 10, 12, 14, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup), kAggressiveMask, 0.6f, 1.0f, 0xE },
        { { 0, 2, 6, 9, 10, 12, 15, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup), kAggressiveMask, 0.55f, 1.0f, 0xC },

        { { 0, 6, 10, 12, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kLaidBackMask, 0.1f, 0.65f, 0xF },
        { { 0, 4, 11, 12, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kLaidBackMask | kDustyMask, 0.15f, 0.75f, 0x9 },
        { { 0, 3, 8, 12, 14, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kLaidBackMask, 0.2f, 0.85f, 0xA },
        { { 0, 5, 8, 11, 15, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kLaidBackMask, 0.2f, 0.8f, 0xC },

        { { 0, 3, 5, 10, 11, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kAllMasks, 0.2f, 0.9f, 0xF },
        { { 0, 4, 7, 9, 12, 15, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kAllMasks, 0.35f, 1.0f, 0xE },
        { { 0, 2, 5, 8, 11, 14, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kClassicMask | kAggressiveMask | kJazzyMask, 0.35f, 1.0f, 0xE },
        { { 0, 7, 10, 12, 15, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kAllMasks, 0.25f, 0.95f, 0x8 },
        { { 0, 3, 8, 10, 11, 14, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kClassicMask | kJazzyMask, 0.35f, 1.0f, 0xE },
        { { 0, 2, 4, 8, 9, 12, 15, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup), kAggressiveMask | kClassicMask, 0.5f, 1.0f, 0xE },
        { { 0, 5, 9, 12, 14, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kDustyMask | kLaidBackMask, 0.15f, 0.8f, 0x9 }
    };

    return templates;
}

const std::vector<SnareFeelProfile>& getBoomBapSnareFeelProfiles()
{
    static const std::vector<SnareFeelProfile> profiles = {
        { "Straight", 10, 10, 1.00f, 0.00f },
        { "Lazy", 22, 16, 1.00f, 0.00f },
        { "Beat2HeavierDelay", 24, 10, 0.95f, 0.04f },
        { "Beat4HeavierDelay", 12, 22, 0.95f, 0.04f },
        { "LightGhostBefore2", 16, 11, 0.92f, 0.12f },
        { "LightGhostBefore4", 14, 14, 0.92f, 0.12f }
    };

    return profiles;
}

const std::vector<HatPatternProfile>& getBoomBapHatPatternProfiles()
{
    static const auto noSubs = noneSubstitutions();

    static const std::vector<HatPatternProfile> profiles = {
        { "1/8 Straight", hats({1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0}), hats({90,0,76,0,86,0,74,0,88,0,78,0,86,0,94,0}), noSubs, kAllMasks, 0.0f, 0.7f },
        { "1/8 Swung", hats({1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0}), hats({94,0,72,0,89,0,70,0,92,0,74,0,90,0,98,0}), noSubs, kClassicMask | kLaidBackMask | kDustyMask, 0.1f, 0.8f },
        { "1/16 Soft", hats({1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}), hats({84,62,78,58,82,60,76,58,84,62,78,58,82,60,76,64}), noSubs, kClassicMask | kJazzyMask | kAggressiveMask, 0.45f, 1.0f },
        { "1/16 Swung", hats({1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}), hats({88,60,80,56,86,58,78,54,88,60,80,56,86,58,78,66}), noSubs, kClassicMask | kLaidBackMask, 0.5f, 1.0f },
        { "Broken A", hats({1,0,1,1,1,0,1,0,1,1,0,1,1,0,1,0}), hats({94,0,76,68,88,0,72,0,90,66,0,70,86,0,78,0}), hats({-1,-1,-1,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,12,-1}), kAllMasks, 0.3f, 1.0f },
        { "Broken B", hats({1,1,0,1,1,0,1,1,0,1,1,0,1,1,0,1}), hats({92,70,0,72,88,0,74,80,0,76,84,0,74,86,0,90}), hats({-1,-1,-1,-1,-1,-1,-1,-1,-1,8,-1,-1,-1,-1,-1,14}), kDustyMask | kJazzyMask | kLaidBackMask, 0.25f, 0.95f },
        { "Sparse Dusty", hats({1,0,0,1,1,0,0,0,1,0,1,0,1,0,0,1}), hats({84,0,0,66,80,0,0,0,82,0,72,0,80,0,0,90}), noSubs, kDustyMask, 0.0f, 0.55f },
        { "Jazzy Loose", hats({1,0,1,0,1,1,0,1,1,0,1,0,1,1,0,1}), hats({86,0,78,0,84,62,0,70,88,0,74,0,82,66,0,92}), hats({-1,-1,-1,-1,-1,4,-1,-1,-1,-1,9,-1,-1,-1,-1,14}), kJazzyMask, 0.25f, 0.9f },
        { "Accented Hats", hats({1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0}), hats({98,62,82,0,94,60,80,0,96,62,82,0,94,60,80,0}), noSubs, kAggressiveMask | kClassicMask, 0.45f, 1.0f },
        { "Bar Ending Phrase", hats({1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,1}), hats({90,0,76,0,86,0,74,0,88,0,76,70,82,74,86,98}), noSubs, kAllMasks, 0.25f, 1.0f },
        { "Dusty Sparse Ending", hats({1,0,1,0,0,0,1,0,1,0,0,1,0,1,0,1}), hats({84,0,72,0,0,0,70,0,82,0,0,66,0,72,0,92}), noSubs, kDustyMask | kLaidBackMask, 0.0f, 0.6f },
        { "Aggressive Tight", hats({1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1}), hats({94,66,82,62,92,0,80,68,94,66,84,0,90,68,82,96}), noSubs, kAggressiveMask, 0.55f, 1.0f },
        { "Pocket Push", hats({1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1}), hats({96,0,76,84,92,0,74,0,94,82,88,0,90,0,80,98}), hats({-1,-1,-1,2,-1,-1,-1,-1,-1,8,-1,-1,-1,-1,-1,14}), kClassicMask | kAggressiveMask | kJazzyMask, 0.4f, 1.0f },
        { "Laid Late Hat", hats({1,0,1,0,1,0,1,0,1,0,1,0,1,1,0,1}), hats({88,0,72,0,84,0,70,0,86,0,74,0,82,68,0,94}), hats({-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,12,-1,14}), kLaidBackMask | kDustyMask, 0.2f, 0.8f }
    };

    return profiles;
}

const std::vector<GhostBehaviorPreset>& getBoomBapGhostPresets()
{
    static const std::vector<GhostBehaviorPreset> presets = {
        { "Ghost Kick Pickup", 0.14f, 2, false, kClassicMask | kAggressiveMask },
        { "Sparse Ghost Kick", 0.08f, 1, false, kDustyMask | kLaidBackMask },
        { "Snare Pickup", 0.12f, 2, false, kClassicMask | kLaidBackMask },
        { "Bar-End Ghost", 0.13f, 2, true, kAllMasks },
        { "Dusty Inner Tap", 0.09f, 2, false, kDustyMask },
        { "Jazzy Soft Pickup", 0.11f, 2, true, kJazzyMask }
    };

    return presets;
}

const std::vector<OpenHatPreset>& getBoomBapOpenHatPresets()
{
    static const std::vector<OpenHatPreset> presets = {
        { "End-of-Bar Lift", 0.18f, true, false, kAllMasks },
        { "After-Kick Punctuation", 0.14f, false, true, kClassicMask | kAggressiveMask },
        { "Phrase Marker", 0.20f, true, false, kJazzyMask | kLaidBackMask },
        { "Rare Pickup Accent", 0.08f, false, true, kDustyMask | kLaidBackMask }
    };

    return presets;
}

const std::vector<PercDecorationPreset>& getBoomBapPercPresets()
{
    static const std::vector<PercDecorationPreset> presets = {
        { "Sparse Dusty", 0.08f, 2, false, kDustyMask },
        { "Shaker-Like Light", 0.12f, 3, false, kClassicMask | kJazzyMask },
        { "Rim Punctuation", 0.10f, 2, false, kClassicMask | kAggressiveMask },
        { "Bar 4 Decoration", 0.16f, 3, true, kAllMasks },
        { "Syncopated Soft", 0.13f, 3, false, kJazzyMask | kLaidBackMask },
        { "Almost Empty", 0.05f, 1, false, kLaidBackMask | kDustyMask }
    };

    return presets;
}

int phraseRoleMask(PhraseRole role)
{
    switch (role)
    {
        case PhraseRole::Base: return 1 << 0;
        case PhraseRole::Variation: return 1 << 1;
        case PhraseRole::Contrast: return 1 << 2;
        case PhraseRole::Ending: return 1 << 3;
        default: return 1 << 0;
    }
}

std::vector<const KickTemplateDefinition*> findMatchingKickTemplates(BoomBapSubstyle substyle,
                                                                     float density,
                                                                     PhraseRole role)
{
    std::vector<const KickTemplateDefinition*> out;
    const auto mask = getSubstyleMask(substyle);
    const auto roleMask = phraseRoleMask(role);

    for (const auto& tpl : getBoomBapKickTemplates())
    {
        if ((tpl.substyleMask & mask) == 0)
            continue;
        if ((tpl.phraseMask & roleMask) == 0)
            continue;
        if (density < tpl.minDensity || density > tpl.maxDensity)
            continue;
        out.push_back(&tpl);
    }

    if (out.empty())
    {
        for (const auto& tpl : getBoomBapKickTemplates())
        {
            if ((tpl.substyleMask & mask) != 0)
                out.push_back(&tpl);
        }

        if (out.empty())
        {
            for (const auto& tpl : getBoomBapKickTemplates())
                out.push_back(&tpl);
        }
    }

    return out;
}

const SnareFeelProfile& chooseSnareFeelProfile(BoomBapSubstyle substyle, float density, std::mt19937& rng)
{
    const auto& all = getBoomBapSnareFeelProfiles();
    std::uniform_int_distribution<int> pick(0, static_cast<int>(all.size() - 1));

    if (substyle == BoomBapSubstyle::LaidBack)
        return density < 0.6f ? all[1] : all[2];
    if (substyle == BoomBapSubstyle::Aggressive)
        return density > 0.6f ? all[3] : all[0];
    if (substyle == BoomBapSubstyle::Dusty)
        return density < 0.45f ? all[4] : all[1];

    return all[static_cast<size_t>(pick(rng))];
}

const HatPatternProfile& chooseHatPatternProfile(BoomBapSubstyle substyle, float density, std::mt19937& rng)
{
    const auto& all = getBoomBapHatPatternProfiles();
    std::vector<int> candidates;
    const auto mask = getSubstyleMask(substyle);

    for (int i = 0; i < static_cast<int>(all.size()); ++i)
    {
        const auto& p = all[static_cast<size_t>(i)];
        if ((p.substyleMask & mask) == 0)
            continue;
        if (density < p.minDensity || density > p.maxDensity)
            continue;
        candidates.push_back(i);
    }

    if (candidates.empty())
    {
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
            candidates.push_back(i);
    }

    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
    return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
}

const GhostBehaviorPreset& chooseGhostPreset(BoomBapSubstyle substyle, std::mt19937& rng)
{
    const auto& all = getBoomBapGhostPresets();
    std::vector<int> candidates;
    const auto mask = getSubstyleMask(substyle);

    for (int i = 0; i < static_cast<int>(all.size()); ++i)
    {
        if ((all[static_cast<size_t>(i)].substyleMask & mask) != 0)
            candidates.push_back(i);
    }

    if (candidates.empty())
    {
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
            candidates.push_back(i);
    }

    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
    return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
}

const OpenHatPreset& chooseOpenHatPreset(BoomBapSubstyle substyle, std::mt19937& rng)
{
    const auto& all = getBoomBapOpenHatPresets();
    std::vector<int> candidates;
    const auto mask = getSubstyleMask(substyle);

    for (int i = 0; i < static_cast<int>(all.size()); ++i)
    {
        if ((all[static_cast<size_t>(i)].substyleMask & mask) != 0)
            candidates.push_back(i);
    }

    if (candidates.empty())
    {
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
            candidates.push_back(i);
    }

    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
    return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
}

const PercDecorationPreset& choosePercPreset(BoomBapSubstyle substyle, std::mt19937& rng)
{
    const auto& all = getBoomBapPercPresets();
    std::vector<int> candidates;
    const auto mask = getSubstyleMask(substyle);

    for (int i = 0; i < static_cast<int>(all.size()); ++i)
    {
        if ((all[static_cast<size_t>(i)].substyleMask & mask) != 0)
            candidates.push_back(i);
    }

    if (candidates.empty())
    {
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
            candidates.push_back(i);
    }

    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
    return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
}
} // namespace bbg
