#include "StyleDefaults.h"

#include <algorithm>

namespace bbg
{
namespace
{
LaneStyleDefaults lane(bool enabled,
                       float volume,
                       float densityBias,
                       float timingBias,
                       float humanizeBias,
                       std::array<juce::String, 3> families,
                       float noteProbability,
                       float phraseEndingProbability,
                       float mutationIntensity,
                       float rgVariationIntensity,
                       float hatFxIntensity,
                       float sub808Activity)
{
    LaneStyleDefaults out;
    out.enabledByDefault = enabled;
    out.volumeDefault = volume;
    out.densityBias = densityBias;
    out.timingBias = timingBias;
    out.humanizeBias = humanizeBias;
    out.preferredPatternFamilies = std::move(families);
    out.noteProbability = noteProbability;
    out.phraseEndingProbability = phraseEndingProbability;
    out.mutationIntensity = mutationIntensity;
    out.rgVariationIntensity = rgVariationIntensity;
    out.hatFxIntensity = hatFxIntensity;
    out.sub808Activity = sub808Activity;
    return out;
}

std::array<LaneStyleDefaults, 11> makeBaseBoomBapLanes()
{
    return {
        lane(true, 0.84f, 1.0f, 1.0f, 1.0f, { "carrier", "accented", "broken" }, 1.0f, 0.48f, 1.0f, 1.0f, 0.0f, 0.0f),
        lane(false, 0.56f, 0.0f, 1.0f, 0.9f, { "off", "off", "off" }, 0.0f, 0.18f, 0.6f, 0.6f, 0.05f, 0.0f),
        lane(true, 0.72f, 0.65f, 1.0f, 0.9f, { "ending", "transition", "lift" }, 0.55f, 0.68f, 0.8f, 0.9f, 0.0f, 0.0f),
        lane(true, 0.90f, 1.0f, 1.0f, 0.95f, { "backbeat", "pocket", "ghosted" }, 1.0f, 0.44f, 0.85f, 0.9f, 0.0f, 0.0f),
        lane(true, 0.78f, 0.7f, 1.0f, 1.0f, { "layer", "ghost", "support" }, 0.62f, 0.42f, 0.9f, 0.95f, 0.0f, 0.0f),
        lane(true, 0.92f, 1.0f, 1.0f, 0.9f, { "anchor", "support", "pickup" }, 1.0f, 0.56f, 0.86f, 0.88f, 0.0f, 0.0f),
        lane(true, 0.70f, 0.62f, 1.0f, 1.0f, { "pickup", "inner", "ghost" }, 0.58f, 0.36f, 0.9f, 0.92f, 0.0f, 0.0f),
        lane(false, 0.74f, 0.0f, 0.95f, 0.9f, { "off", "off", "off" }, 0.0f, 0.12f, 0.7f, 0.7f, 0.0f, 0.0f),
        lane(false, 0.66f, 0.5f, 1.0f, 0.95f, { "carrier_alt", "jazz", "texture" }, 0.36f, 0.46f, 0.75f, 0.8f, 0.0f, 0.0f),
        lane(false, 0.60f, 0.35f, 1.0f, 0.9f, { "marker", "ending", "section" }, 0.25f, 0.78f, 0.6f, 0.7f, 0.0f, 0.0f),
        lane(true, 0.66f, 0.6f, 1.0f, 1.0f, { "punctuation", "decor", "texture" }, 0.5f, 0.44f, 0.85f, 0.88f, 0.0f, 0.0f)
    };
}

std::array<LaneStyleDefaults, 11> makeBaseRapLanes()
{
    return {
        lane(true, 0.82f, 1.0f, 0.85f, 0.82f, { "straight", "pulse", "simple" }, 1.0f, 0.32f, 0.9f, 0.92f, 0.0f, 0.0f),
        lane(false, 0.58f, 0.22f, 0.9f, 0.84f, { "minimal", "accent", "burst" }, 0.12f, 0.24f, 0.7f, 0.78f, 0.12f, 0.0f),
        lane(true, 0.68f, 0.52f, 0.85f, 0.8f, { "transition", "accent", "light" }, 0.45f, 0.45f, 0.8f, 0.9f, 0.0f, 0.0f),
        lane(true, 0.92f, 1.0f, 0.82f, 0.8f, { "backbeat", "direct", "tight" }, 1.0f, 0.34f, 0.86f, 0.9f, 0.0f, 0.0f),
        lane(true, 0.76f, 0.6f, 0.9f, 0.84f, { "layer", "thick", "ghost_light" }, 0.56f, 0.28f, 0.88f, 0.92f, 0.0f, 0.0f),
        lane(true, 0.94f, 1.0f, 0.84f, 0.8f, { "backbone", "linear", "support" }, 1.0f, 0.42f, 0.88f, 0.9f, 0.0f, 0.0f),
        lane(true, 0.66f, 0.45f, 0.92f, 0.85f, { "support", "ghost", "pickup" }, 0.38f, 0.22f, 0.9f, 0.94f, 0.0f, 0.0f),
        lane(false, 0.72f, 0.35f, 0.86f, 0.82f, { "sub", "response", "anchor" }, 0.22f, 0.22f, 0.86f, 0.9f, 0.0f, 0.2f),
        lane(false, 0.62f, 0.3f, 0.9f, 0.85f, { "jazz", "alt", "ride" }, 0.2f, 0.24f, 0.65f, 0.72f, 0.0f, 0.0f),
        lane(false, 0.58f, 0.2f, 0.95f, 0.85f, { "marker", "hit", "ending" }, 0.18f, 0.62f, 0.58f, 0.65f, 0.0f, 0.0f),
        lane(true, 0.64f, 0.48f, 0.9f, 0.84f, { "texture", "small", "support" }, 0.4f, 0.3f, 0.82f, 0.9f, 0.0f, 0.0f)
    };
}

std::array<LaneStyleDefaults, 11> makeBaseTrapLanes()
{
    return {
        lane(true, 0.84f, 1.0f, 0.82f, 0.8f, { "base16", "base8", "straight" }, 1.0f, 0.28f, 0.9f, 0.92f, 0.0f, 0.0f),
        lane(true, 0.72f, 1.0f, 0.86f, 0.82f, { "roll", "triplet", "ratchet" }, 0.78f, 0.44f, 0.94f, 1.0f, 0.72f, 0.0f),
        lane(true, 0.70f, 0.62f, 0.86f, 0.8f, { "edge", "accent", "transition" }, 0.45f, 0.62f, 0.82f, 0.9f, 0.0f, 0.0f),
        lane(true, 0.92f, 0.92f, 0.82f, 0.78f, { "backbeat", "stable", "clear" }, 1.0f, 0.30f, 0.84f, 0.88f, 0.0f, 0.0f),
        lane(true, 0.78f, 0.52f, 0.9f, 0.82f, { "layer", "thick", "lightghost" }, 0.44f, 0.20f, 0.82f, 0.86f, 0.0f, 0.0f),
        lane(true, 0.94f, 1.0f, 0.84f, 0.8f, { "direct", "support", "half_time" }, 1.0f, 0.40f, 0.9f, 0.92f, 0.0f, 0.0f),
        lane(true, 0.66f, 0.40f, 0.92f, 0.84f, { "support", "light", "pickup" }, 0.24f, 0.18f, 0.84f, 0.9f, 0.0f, 0.0f),
        lane(true, 0.95f, 1.0f, 0.86f, 0.82f, { "anchor", "answer", "long_tail" }, 0.88f, 0.36f, 0.92f, 0.96f, 0.0f, 0.88f),
        lane(false, 0.56f, 0.2f, 0.9f, 0.84f, { "off", "off", "off" }, 0.08f, 0.12f, 0.6f, 0.66f, 0.0f, 0.0f),
        lane(false, 0.60f, 0.22f, 0.92f, 0.84f, { "marker", "impact", "ending" }, 0.2f, 0.66f, 0.62f, 0.7f, 0.0f, 0.0f),
        lane(true, 0.62f, 0.42f, 0.9f, 0.84f, { "texture", "small", "support" }, 0.28f, 0.26f, 0.78f, 0.86f, 0.0f, 0.0f)
    };
}

std::array<LaneStyleDefaults, 11> makeBaseDrillLanes()
{
    return {
        lane(true, 0.84f, 1.0f, 0.82f, 0.8f, { "skippy", "triplet_feel", "angular" }, 1.0f, 0.26f, 0.9f, 0.92f, 0.0f, 0.0f),
        lane(true, 0.70f, 0.92f, 0.86f, 0.82f, { "structural", "triplet", "burst" }, 0.66f, 0.42f, 0.92f, 0.98f, 0.62f, 0.0f),
        lane(true, 0.68f, 0.50f, 0.86f, 0.8f, { "edge", "accent", "section" }, 0.38f, 0.70f, 0.8f, 0.88f, 0.0f, 0.0f),
        lane(true, 0.94f, 0.92f, 0.82f, 0.78f, { "hard", "cold", "readable" }, 1.0f, 0.28f, 0.84f, 0.88f, 0.0f, 0.0f),
        lane(true, 0.74f, 0.42f, 0.9f, 0.82f, { "support", "tight", "limited" }, 0.30f, 0.18f, 0.78f, 0.84f, 0.0f, 0.0f),
        lane(true, 0.94f, 0.96f, 0.84f, 0.8f, { "broken", "angular", "tension" }, 1.0f, 0.36f, 0.9f, 0.92f, 0.0f, 0.0f),
        lane(true, 0.62f, 0.34f, 0.92f, 0.84f, { "rare", "support", "shadow" }, 0.18f, 0.14f, 0.8f, 0.88f, 0.0f, 0.0f),
        lane(true, 0.96f, 1.0f, 0.86f, 0.82f, { "anchor", "counter", "phrase" }, 0.9f, 0.34f, 0.94f, 0.98f, 0.0f, 0.92f),
        lane(false, 0.54f, 0.18f, 0.9f, 0.84f, { "off", "off", "off" }, 0.06f, 0.12f, 0.58f, 0.62f, 0.0f, 0.0f),
        lane(false, 0.58f, 0.2f, 0.92f, 0.84f, { "dark", "impact", "ending" }, 0.22f, 0.72f, 0.62f, 0.7f, 0.0f, 0.0f),
        lane(true, 0.60f, 0.36f, 0.9f, 0.84f, { "limited", "support", "detail" }, 0.22f, 0.24f, 0.74f, 0.82f, 0.0f, 0.0f)
    };
}

const std::array<GenreStyleDefaults, 8>& boomBapStyles()
{
    static const std::array<GenreStyleDefaults, 8> table = []
    {
        std::array<GenreStyleDefaults, 8> styles;

        { auto lanes = makeBaseBoomBapLanes(); styles[0] = { GenreType::BoomBap, "Classic", 57.0f, 0.52f, 0.42f, 0.34f, 0.50f, lanes }; }
        { auto lanes = makeBaseBoomBapLanes(); lanes[0].densityBias = 0.86f; lanes[5].densityBias = 0.82f; lanes[10].enabledByDefault = false; lanes[10].noteProbability = 0.32f; styles[1] = { GenreType::BoomBap, "Dusty", 58.0f, 0.50f, 0.38f, 0.30f, 0.44f, lanes }; }
        { auto lanes = makeBaseBoomBapLanes(); lanes[0].densityBias = 1.06f; lanes[8].enabledByDefault = true; lanes[8].noteProbability = 0.58f; lanes[8].volumeDefault = 0.70f; lanes[10].noteProbability = 0.58f; styles[2] = { GenreType::BoomBap, "Jazzy", 62.0f, 0.48f, 0.46f, 0.38f, 0.56f, lanes }; }
        { auto lanes = makeBaseBoomBapLanes(); lanes[5].densityBias = 1.2f; lanes[0].densityBias = 1.08f; lanes[4].noteProbability = 0.48f; styles[3] = { GenreType::BoomBap, "Aggressive", 55.0f, 0.58f, 0.34f, 0.24f, 0.62f, lanes }; }
        { auto lanes = makeBaseBoomBapLanes(); lanes[5].densityBias = 0.84f; lanes[0].densityBias = 0.88f; lanes[2].noteProbability = 0.48f; lanes[9].enabledByDefault = false; styles[4] = { GenreType::BoomBap, "LaidBack", 60.0f, 0.50f, 0.48f, 0.40f, 0.46f, lanes }; }
        { auto lanes = makeBaseBoomBapLanes(); lanes[2].noteProbability = 0.62f; lanes[8].enabledByDefault = true; lanes[8].noteProbability = 0.52f; lanes[10].noteProbability = 0.64f; styles[5] = { GenreType::BoomBap, "BoomBapGold", 59.0f, 0.56f, 0.44f, 0.36f, 0.57f, lanes }; }
        { auto lanes = makeBaseBoomBapLanes(); lanes[0].densityBias = 0.74f; lanes[0].noteProbability = 0.78f; lanes[5].densityBias = 0.78f; lanes[2].noteProbability = 0.28f; lanes[8].enabledByDefault = false; lanes[10].noteProbability = 0.26f; lanes[3].volumeDefault = 0.94f; styles[6] = { GenreType::BoomBap, "RussianUnderground", 56.0f, 0.54f, 0.32f, 0.26f, 0.38f, lanes }; }
        {
            auto lanes = makeBaseBoomBapLanes();
            lanes[0].densityBias = 0.82f;
            lanes[0].timingBias = 1.06f;
            lanes[0].humanizeBias = 1.08f;
            lanes[0].noteProbability = 0.92f;
            lanes[1].enabledByDefault = false;
            lanes[1].noteProbability = 0.04f;
            lanes[2].noteProbability = 0.12f;
            lanes[2].phraseEndingProbability = 0.42f;
            lanes[3].volumeDefault = 0.86f;
            lanes[3].timingBias = 1.08f;
            lanes[4].noteProbability = 0.10f;
            lanes[4].volumeDefault = 0.64f;
            lanes[5].densityBias = 0.74f;
            lanes[5].timingBias = 1.06f;
            lanes[6].noteProbability = 0.18f;
            lanes[6].volumeDefault = 0.62f;
            lanes[8].enabledByDefault = false;
            lanes[9].enabledByDefault = false;
            lanes[10].noteProbability = 0.18f;
            lanes[10].volumeDefault = 0.58f;
            styles[7] = { GenreType::BoomBap, "LofiRap", 56.0f, 0.38f, 0.36f, 0.44f, 0.34f, lanes };
        }
        return styles;
    }();
    return table;
}

const std::array<GenreStyleDefaults, 7>& rapStyles()
{
    static const std::array<GenreStyleDefaults, 7> table = []
    {
        std::array<GenreStyleDefaults, 7> styles;
        {
            auto lanes = makeBaseRapLanes();
            lanes[1].enabledByDefault = false; lanes[1].noteProbability = 0.02f;
            lanes[2].noteProbability = 0.08f;
            lanes[4].noteProbability = 0.16f;
            lanes[6].noteProbability = 0.18f;
            lanes[7].enabledByDefault = false; lanes[7].noteProbability = 0.10f;
            lanes[8].enabledByDefault = false;
            lanes[9].enabledByDefault = false;
            lanes[10].noteProbability = 0.10f;
            styles[0] = { GenreType::Rap, "EastCoast", 53.0f, 0.54f, 0.32f, 0.26f, 0.48f, lanes };
        }
        {
            auto lanes = makeBaseRapLanes();
            lanes[1].enabledByDefault = false; lanes[1].noteProbability = 0.03f;
            lanes[2].noteProbability = 0.16f;
            lanes[4].noteProbability = 0.18f;
            lanes[6].noteProbability = 0.14f;
            lanes[7].enabledByDefault = true; lanes[7].noteProbability = 0.16f;
            lanes[8].enabledByDefault = false;
            lanes[9].enabledByDefault = false;
            lanes[10].noteProbability = 0.14f;
            styles[1] = { GenreType::Rap, "WestCoast", 54.0f, 0.50f, 0.36f, 0.32f, 0.46f, lanes };
        }
        {
            auto lanes = makeBaseRapLanes();
            lanes[1].enabledByDefault = true; lanes[1].noteProbability = 0.14f;
            lanes[2].noteProbability = 0.22f;
            lanes[4].noteProbability = 0.28f;
            lanes[6].noteProbability = 0.24f;
            lanes[7].enabledByDefault = true; lanes[7].noteProbability = 0.30f;
            lanes[8].enabledByDefault = false;
            lanes[9].enabledByDefault = true; lanes[9].noteProbability = 0.08f;
            lanes[10].noteProbability = 0.18f;
            styles[2] = { GenreType::Rap, "DirtySouthClassic", 52.0f, 0.54f, 0.30f, 0.26f, 0.58f, lanes };
        }
        {
            auto lanes = makeBaseRapLanes();
            lanes[1].enabledByDefault = false; lanes[1].noteProbability = 0.01f;
            lanes[2].noteProbability = 0.08f;
            lanes[4].noteProbability = 0.12f;
            lanes[6].noteProbability = 0.16f;
            lanes[7].enabledByDefault = true; lanes[7].noteProbability = 0.18f;
            lanes[8].enabledByDefault = false;
            lanes[9].enabledByDefault = false;
            lanes[10].enabledByDefault = false; lanes[10].noteProbability = 0.04f;
            styles[3] = { GenreType::Rap, "GermanStreetRap", 51.0f, 0.52f, 0.24f, 0.20f, 0.44f, lanes };
        }
        {
            auto lanes = makeBaseRapLanes();
            lanes[1].enabledByDefault = false; lanes[1].noteProbability = 0.01f;
            lanes[2].noteProbability = 0.10f;
            lanes[4].noteProbability = 0.18f;
            lanes[6].noteProbability = 0.12f;
            lanes[7].enabledByDefault = false; lanes[7].noteProbability = 0.16f;
            lanes[8].enabledByDefault = false;
            lanes[9].enabledByDefault = false;
            lanes[10].noteProbability = 0.10f;
            styles[4] = { GenreType::Rap, "RussianRap", 51.0f, 0.50f, 0.26f, 0.24f, 0.40f, lanes };
        }
        {
            auto lanes = makeBaseRapLanes();
            lanes[1].enabledByDefault = true; lanes[1].noteProbability = 0.12f;
            lanes[2].noteProbability = 0.22f;
            lanes[4].noteProbability = 0.28f;
            lanes[6].noteProbability = 0.12f;
            lanes[7].enabledByDefault = true; lanes[7].noteProbability = 0.20f;
            lanes[8].enabledByDefault = false; lanes[8].noteProbability = 0.06f;
            lanes[9].enabledByDefault = true; lanes[9].noteProbability = 0.10f;
            lanes[10].noteProbability = 0.22f;
            styles[5] = { GenreType::Rap, "RnBRap", 54.0f, 0.46f, 0.38f, 0.36f, 0.42f, lanes };
        }
        {
            auto lanes = makeBaseRapLanes();
            lanes[0].densityBias = 1.06f;
            lanes[0].noteProbability = 0.94f;
            lanes[3].volumeDefault = 0.97f;
            lanes[3].noteProbability = 1.0f;
            lanes[5].densityBias = 1.24f;
            lanes[5].noteProbability = 1.0f;
            lanes[6].enabledByDefault = true; lanes[6].noteProbability = 0.16f;
            lanes[2].noteProbability = 0.06f;
            lanes[4].noteProbability = 0.14f;
            lanes[8].enabledByDefault = false; lanes[8].noteProbability = 0.02f;
            lanes[9].enabledByDefault = false; lanes[9].noteProbability = 0.02f;
            lanes[10].enabledByDefault = false; lanes[10].noteProbability = 0.06f;
            lanes[7].enabledByDefault = false; lanes[7].noteProbability = 0.10f; lanes[7].sub808Activity = 0.18f;
            styles[6] = { GenreType::Rap, "HardcoreRap", 52.0f, 0.60f, 0.30f, 0.24f, 0.62f, lanes };
        }
        return styles;
    }();
    return table;
}

const std::array<GenreStyleDefaults, 6>& trapStyles()
{
    static const std::array<GenreStyleDefaults, 6> table = []
    {
        std::array<GenreStyleDefaults, 6> styles;
        { auto lanes = makeBaseTrapLanes(); lanes[1].hatFxIntensity = 0.76f; lanes[1].noteProbability = 0.82f; lanes[7].sub808Activity = 0.88f; styles[0] = { GenreType::Trap, "ATLClassic", 52.0f, 0.58f, 0.30f, 0.28f, 0.58f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[1].noteProbability = 0.62f; lanes[1].hatFxIntensity = 0.58f; lanes[7].sub808Activity = 0.94f; lanes[10].noteProbability = 0.16f; styles[1] = { GenreType::Trap, "DarkTrap", 51.0f, 0.60f, 0.24f, 0.22f, 0.40f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[0].volumeDefault = 0.78f; lanes[2].noteProbability = 0.58f; lanes[7].sub808Activity = 0.74f; styles[2] = { GenreType::Trap, "CloudTrap", 53.0f, 0.48f, 0.36f, 0.34f, 0.48f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[1].noteProbability = 0.98f; lanes[1].hatFxIntensity = 1.0f; lanes[0].densityBias = 1.2f; lanes[7].sub808Activity = 0.92f; styles[3] = { GenreType::Trap, "RageTrap", 52.5f, 0.64f, 0.32f, 0.30f, 0.74f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[1].noteProbability = 0.38f; lanes[1].hatFxIntensity = 0.36f; lanes[5].densityBias = 0.88f; lanes[7].sub808Activity = 0.82f; lanes[10].noteProbability = 0.22f; styles[4] = { GenreType::Trap, "MemphisTrap", 51.5f, 0.56f, 0.30f, 0.28f, 0.44f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[1].noteProbability = 0.74f; lanes[1].hatFxIntensity = 0.7f; lanes[2].noteProbability = 0.52f; lanes[10].noteProbability = 0.22f; styles[5] = { GenreType::Trap, "LuxuryTrap", 52.0f, 0.54f, 0.30f, 0.30f, 0.52f, lanes }; }
        return styles;
    }();
    return table;
}

const std::array<GenreStyleDefaults, 4>& drillStyles()
{
    static const std::array<GenreStyleDefaults, 4> table = []
    {
        std::array<GenreStyleDefaults, 4> styles;
        { auto lanes = makeBaseDrillLanes(); lanes[1].hatFxIntensity = 0.84f; lanes[1].noteProbability = 0.74f; lanes[7].sub808Activity = 0.9f; lanes[5].densityBias = 1.08f; styles[0] = { GenreType::Drill, "UKDrill", 51.0f, 0.60f, 0.26f, 0.22f, 0.58f, lanes }; }
        { auto lanes = makeBaseDrillLanes(); lanes[1].hatFxIntensity = 0.9f; lanes[1].noteProbability = 0.78f; lanes[3].volumeDefault = 0.98f; lanes[5].densityBias = 1.12f; lanes[7].sub808Activity = 0.92f; styles[1] = { GenreType::Drill, "BrooklynDrill", 51.0f, 0.64f, 0.26f, 0.22f, 0.62f, lanes }; }
        { auto lanes = makeBaseDrillLanes(); lanes[1].hatFxIntensity = 0.68f; lanes[1].noteProbability = 0.62f; lanes[5].densityBias = 0.92f; lanes[7].sub808Activity = 0.84f; styles[2] = { GenreType::Drill, "NYDrill", 50.8f, 0.58f, 0.24f, 0.22f, 0.50f, lanes }; }
        { auto lanes = makeBaseDrillLanes(); lanes[1].hatFxIntensity = 0.82f; lanes[1].noteProbability = 0.68f; lanes[5].densityBias = 0.84f; lanes[7].sub808Activity = 0.9f; lanes[10].noteProbability = 0.12f; styles[3] = { GenreType::Drill, "DarkDrill", 50.5f, 0.56f, 0.22f, 0.20f, 0.36f, lanes }; }
        return styles;
    }();
    return table;
}
} // namespace

int trackTypeToLaneIndex(TrackType trackType)
{
    switch (trackType)
    {
        case TrackType::HiHat: return 0;
        case TrackType::HatFX: return 1;
        case TrackType::OpenHat: return 2;
        case TrackType::Snare: return 3;
        case TrackType::ClapGhostSnare: return 4;
        case TrackType::Kick: return 5;
        case TrackType::GhostKick: return 6;
        case TrackType::Sub808: return 7;
        case TrackType::Ride: return 8;
        case TrackType::Cymbal: return 9;
        case TrackType::Perc: return 10;
        default: return 0;
    }
}

const GenreStyleDefaults& getGenreStyleDefaults(GenreType genre, int substyleIndex)
{
    if (genre == GenreType::Rap)
    {
        const auto& styles = rapStyles();
        const int idx = std::clamp(substyleIndex, 0, static_cast<int>(styles.size()) - 1);
        return styles[static_cast<size_t>(idx)];
    }

    if (genre == GenreType::Trap)
    {
        const auto& styles = trapStyles();
        const int idx = std::clamp(substyleIndex, 0, static_cast<int>(styles.size()) - 1);
        return styles[static_cast<size_t>(idx)];
    }

    if (genre == GenreType::Drill)
    {
        const auto& styles = drillStyles();
        const int idx = std::clamp(substyleIndex, 0, static_cast<int>(styles.size()) - 1);
        return styles[static_cast<size_t>(idx)];
    }

    const auto& styles = boomBapStyles();
    const int idx = std::clamp(substyleIndex, 0, static_cast<int>(styles.size()) - 1);
    return styles[static_cast<size_t>(idx)];
}

const LaneStyleDefaults& getLaneStyleDefaults(const GenreStyleDefaults& style, TrackType trackType)
{
    return style.laneDefaults[static_cast<size_t>(trackTypeToLaneIndex(trackType))];
}

juce::StringArray getBoomBapSubstyleNames()
{
    const auto& styles = boomBapStyles();
    juce::StringArray names;
    for (const auto& s : styles)
        names.add(s.substyleName);
    return names;
}

juce::StringArray getRapSubstyleNames()
{
    const auto& styles = rapStyles();
    juce::StringArray names;
    for (const auto& s : styles)
        names.add(s.substyleName);
    return names;
}

juce::StringArray getTrapSubstyleNames()
{
    const auto& styles = trapStyles();
    juce::StringArray names;
    for (const auto& s : styles)
        names.add(s.substyleName);
    return names;
}

juce::StringArray getDrillSubstyleNames()
{
    const auto& styles = drillStyles();
    juce::StringArray names;
    for (const auto& s : styles)
        names.add(s.substyleName);
    return names;
}
} // namespace bbg
