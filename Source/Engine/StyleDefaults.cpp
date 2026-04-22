#include "StyleDefaults.h"

#include <algorithm>
#include <cstdint>

namespace bbg
{
namespace
{
uint32_t mixHash(uint32_t hash, uint32_t value)
{
    hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    return hash;
}

uint32_t hashStyleBpmSeed(const GeneratorParams& params, int substyleIndex, const juce::String& substyleName)
{
    uint32_t hash = 2166136261u;
    hash = mixHash(hash, static_cast<uint32_t>(juce::jlimit(1, 999999, params.seed)));
    hash = mixHash(hash, static_cast<uint32_t>(params.genre));
    hash = mixHash(hash, static_cast<uint32_t>(juce::jmax(0, substyleIndex)));

    const char* utf8 = substyleName.toRawUTF8();
    while (*utf8 != '\0')
    {
        hash = mixHash(hash, static_cast<uint32_t>(static_cast<unsigned char>(*utf8)));
        ++utf8;
    }

    return hash;
}

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
        lane(true, 0.84f, 1.0f, 0.82f, 0.76f, { "carrier", "tresillo", "motif" }, 1.0f, 0.42f, 0.86f, 0.92f, 0.0f, 0.0f),
        lane(true, 0.68f, 0.64f, 0.84f, 0.78f, { "accent", "edge", "transition" }, 0.28f, 0.58f, 0.74f, 0.82f, 0.56f, 0.0f),
        lane(true, 0.70f, 0.44f, 0.84f, 0.78f, { "lift", "phrase_edge", "support" }, 0.32f, 0.62f, 0.76f, 0.84f, 0.0f, 0.0f),
        lane(true, 0.92f, 1.0f, 0.84f, 0.76f, { "backbeat", "half_time", "anchor" }, 1.0f, 0.34f, 0.82f, 0.88f, 0.0f, 0.0f),
        lane(true, 0.78f, 0.46f, 0.90f, 0.82f, { "ghost", "drag", "support" }, 0.24f, 0.24f, 0.78f, 0.84f, 0.0f, 0.0f),
        lane(true, 0.94f, 1.0f, 0.82f, 0.76f, { "syncopated", "anchor", "pickup" }, 1.0f, 0.48f, 0.86f, 0.90f, 0.0f, 0.0f),
        lane(true, 0.66f, 0.36f, 0.92f, 0.84f, { "pickup", "response", "support" }, 0.18f, 0.20f, 0.76f, 0.82f, 0.0f, 0.0f),
        lane(true, 0.96f, 1.0f, 0.84f, 0.80f, { "anchor", "hold", "slide" }, 0.86f, 0.40f, 0.88f, 0.94f, 0.0f, 0.94f),
        lane(false, 0.54f, 0.12f, 0.92f, 0.82f, { "off", "off", "off" }, 0.06f, 0.10f, 0.54f, 0.60f, 0.0f, 0.0f),
        lane(false, 0.58f, 0.18f, 0.92f, 0.82f, { "marker", "impact", "ending" }, 0.16f, 0.62f, 0.58f, 0.64f, 0.0f, 0.0f),
        lane(true, 0.60f, 0.28f, 0.90f, 0.82f, { "texture", "small", "support" }, 0.14f, 0.24f, 0.68f, 0.76f, 0.0f, 0.0f)
    };
}

const std::array<GenreStyleDefaults, 6>& boomBapStyles()
{
    static const std::array<GenreStyleDefaults, 6> table = []
    {
        std::array<GenreStyleDefaults, 6> styles;

        {
            auto lanes = makeBaseBoomBapLanes();
            lanes[0].densityBias = 0.96f;
            lanes[0].noteProbability = 1.0f;
            lanes[2].densityBias = 0.36f;
            lanes[2].noteProbability = 0.28f;
            lanes[2].phraseEndingProbability = 0.34f;
            lanes[3].phraseEndingProbability = 0.18f;
            lanes[4].volumeDefault = 0.58f;
            lanes[4].densityBias = 0.24f;
            lanes[4].noteProbability = 0.14f;
            lanes[4].phraseEndingProbability = 0.16f;
            lanes[5].densityBias = 0.92f;
            lanes[6].volumeDefault = 0.56f;
            lanes[6].densityBias = 0.26f;
            lanes[6].noteProbability = 0.18f;
            lanes[6].phraseEndingProbability = 0.18f;
            lanes[8].enabledByDefault = false;
            lanes[8].noteProbability = 0.04f;
            lanes[10].densityBias = 0.24f;
            lanes[10].noteProbability = 0.18f;
            lanes[10].phraseEndingProbability = 0.18f;
            styles[0] = { GenreType::BoomBap, "Classic", 90.0f, 88, 92, 57.0f, 0.52f, 0.38f, 0.30f, 0.46f, lanes };
        }
        {
            auto lanes = makeBaseBoomBapLanes();
            lanes[0].volumeDefault = 0.80f;
            lanes[0].densityBias = 0.90f;
            lanes[0].timingBias = 1.14f;
            lanes[0].humanizeBias = 1.02f;
            lanes[0].noteProbability = 0.96f;
            lanes[2].noteProbability = 0.18f;
            lanes[2].phraseEndingProbability = 0.42f;
            lanes[3].volumeDefault = 0.86f;
            lanes[3].timingBias = 1.10f;
            lanes[4].volumeDefault = 0.56f;
            lanes[4].densityBias = 0.24f;
            lanes[4].noteProbability = 0.20f;
            lanes[4].phraseEndingProbability = 0.24f;
            lanes[5].densityBias = 0.78f;
            lanes[5].timingBias = 1.06f;
            lanes[6].volumeDefault = 0.54f;
            lanes[6].densityBias = 0.24f;
            lanes[6].noteProbability = 0.18f;
            lanes[8].enabledByDefault = true;
            lanes[8].volumeDefault = 0.48f;
            lanes[8].densityBias = 0.42f;
            lanes[8].timingBias = 1.08f;
            lanes[8].noteProbability = 0.34f;
            lanes[9].enabledByDefault = false;
            lanes[9].noteProbability = 0.04f;
            lanes[10].volumeDefault = 0.54f;
            lanes[10].densityBias = 0.28f;
            lanes[10].noteProbability = 0.24f;
            lanes[10].phraseEndingProbability = 0.30f;
            styles[1] = { GenreType::BoomBap, "Dusty", 86.0f, 82, 90, 59.0f, 0.46f, 0.52f, 0.40f, 0.38f, lanes };
        }
        {
            auto lanes = makeBaseBoomBapLanes();
            lanes[0].volumeDefault = 0.72f;
            lanes[0].densityBias = 0.44f;
            lanes[0].timingBias = 1.04f;
            lanes[0].noteProbability = 0.56f;
            lanes[2].noteProbability = 0.18f;
            lanes[2].phraseEndingProbability = 0.46f;
            lanes[3].volumeDefault = 0.78f;
            lanes[3].densityBias = 0.72f;
            lanes[3].timingBias = 0.96f;
            lanes[3].noteProbability = 0.62f;
            lanes[3].phraseEndingProbability = 0.58f;
            lanes[4].volumeDefault = 0.55f;
            lanes[4].densityBias = 0.62f;
            lanes[4].noteProbability = 0.46f;
            lanes[4].phraseEndingProbability = 0.62f;
            lanes[5].volumeDefault = 0.62f;
            lanes[5].densityBias = 0.56f;
            lanes[5].timingBias = 0.90f;
            lanes[5].noteProbability = 0.78f;
            lanes[6].volumeDefault = 0.48f;
            lanes[6].densityBias = 0.34f;
            lanes[6].noteProbability = 0.24f;
            lanes[8].enabledByDefault = true;
            lanes[8].volumeDefault = 0.78f;
            lanes[8].densityBias = 1.12f;
            lanes[8].timingBias = 1.08f;
            lanes[8].noteProbability = 1.0f;
            lanes[8].phraseEndingProbability = 0.72f;
            lanes[9].enabledByDefault = true;
            lanes[9].volumeDefault = 0.30f;
            lanes[9].densityBias = 0.30f;
            lanes[9].timingBias = 0.94f;
            lanes[9].noteProbability = 0.42f;
            lanes[9].phraseEndingProbability = 0.40f;
            lanes[10].volumeDefault = 0.60f;
            lanes[10].densityBias = 0.70f;
            lanes[10].timingBias = 1.06f;
            lanes[10].noteProbability = 0.52f;
            lanes[10].phraseEndingProbability = 0.66f;
            styles[2] = { GenreType::BoomBap, "Jazzy", 94.0f, 88, 102, 63.0f, 0.50f, 0.50f, 0.34f, 0.58f, lanes };
        }
        {
            auto lanes = makeBaseBoomBapLanes();
            lanes[0].volumeDefault = 0.78f;
            lanes[0].densityBias = 0.96f;
            lanes[0].timingBias = 1.04f;
            lanes[0].humanizeBias = 0.92f;
            lanes[0].noteProbability = 0.96f;
            lanes[2].volumeDefault = 0.50f;
            lanes[2].densityBias = 0.18f;
            lanes[2].noteProbability = 0.10f;
            lanes[2].phraseEndingProbability = 0.18f;
            lanes[3].volumeDefault = 0.94f;
            lanes[3].densityBias = 1.0f;
            lanes[3].timingBias = 1.0f;
            lanes[3].noteProbability = 1.0f;
            lanes[3].phraseEndingProbability = 0.34f;
            lanes[4].volumeDefault = 0.52f;
            lanes[4].densityBias = 0.22f;
            lanes[4].noteProbability = 0.18f;
            lanes[4].phraseEndingProbability = 0.20f;
            lanes[5].volumeDefault = 0.94f;
            lanes[5].densityBias = 0.98f;
            lanes[5].timingBias = 0.96f;
            lanes[5].noteProbability = 1.0f;
            lanes[5].phraseEndingProbability = 0.44f;
            lanes[6].volumeDefault = 0.48f;
            lanes[6].densityBias = 0.18f;
            lanes[6].noteProbability = 0.12f;
            lanes[6].phraseEndingProbability = 0.16f;
            lanes[8].enabledByDefault = false;
            lanes[8].noteProbability = 0.02f;
            lanes[9].enabledByDefault = false;
            lanes[9].noteProbability = 0.03f;
            lanes[10].volumeDefault = 0.50f;
            lanes[10].densityBias = 0.22f;
            lanes[10].noteProbability = 0.18f;
            lanes[10].phraseEndingProbability = 0.24f;
            styles[3] = { GenreType::BoomBap, "BoomBapGold", 92.0f, 89, 96, 58.5f, 0.58f, 0.38f, 0.28f, 0.52f, lanes };
        }
        {
            auto lanes = makeBaseBoomBapLanes();
            lanes[0].volumeDefault = 0.70f;
            lanes[0].densityBias = 0.70f;
            lanes[0].timingBias = 1.08f;
            lanes[0].humanizeBias = 0.98f;
            lanes[0].noteProbability = 0.82f;
            lanes[1].enabledByDefault = false;
            lanes[1].noteProbability = 0.0f;
            lanes[2].enabledByDefault = false;
            lanes[2].volumeDefault = 0.46f;
            lanes[2].densityBias = 0.08f;
            lanes[2].noteProbability = 0.05f;
            lanes[2].phraseEndingProbability = 0.12f;
            lanes[3].volumeDefault = 0.96f;
            lanes[3].densityBias = 1.0f;
            lanes[3].timingBias = 1.12f;
            lanes[3].humanizeBias = 0.86f;
            lanes[3].noteProbability = 1.0f;
            lanes[3].phraseEndingProbability = 0.22f;
            lanes[4].volumeDefault = 0.42f;
            lanes[4].densityBias = 0.08f;
            lanes[4].timingBias = 1.08f;
            lanes[4].noteProbability = 0.04f;
            lanes[4].phraseEndingProbability = 0.08f;
            lanes[5].volumeDefault = 0.96f;
            lanes[5].densityBias = 0.82f;
            lanes[5].timingBias = 1.04f;
            lanes[5].noteProbability = 1.0f;
            lanes[5].phraseEndingProbability = 0.30f;
            lanes[6].volumeDefault = 0.42f;
            lanes[6].densityBias = 0.10f;
            lanes[6].noteProbability = 0.06f;
            lanes[6].phraseEndingProbability = 0.08f;
            lanes[8].enabledByDefault = false;
            lanes[8].densityBias = 0.0f;
            lanes[8].noteProbability = 0.0f;
            lanes[9].enabledByDefault = false;
            lanes[9].densityBias = 0.0f;
            lanes[9].noteProbability = 0.0f;
            lanes[10].volumeDefault = 0.48f;
            lanes[10].densityBias = 0.14f;
            lanes[10].timingBias = 1.06f;
            lanes[10].noteProbability = 0.10f;
            lanes[10].phraseEndingProbability = 0.16f;
            styles[4] = { GenreType::BoomBap, "RussianUnderground", 82.0f, 78, 88, 56.5f, 0.42f, 0.34f, 0.28f, 0.32f, lanes };
        }
        {
            auto lanes = makeBaseBoomBapLanes();
            lanes[0].volumeDefault = 0.62f;
            lanes[0].densityBias = 0.62f;
            lanes[0].timingBias = 1.18f;
            lanes[0].humanizeBias = 1.18f;
            lanes[0].noteProbability = 0.76f;
            lanes[1].enabledByDefault = false;
            lanes[1].densityBias = 0.0f;
            lanes[1].noteProbability = 0.0f;
            lanes[2].noteProbability = 0.035f;
            lanes[2].phraseEndingProbability = 0.12f;
            lanes[3].volumeDefault = 0.78f;
            lanes[3].densityBias = 0.56f;
            lanes[3].timingBias = 1.16f;
            lanes[3].humanizeBias = 1.10f;
            lanes[4].noteProbability = 0.035f;
            lanes[4].volumeDefault = 0.42f;
            lanes[5].volumeDefault = 0.78f;
            lanes[5].densityBias = 0.62f;
            lanes[5].timingBias = 1.14f;
            lanes[5].noteProbability = 0.86f;
            lanes[6].noteProbability = 0.08f;
            lanes[6].phraseEndingProbability = 0.12f;
            lanes[6].volumeDefault = 0.46f;
            lanes[8].enabledByDefault = false;
            lanes[8].densityBias = 0.0f;
            lanes[8].noteProbability = 0.0f;
            lanes[9].enabledByDefault = false;
            lanes[9].densityBias = 0.0f;
            lanes[9].noteProbability = 0.0f;
            lanes[10].noteProbability = 0.08f;
            lanes[10].phraseEndingProbability = 0.16f;
            lanes[10].volumeDefault = 0.42f;
            styles[5] = { GenreType::BoomBap, "LofiRap", 78.0f, 74, 84, 57.0f, 0.36f, 0.40f, 0.50f, 0.30f, lanes };
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
            lanes[0].volumeDefault = 0.76f;
            lanes[0].densityBias = 0.84f;
            lanes[0].timingBias = 1.02f;
            lanes[0].humanizeBias = 0.92f;
            lanes[0].noteProbability = 0.92f;
            lanes[1].enabledByDefault = false;
            lanes[1].densityBias = 0.0f;
            lanes[1].noteProbability = 0.0f;
            lanes[2].enabledByDefault = false;
            lanes[2].volumeDefault = 0.52f;
            lanes[2].densityBias = 0.08f;
            lanes[2].noteProbability = 0.035f;
            lanes[2].phraseEndingProbability = 0.08f;
            lanes[3].volumeDefault = 0.98f;
            lanes[3].densityBias = 1.0f;
            lanes[3].timingBias = 1.06f;
            lanes[3].humanizeBias = 0.88f;
            lanes[3].noteProbability = 1.0f;
            lanes[3].phraseEndingProbability = 0.16f;
            lanes[4].volumeDefault = 0.54f;
            lanes[4].densityBias = 0.16f;
            lanes[4].noteProbability = 0.055f;
            lanes[4].phraseEndingProbability = 0.08f;
            lanes[5].volumeDefault = 0.98f;
            lanes[5].densityBias = 0.92f;
            lanes[5].timingBias = 0.98f;
            lanes[5].humanizeBias = 0.86f;
            lanes[5].noteProbability = 1.0f;
            lanes[6].volumeDefault = 0.46f;
            lanes[6].densityBias = 0.14f;
            lanes[6].noteProbability = 0.06f;
            lanes[6].phraseEndingProbability = 0.08f;
            lanes[7].enabledByDefault = false;
            lanes[7].densityBias = 0.0f;
            lanes[7].noteProbability = 0.0f;
            lanes[7].sub808Activity = 0.02f;
            lanes[8].enabledByDefault = false;
            lanes[8].densityBias = 0.0f;
            lanes[8].noteProbability = 0.0f;
            lanes[9].enabledByDefault = false;
            lanes[9].densityBias = 0.0f;
            lanes[9].noteProbability = 0.0f;
            lanes[10].volumeDefault = 0.50f;
            lanes[10].densityBias = 0.16f;
            lanes[10].noteProbability = 0.07f;
            lanes[10].phraseEndingProbability = 0.12f;
            styles[0] = { GenreType::Rap, "EastCoast", 92.0f, 86, 96, 54.0f, 0.46f, 0.30f, 0.24f, 0.54f, lanes };
        }
        {
            auto lanes = makeBaseRapLanes();
            lanes[0].volumeDefault = 0.78f;
            lanes[0].densityBias = 0.88f;
            lanes[0].timingBias = 1.10f;
            lanes[0].humanizeBias = 1.02f;
            lanes[0].noteProbability = 0.96f;
            lanes[1].enabledByDefault = false;
            lanes[1].densityBias = 0.0f;
            lanes[1].noteProbability = 0.0f;
            lanes[2].enabledByDefault = true;
            lanes[2].volumeDefault = 0.64f;
            lanes[2].densityBias = 0.36f;
            lanes[2].noteProbability = 0.18f;
            lanes[2].phraseEndingProbability = 0.34f;
            lanes[3].volumeDefault = 0.90f;
            lanes[3].densityBias = 1.0f;
            lanes[3].timingBias = 1.16f;
            lanes[3].humanizeBias = 1.04f;
            lanes[3].noteProbability = 1.0f;
            lanes[4].volumeDefault = 0.70f;
            lanes[4].densityBias = 0.40f;
            lanes[4].timingBias = 1.14f;
            lanes[4].noteProbability = 0.24f;
            lanes[4].phraseEndingProbability = 0.28f;
            lanes[5].volumeDefault = 0.94f;
            lanes[5].densityBias = 0.86f;
            lanes[5].timingBias = 1.10f;
            lanes[5].humanizeBias = 0.98f;
            lanes[5].noteProbability = 1.0f;
            lanes[6].volumeDefault = 0.50f;
            lanes[6].densityBias = 0.20f;
            lanes[6].noteProbability = 0.10f;
            lanes[6].phraseEndingProbability = 0.14f;
            lanes[7].enabledByDefault = true;
            lanes[7].volumeDefault = 0.78f;
            lanes[7].densityBias = 0.30f;
            lanes[7].noteProbability = 0.18f;
            lanes[7].sub808Activity = 0.28f;
            lanes[8].enabledByDefault = false;
            lanes[8].densityBias = 0.0f;
            lanes[8].noteProbability = 0.0f;
            lanes[9].enabledByDefault = false;
            lanes[9].densityBias = 0.0f;
            lanes[9].noteProbability = 0.0f;
            lanes[10].volumeDefault = 0.58f;
            lanes[10].densityBias = 0.32f;
            lanes[10].noteProbability = 0.18f;
            lanes[10].phraseEndingProbability = 0.24f;
            styles[1] = { GenreType::Rap, "WestCoast", 94.0f, 88, 100, 55.5f, 0.46f, 0.38f, 0.34f, 0.44f, lanes };
        }
        {
            auto lanes = makeBaseRapLanes();
            lanes[0].volumeDefault = 0.78f;
            lanes[0].densityBias = 0.96f;
            lanes[0].timingBias = 1.06f;
            lanes[0].humanizeBias = 0.96f;
            lanes[0].noteProbability = 0.98f;
            lanes[1].enabledByDefault = true;
            lanes[1].volumeDefault = 0.50f;
            lanes[1].densityBias = 0.20f;
            lanes[1].noteProbability = 0.10f;
            lanes[1].hatFxIntensity = 0.16f;
            lanes[2].enabledByDefault = true;
            lanes[2].volumeDefault = 0.66f;
            lanes[2].densityBias = 0.44f;
            lanes[2].noteProbability = 0.28f;
            lanes[2].phraseEndingProbability = 0.44f;
            lanes[3].volumeDefault = 0.92f;
            lanes[3].densityBias = 1.0f;
            lanes[3].timingBias = 1.08f;
            lanes[3].humanizeBias = 0.94f;
            lanes[3].noteProbability = 1.0f;
            lanes[4].volumeDefault = 0.76f;
            lanes[4].densityBias = 0.52f;
            lanes[4].timingBias = 1.08f;
            lanes[4].noteProbability = 0.38f;
            lanes[4].phraseEndingProbability = 0.34f;
            lanes[5].volumeDefault = 0.96f;
            lanes[5].densityBias = 1.04f;
            lanes[5].timingBias = 1.04f;
            lanes[5].noteProbability = 1.0f;
            lanes[6].volumeDefault = 0.52f;
            lanes[6].densityBias = 0.28f;
            lanes[6].noteProbability = 0.18f;
            lanes[6].phraseEndingProbability = 0.22f;
            lanes[7].enabledByDefault = false;
            lanes[7].volumeDefault = 0.0f;
            lanes[7].densityBias = 0.0f;
            lanes[7].noteProbability = 0.0f;
            lanes[7].sub808Activity = 0.0f;
            lanes[8].enabledByDefault = false;
            lanes[8].densityBias = 0.0f;
            lanes[8].noteProbability = 0.0f;
            lanes[9].enabledByDefault = true;
            lanes[9].volumeDefault = 0.50f;
            lanes[9].densityBias = 0.18f;
            lanes[9].noteProbability = 0.10f;
            lanes[9].phraseEndingProbability = 0.36f;
            lanes[10].enabledByDefault = true;
            lanes[10].volumeDefault = 0.58f;
            lanes[10].densityBias = 0.34f;
            lanes[10].noteProbability = 0.24f;
            lanes[10].phraseEndingProbability = 0.30f;
            styles[2] = { GenreType::Rap, "DirtySouthClassic", 80.0f, 74, 88, 54.5f, 0.56f, 0.36f, 0.30f, 0.56f, lanes };
        }
        {
            auto lanes = makeBaseRapLanes();
            lanes[0].volumeDefault = 0.74f;
            lanes[0].densityBias = 0.88f;
            lanes[0].timingBias = 0.92f;
            lanes[0].humanizeBias = 0.78f;
            lanes[0].noteProbability = 0.92f;
            lanes[1].enabledByDefault = false;
            lanes[1].densityBias = 0.0f;
            lanes[1].noteProbability = 0.0f;
            lanes[1].hatFxIntensity = 0.0f;
            lanes[2].enabledByDefault = false;
            lanes[2].volumeDefault = 0.50f;
            lanes[2].densityBias = 0.08f;
            lanes[2].noteProbability = 0.025f;
            lanes[2].phraseEndingProbability = 0.18f;
            lanes[3].volumeDefault = 0.98f;
            lanes[3].densityBias = 1.0f;
            lanes[3].timingBias = 0.90f;
            lanes[3].humanizeBias = 0.74f;
            lanes[3].noteProbability = 1.0f;
            lanes[3].phraseEndingProbability = 0.12f;
            lanes[4].enabledByDefault = true;
            lanes[4].volumeDefault = 0.56f;
            lanes[4].densityBias = 0.16f;
            lanes[4].timingBias = 0.90f;
            lanes[4].humanizeBias = 0.72f;
            lanes[4].noteProbability = 0.08f;
            lanes[4].phraseEndingProbability = 0.16f;
            lanes[5].volumeDefault = 0.99f;
            lanes[5].densityBias = 0.94f;
            lanes[5].timingBias = 0.88f;
            lanes[5].humanizeBias = 0.76f;
            lanes[5].noteProbability = 1.0f;
            lanes[5].phraseEndingProbability = 0.24f;
            lanes[6].volumeDefault = 0.48f;
            lanes[6].densityBias = 0.10f;
            lanes[6].noteProbability = 0.055f;
            lanes[6].phraseEndingProbability = 0.10f;
            lanes[7].enabledByDefault = false;
            lanes[7].volumeDefault = 0.0f;
            lanes[7].densityBias = 0.0f;
            lanes[7].noteProbability = 0.0f;
            lanes[7].sub808Activity = 0.0f;
            lanes[8].enabledByDefault = false;
            lanes[8].densityBias = 0.0f;
            lanes[8].noteProbability = 0.0f;
            lanes[9].enabledByDefault = false;
            lanes[9].densityBias = 0.0f;
            lanes[9].noteProbability = 0.0f;
            lanes[10].enabledByDefault = false;
            lanes[10].volumeDefault = 0.44f;
            lanes[10].densityBias = 0.08f;
            lanes[10].noteProbability = 0.025f;
            lanes[10].phraseEndingProbability = 0.12f;
            styles[3] = { GenreType::Rap, "GermanStreetRap", 88.0f, 82, 94, 51.5f, 0.48f, 0.22f, 0.16f, 0.54f, lanes };
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
            styles[4] = { GenreType::Rap, "RussianRap", 90.0f, 86, 94, 51.0f, 0.50f, 0.26f, 0.24f, 0.40f, lanes };
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
            styles[5] = { GenreType::Rap, "RnBRap", 97.0f, 92, 102, 54.0f, 0.46f, 0.38f, 0.36f, 0.42f, lanes };
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
            styles[6] = { GenreType::Rap, "HardcoreRap", 90.0f, 86, 94, 52.0f, 0.60f, 0.30f, 0.24f, 0.62f, lanes };
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
        { auto lanes = makeBaseTrapLanes(); lanes[1].hatFxIntensity = 0.76f; lanes[1].noteProbability = 0.82f; lanes[7].sub808Activity = 0.88f; styles[0] = { GenreType::Trap, "ATLClassic", 141.0f, 138, 144, 52.0f, 0.58f, 0.30f, 0.28f, 0.58f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[1].noteProbability = 0.62f; lanes[1].hatFxIntensity = 0.58f; lanes[7].sub808Activity = 0.94f; lanes[10].noteProbability = 0.16f; styles[1] = { GenreType::Trap, "DarkTrap", 139.0f, 136, 142, 51.0f, 0.60f, 0.24f, 0.22f, 0.40f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[0].volumeDefault = 0.78f; lanes[2].noteProbability = 0.58f; lanes[7].sub808Activity = 0.74f; styles[2] = { GenreType::Trap, "CloudTrap", 134.0f, 130, 138, 53.0f, 0.48f, 0.36f, 0.34f, 0.48f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[1].noteProbability = 0.98f; lanes[1].hatFxIntensity = 1.0f; lanes[0].densityBias = 1.2f; lanes[7].sub808Activity = 0.92f; styles[3] = { GenreType::Trap, "RageTrap", 150.0f, 146, 154, 52.5f, 0.64f, 0.32f, 0.30f, 0.74f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[1].noteProbability = 0.38f; lanes[1].hatFxIntensity = 0.36f; lanes[5].densityBias = 0.88f; lanes[7].sub808Activity = 0.82f; lanes[10].noteProbability = 0.22f; styles[4] = { GenreType::Trap, "MemphisTrap", 141.0f, 138, 144, 51.5f, 0.56f, 0.30f, 0.28f, 0.44f, lanes }; }
        { auto lanes = makeBaseTrapLanes(); lanes[1].noteProbability = 0.74f; lanes[1].hatFxIntensity = 0.7f; lanes[2].noteProbability = 0.52f; lanes[10].noteProbability = 0.22f; styles[5] = { GenreType::Trap, "LuxuryTrap", 145.0f, 142, 148, 52.0f, 0.54f, 0.30f, 0.30f, 0.52f, lanes }; }
        return styles;
    }();
    return table;
}

const std::array<GenreStyleDefaults, 1>& drillStyles()
{
    static const std::array<GenreStyleDefaults, 1> table = []
    {
        std::array<GenreStyleDefaults, 1> styles;
        auto lanes = makeBaseDrillLanes();
        styles[0] = { GenreType::Drill, "Main", 143.0f, 140, 146, 50.5f, 0.56f, 0.30f, 0.22f, 0.58f, lanes };
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

int getSelectedSubstyleIndex(const GeneratorParams& params)
{
    switch (params.genre)
    {
        case GenreType::Rap: return params.rapSubstyle;
        case GenreType::Trap: return params.trapSubstyle;
        case GenreType::Drill: return params.drillSubstyle;
        case GenreType::BoomBap:
        default: return params.boombapSubstyle;
    }
}

const GenreStyleDefaults& getGenreStyleDefaults(GenreType genre, int substyleIndex)
{
    if (genre == GenreType::Drill)
    {
        const auto& styles = drillStyles();
        const int idx = std::clamp(substyleIndex, 0, static_cast<int>(styles.size()) - 1);
        return styles[static_cast<size_t>(idx)];
    }

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

    const auto& styles = boomBapStyles();
    const int idx = std::clamp(substyleIndex, 0, static_cast<int>(styles.size()) - 1);
    return styles[static_cast<size_t>(idx)];
}

const LaneStyleDefaults& getLaneStyleDefaults(const GenreStyleDefaults& style, TrackType trackType)
{
    return style.laneDefaults[static_cast<size_t>(trackTypeToLaneIndex(trackType))];
}

float chooseDeterministicStyleBpm(const GeneratorParams& params)
{
    const int substyleIndex = getSelectedSubstyleIndex(params);
    const auto& style = getGenreStyleDefaults(params.genre, substyleIndex);
    const int bpmMin = juce::jmin(style.bpmMin, style.bpmMax);
    const int bpmMax = juce::jmax(style.bpmMin, style.bpmMax);
    if (bpmMax <= bpmMin)
        return static_cast<float>(bpmMin);

    const uint32_t hash = hashStyleBpmSeed(params, substyleIndex, style.substyleName);
    const uint32_t span = static_cast<uint32_t>(bpmMax - bpmMin + 1);
    return static_cast<float>(bpmMin + static_cast<int>(hash % span));
}

GenerationBpmSelection resolveGenerationBpm(const GeneratorParams& params,
                                            float currentBpm,
                                            bool bpmLocked,
                                            const std::optional<double>& hostTempo)
{
    if (params.syncDawTempo && hostTempo.has_value() && *hostTempo > 0.0)
        return { static_cast<float>(*hostTempo), GenerationBpmSource::HostSync };

    if (bpmLocked)
        return { currentBpm, GenerationBpmSource::BpmLock };

    return { chooseDeterministicStyleBpm(params), GenerationBpmSource::DeterministicStyleRange };
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
