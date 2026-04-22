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

        { { 0, 3, 10, 11, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kGoldMask, 0.0f, 0.75f, 0xF },
        { { 0, 6, 8, 10, 11, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kGoldMask, 0.2f, 1.0f, 0xF },
        { { 0, 3, 8, 10, 14, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kGoldMask, 0.25f, 1.0f, 0xE },
        { { 0, 5, 10, 11, 14, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kGoldMask, 0.25f, 0.9f, 0xD },

        { { 0, 7, 10, -1, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kRussianUndergroundMask, 0.0f, 0.72f, 0xF },
        { { 0, 5, 10, 11, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kRussianUndergroundMask, 0.18f, 0.9f, 0xF },
        { { 0, 3, 8, 11, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kRussianUndergroundMask, 0.15f, 0.85f, 0xE },
        { { 0, 6, 10, 15, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kRussianUndergroundMask, 0.25f, 1.0f, 0xD },

        { { 0, 10, -1, -1, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kLofiRapMask, 0.0f, 0.66f, 0x7 },
        { { 0, 8, 11, -1, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kLofiRapMask, 0.0f, 0.82f, 0xF },
        { { 0, 3, 10, -1, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kLofiRapMask, 0.12f, 0.88f, 0xE },
        { { 0, 6, 10, 14, -1, -1, -1, -1 }, roles(KickHitRole::Anchor, KickHitRole::Support, KickHitRole::Anchor, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup, KickHitRole::Pickup), kLofiRapMask, 0.22f, 1.0f, 0xD },

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
        { "Straight", 10, 10, 5, 5, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.04f, false, false },
        { "Lazy", 22, 16, 7, 6, 0.96f, 0.04f, 0.04f, 0.05f, 0.04f, 0.08f, false, false },
        { "Beat2Pocket", 24, 11, 8, 5, 0.92f, 0.08f, 0.12f, 0.05f, 0.08f, 0.10f, true, false },
        { "Beat4Pocket", 12, 22, 5, 8, 0.92f, 0.08f, 0.05f, 0.12f, 0.08f, 0.10f, true, false },
        { "GhostBefore2", 16, 11, 7, 5, 0.86f, 0.14f, 0.26f, 0.10f, 0.10f, 0.08f, true, false },
        { "GhostBefore4", 14, 14, 6, 7, 0.86f, 0.14f, 0.10f, 0.26f, 0.10f, 0.08f, true, false },
        { "DryUnderground", 18, 14, 5, 5, 0.58f, 0.04f, 0.02f, 0.04f, 0.02f, 0.05f, true, true },
        { "DustySoft", 20, 18, 6, 6, 0.72f, 0.08f, 0.10f, 0.10f, 0.04f, 0.06f, true, false },
        { "LofiMellow", 26, 20, 8, 7, 0.48f, 0.06f, 0.06f, 0.08f, 0.04f, 0.04f, true, true },
        { "GoldCrisp", 11, 13, 5, 5, 0.98f, 0.12f, 0.08f, 0.14f, 0.12f, 0.14f, false, false },
        { "AggressiveTight", 8, 10, 4, 4, 0.90f, 0.05f, 0.04f, 0.06f, 0.02f, 0.18f, false, false },
        { "JazzyPushPull", 17, 23, 7, 8, 0.80f, 0.16f, 0.16f, 0.20f, 0.16f, 0.10f, true, false }
    };

    return profiles;
}

const std::vector<HatPatternProfile>& getBoomBapHatPatternProfiles()
{
    static const auto noSubs = noneSubstitutions();

    static const std::vector<HatPatternProfile> profiles = {
        { "1/8 Straight", hats({1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0}), hats({90,0,76,0,86,0,74,0,88,0,78,0,86,0,94,0}), noSubs, kAllMasks, 0.0f, 0.7f },
        { "1/8 Swung", hats({1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0}), hats({94,0,72,0,89,0,70,0,92,0,74,0,90,0,98,0}), noSubs, kClassicMask | kLaidBackMask | kDustyMask, 0.1f, 0.8f },
        { "Gold Swung Eighth", hats({1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0}), hats({96,0,70,0,90,0,68,0,94,0,72,0,90,0,98,0}), noSubs, kGoldMask, 0.0f, 1.0f },
        { "Gold Dusty Broken", hats({1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0}), hats({94,0,70,0,88,0,66,58,92,0,70,0,88,0,74,0}), hats({-1,-1,-1,-1,-1,-1,-1,6,-1,-1,-1,-1,-1,-1,-1,-1}), kGoldMask, 0.35f, 1.0f },
        { "Russian Basement Eighth", hats({1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0}), hats({82,0,62,0,78,0,60,0,80,0,64,0,82,0,68,0}), noSubs, kRussianUndergroundMask, 0.0f, 1.0f },
        { "Russian Broken Sparse", hats({1,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0}), hats({80,0,60,0,76,0,0,0,78,0,62,0,80,0,66,0}), noSubs, kRussianUndergroundMask, 0.0f, 0.72f },
        { "Russian Courtyard Drag", hats({1,0,0,0,1,0,1,0,1,0,0,1,1,0,1,0}), hats({78,0,0,0,80,0,58,0,76,0,0,54,82,0,64,0}), hats({-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,10,-1,-1,-1,-1}), kRussianUndergroundMask, 0.25f, 1.0f },
        { "Lofi Dusty Eighth", hats({1,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0}), hats({70,0,54,0,66,0,0,0,68,0,56,0,70,0,58,0}), noSubs, kLofiRapMask, 0.0f, 1.0f },
        { "Lofi Tape Holes", hats({1,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0}), hats({68,0,52,0,64,0,50,0,66,0,0,0,68,0,56,0}), noSubs, kLofiRapMask, 0.0f, 0.76f },
        { "Lofi Lazy Drag", hats({1,0,0,0,1,0,1,0,1,0,1,0,1,0,0,1}), hats({66,0,0,0,68,0,50,0,64,0,54,0,68,0,0,52}), hats({-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,14}), kLofiRapMask, 0.18f, 1.0f },
        { "1/16 Soft", hats({1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}), hats({84,62,78,58,82,60,76,58,84,62,78,58,82,60,76,64}), noSubs, kClassicMask | kJazzyMask | kAggressiveMask, 0.45f, 1.0f },
        { "1/16 Swung", hats({1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}), hats({88,60,80,56,86,58,78,54,88,60,80,56,86,58,78,66}), noSubs, kClassicMask | kLaidBackMask, 0.5f, 1.0f },
        { "Broken A", hats({1,0,1,1,1,0,1,0,1,1,0,1,1,0,1,0}), hats({94,0,76,68,88,0,72,0,90,66,0,70,86,0,78,0}), hats({-1,-1,-1,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,12,-1}), kAllMasks, 0.3f, 1.0f },
        { "Broken B", hats({1,1,0,1,1,0,1,1,0,1,1,0,1,1,0,1}), hats({92,70,0,72,88,0,74,80,0,76,84,0,74,86,0,90}), hats({-1,-1,-1,-1,-1,-1,-1,-1,-1,8,-1,-1,-1,-1,-1,14}), kDustyMask | kJazzyMask | kLaidBackMask, 0.25f, 0.95f },
        { "Sparse Dusty", hats({1,0,0,1,1,0,0,0,1,0,1,0,1,0,0,1}), hats({84,0,0,66,80,0,0,0,82,0,72,0,80,0,0,90}), noSubs, kDustyMask, 0.0f, 0.55f },
        { "Jazzy Loose", hats({1,0,1,0,1,1,0,1,1,0,1,0,1,1,0,1}), hats({86,0,78,0,84,62,0,70,88,0,74,0,82,66,0,92}), hats({-1,-1,-1,-1,-1,4,-1,-1,-1,-1,9,-1,-1,-1,-1,14}), kJazzyMask, 0.25f, 0.9f },
        { "Jazz Foot 2 And 4", hats({0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0}), hats({0,0,0,0,88,0,0,0,0,0,0,0,92,0,0,0}), noSubs, kJazzyMask, 0.0f, 1.0f },
        { "Accented Hats", hats({1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0}), hats({98,62,82,0,94,60,80,0,96,62,82,0,94,60,80,0}), noSubs, kAggressiveMask | kClassicMask, 0.45f, 1.0f },
        { "Bar Ending Phrase", hats({1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,1}), hats({90,0,76,0,86,0,74,0,88,0,76,70,82,74,86,98}), noSubs, kAllMasks, 0.25f, 1.0f },
        { "Dusty Sparse Ending", hats({1,0,1,0,0,0,1,0,1,0,0,1,0,1,0,1}), hats({84,0,72,0,0,0,70,0,82,0,0,66,0,72,0,92}), noSubs, kDustyMask | kLaidBackMask | kRussianUndergroundMask, 0.0f, 0.6f },
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
        { "Sparse Ghost Kick", 0.08f, 1, false, kDustyMask | kLaidBackMask | kRussianUndergroundMask | kLofiRapMask },
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
        { "Rare Pickup Accent", 0.08f, false, true, kDustyMask | kLaidBackMask | kRussianUndergroundMask | kLofiRapMask }
    };

    return presets;
}

const std::vector<PercDecorationPreset>& getBoomBapPercPresets()
{
    static const std::vector<PercDecorationPreset> presets = {
        { "Sparse Dusty", 0.08f, 2, false, kDustyMask | kRussianUndergroundMask | kLofiRapMask },
        { "Shaker-Like Light", 0.12f, 3, false, kClassicMask | kJazzyMask },
        { "Rim Punctuation", 0.10f, 2, false, kClassicMask | kAggressiveMask },
        { "Bar 4 Decoration", 0.16f, 3, true, kAllMasks },
        { "Syncopated Soft", 0.13f, 3, false, kJazzyMask | kLaidBackMask },
        { "Almost Empty", 0.05f, 1, false, kLaidBackMask | kDustyMask | kRussianUndergroundMask | kLofiRapMask }
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
    std::vector<int> candidates;

    const auto addNamedCandidate = [&all, &candidates](const char* profileName, int weight)
    {
        if (weight <= 0)
            return;

        for (int i = 0; i < static_cast<int>(all.size()); ++i)
        {
            if (all[static_cast<size_t>(i)].name != profileName)
                continue;

            for (int copy = 0; copy < weight; ++copy)
                candidates.push_back(i);
            break;
        }
    };

    switch (substyle)
    {
        case BoomBapSubstyle::Classic:
            if (density < 0.40f)
            {
                addNamedCandidate("Straight", 5);
                addNamedCandidate("Lazy", 3);
                addNamedCandidate("DryUnderground", 2);
            }
            else if (density < 0.65f)
            {
                addNamedCandidate("Straight", 4);
                addNamedCandidate("Lazy", 3);
                addNamedCandidate("DryUnderground", 2);
                addNamedCandidate("Beat2Pocket", 1);
                addNamedCandidate("Beat4Pocket", 1);
            }
            else
            {
                addNamedCandidate("Straight", 3);
                addNamedCandidate("Lazy", 2);
                addNamedCandidate("DryUnderground", 2);
                addNamedCandidate("Beat2Pocket", 2);
                addNamedCandidate("Beat4Pocket", 2);
            }
            break;
        case BoomBapSubstyle::Dusty:
            candidates = density < 0.5f ? std::vector<int> { 7, 4, 5, 1 } : std::vector<int> { 7, 1, 2 };
            break;
        case BoomBapSubstyle::Jazzy:
            candidates = density > 0.45f ? std::vector<int> { 11, 2, 3, 5 } : std::vector<int> { 11, 4, 5, 7 };
            break;
        case BoomBapSubstyle::Aggressive:
            candidates = density > 0.55f ? std::vector<int> { 10, 9, 3 } : std::vector<int> { 10, 0, 2 };
            break;
        case BoomBapSubstyle::LaidBack:
            candidates = density < 0.5f ? std::vector<int> { 1, 7, 8 } : std::vector<int> { 1, 2, 5 };
            break;
        case BoomBapSubstyle::BoomBapGold:
            candidates = density > 0.55f ? std::vector<int> { 9, 0, 5, 3 } : std::vector<int> { 9, 0, 4 };
            break;
        case BoomBapSubstyle::RussianUnderground:
            candidates = density > 0.5f ? std::vector<int> { 6, 6, 3, 5 } : std::vector<int> { 6, 6, 0, 1, 7 };
            break;
        case BoomBapSubstyle::LofiRap:
            candidates = density > 0.5f ? std::vector<int> { 8, 8, 7, 1 } : std::vector<int> { 8, 8, 7, 6 };
            break;
        default:
            break;
    }

    if (candidates.empty())
    {
        candidates.reserve(all.size());
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
            candidates.push_back(i);
    }

    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
    return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
}

const HatPatternProfile& chooseHatPatternProfile(BoomBapSubstyle substyle, float density, PhraseRole role, std::mt19937& rng)
{
    const auto& all = getBoomBapHatPatternProfiles();
    std::vector<int> candidates;
    const auto mask = getSubstyleMask(substyle);

    const auto addNamedPattern = [&all, &candidates, density](const char* patternName, int weight, bool ignoreDensity)
    {
        if (weight <= 0)
            return;

        for (int i = 0; i < static_cast<int>(all.size()); ++i)
        {
            const auto& pattern = all[static_cast<size_t>(i)];
            if (pattern.name != patternName)
                continue;
            if (!ignoreDensity && (density < pattern.minDensity || density > pattern.maxDensity))
                continue;

            for (int copy = 0; copy < weight; ++copy)
                candidates.push_back(i);
            break;
        }
    };

    if (substyle == BoomBapSubstyle::Classic)
    {
        switch (role)
        {
            case PhraseRole::Base:
                addNamedPattern("1/8 Straight", 7, true);
                addNamedPattern("1/8 Swung", 5, true);
                addNamedPattern("Broken A", density >= 0.58f ? 1 : 0, false);
                break;
            case PhraseRole::Variation:
                addNamedPattern("1/8 Straight", 5, true);
                addNamedPattern("1/8 Swung", 5, true);
                addNamedPattern("Broken A", 1, false);
                addNamedPattern("Pocket Push", density >= 0.68f ? 1 : 0, false);
                break;
            case PhraseRole::Contrast:
                addNamedPattern("1/8 Swung", 6, true);
                addNamedPattern("1/8 Straight", 3, true);
                addNamedPattern("Broken A", 2, false);
                addNamedPattern("Pocket Push", 1, false);
                break;
            case PhraseRole::Ending:
                addNamedPattern("1/8 Straight", 4, true);
                addNamedPattern("1/8 Swung", 3, true);
                addNamedPattern("Broken A", 1, false);
                addNamedPattern("Bar Ending Phrase", 3, true);
                addNamedPattern("Pocket Push", density >= 0.60f ? 1 : 0, false);
                break;
            default:
                addNamedPattern("1/8 Straight", 6, true);
                addNamedPattern("1/8 Swung", 4, true);
                break;
        }

        if (density >= 0.78f && role != PhraseRole::Base)
            addNamedPattern("1/16 Soft", 1, false);

        if (!candidates.empty())
        {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
            return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
        }
    }

    if (substyle == BoomBapSubstyle::Dusty)
    {
        switch (role)
        {
            case PhraseRole::Base:
                addNamedPattern("1/8 Swung", 7, true);
                addNamedPattern("Laid Late Hat", 5, true);
                addNamedPattern("Sparse Dusty", density < 0.58f ? 4 : 2, false);
                addNamedPattern("Broken B", density >= 0.42f ? 2 : 0, false);
                break;
            case PhraseRole::Variation:
                addNamedPattern("Laid Late Hat", 6, true);
                addNamedPattern("Broken B", 4, false);
                addNamedPattern("1/8 Swung", 3, true);
                addNamedPattern("Sparse Dusty", 2, false);
                break;
            case PhraseRole::Contrast:
                addNamedPattern("Sparse Dusty", 6, true);
                addNamedPattern("Dusty Sparse Ending", 3, true);
                addNamedPattern("1/8 Swung", 2, true);
                break;
            case PhraseRole::Ending:
                addNamedPattern("Dusty Sparse Ending", 6, true);
                addNamedPattern("Laid Late Hat", 4, true);
                addNamedPattern("Broken B", density >= 0.45f ? 2 : 0, false);
                break;
            default:
                addNamedPattern("1/8 Swung", 6, true);
                addNamedPattern("Laid Late Hat", 4, true);
                break;
        }

        if (!candidates.empty())
        {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
            return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
        }
    }

    if (substyle == BoomBapSubstyle::Jazzy)
    {
        switch (role)
        {
            case PhraseRole::Base:
                addNamedPattern("Jazz Foot 2 And 4", 8, true);
                addNamedPattern("Jazzy Loose", 3, true);
                addNamedPattern("1/16 Soft", density >= 0.54f ? 2 : 1, false);
                break;
            case PhraseRole::Variation:
                addNamedPattern("Jazzy Loose", 6, true);
                addNamedPattern("Jazz Foot 2 And 4", 5, true);
                addNamedPattern("Broken B", 2, false);
                break;
            case PhraseRole::Contrast:
                addNamedPattern("Jazz Foot 2 And 4", 7, true);
                addNamedPattern("Jazzy Loose", 3, true);
                break;
            case PhraseRole::Ending:
                addNamedPattern("Jazzy Loose", 5, true);
                addNamedPattern("Jazz Foot 2 And 4", 4, true);
                addNamedPattern("Bar Ending Phrase", 2, true);
                break;
            default:
                addNamedPattern("Jazz Foot 2 And 4", 7, true);
                addNamedPattern("Jazzy Loose", 3, true);
                break;
        }

        if (!candidates.empty())
        {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
            return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
        }
    }

    if (substyle == BoomBapSubstyle::BoomBapGold)
    {
        switch (role)
        {
            case PhraseRole::Base:
                addNamedPattern("Gold Swung Eighth", 8, true);
                addNamedPattern("1/8 Straight", 3, true);
                addNamedPattern("Gold Dusty Broken", density >= 0.46f ? 2 : 1, false);
                break;
            case PhraseRole::Variation:
                addNamedPattern("Gold Swung Eighth", 6, true);
                addNamedPattern("Gold Dusty Broken", 4, false);
                addNamedPattern("Broken A", density >= 0.54f ? 2 : 1, false);
                break;
            case PhraseRole::Contrast:
                addNamedPattern("Gold Swung Eighth", 7, true);
                addNamedPattern("1/8 Swung", 3, true);
                addNamedPattern("Gold Dusty Broken", 1, false);
                break;
            case PhraseRole::Ending:
                addNamedPattern("Gold Swung Eighth", 5, true);
                addNamedPattern("Gold Dusty Broken", 3, false);
                addNamedPattern("Bar Ending Phrase", 2, true);
                break;
            default:
                addNamedPattern("Gold Swung Eighth", 7, true);
                addNamedPattern("1/8 Straight", 3, true);
                break;
        }

        if (!candidates.empty())
        {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
            return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
        }
    }

    if (substyle == BoomBapSubstyle::RussianUnderground)
    {
        switch (role)
        {
            case PhraseRole::Base:
                addNamedPattern("Russian Basement Eighth", 7, true);
                addNamedPattern("Russian Broken Sparse", density < 0.58f ? 4 : 2, false);
                addNamedPattern("Russian Courtyard Drag", 2, false);
                break;
            case PhraseRole::Variation:
                addNamedPattern("Russian Courtyard Drag", 5, true);
                addNamedPattern("Russian Basement Eighth", 4, true);
                addNamedPattern("Russian Broken Sparse", 3, true);
                break;
            case PhraseRole::Contrast:
                addNamedPattern("Russian Broken Sparse", 7, true);
                addNamedPattern("Russian Basement Eighth", 2, true);
                break;
            case PhraseRole::Ending:
                addNamedPattern("Russian Courtyard Drag", 5, true);
                addNamedPattern("Russian Basement Eighth", 4, true);
                addNamedPattern("Dusty Sparse Ending", 2, true);
                break;
            default:
                addNamedPattern("Russian Basement Eighth", 6, true);
                addNamedPattern("Russian Broken Sparse", 4, true);
                break;
        }

        if (!candidates.empty())
        {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
            return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
        }
    }

    if (substyle == BoomBapSubstyle::LofiRap)
    {
        switch (role)
        {
            case PhraseRole::Base:
                addNamedPattern("Lofi Dusty Eighth", 7, true);
                addNamedPattern("Lofi Tape Holes", density < 0.58f ? 5 : 3, true);
                addNamedPattern("Lofi Lazy Drag", 2, false);
                break;
            case PhraseRole::Variation:
                addNamedPattern("Lofi Tape Holes", 6, true);
                addNamedPattern("Lofi Dusty Eighth", 4, true);
                addNamedPattern("Lofi Lazy Drag", 3, true);
                break;
            case PhraseRole::Contrast:
                addNamedPattern("Lofi Tape Holes", 7, true);
                addNamedPattern("Lofi Lazy Drag", 3, true);
                break;
            case PhraseRole::Ending:
                addNamedPattern("Lofi Lazy Drag", 5, true);
                addNamedPattern("Lofi Dusty Eighth", 4, true);
                addNamedPattern("Dusty Sparse Ending", 2, true);
                break;
            default:
                addNamedPattern("Lofi Dusty Eighth", 6, true);
                addNamedPattern("Lofi Tape Holes", 4, true);
                break;
        }

        if (!candidates.empty())
        {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
            return all[static_cast<size_t>(candidates[static_cast<size_t>(pick(rng))])];
        }
    }

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
