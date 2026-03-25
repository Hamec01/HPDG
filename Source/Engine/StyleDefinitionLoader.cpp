#include "StyleDefinitionLoader.h"

#include <algorithm>

#include "BoomBap/BoomBapStyleProfile.h"
#include "Drill/DrillStyleProfile.h"
#include "Rap/RapStyleProfile.h"
#include "StyleDefaults.h"
#include "Trap/TrapStyleProfile.h"
#include "HiResTiming.h"
#include "../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
juce::String sanitizePathSegment(const juce::String& input)
{
    auto text = input.trim();
    if (text.isEmpty())
        return "Unsorted";

    juce::String cleaned;
    cleaned.preallocateBytes(text.getNumBytesAsUTF8());

    for (auto character : text)
    {
        if (juce::CharacterFunctions::isLetterOrDigit(character))
            cleaned << juce::String::charToString(character);
        else if (character == ' ' || character == '-' || character == '_')
            cleaned << "_";
    }

    cleaned = cleaned.trimCharactersAtStart("_").trimCharactersAtEnd("_");
    return cleaned.isEmpty() ? "Unsorted" : cleaned;
}

juce::String defaultLaneRoleForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return "core_pulse";
        case TrackType::Snare: return "backbeat";
        case TrackType::HiHat: return "carrier";
        case TrackType::OpenHat: return "accent";
        case TrackType::ClapGhostSnare: return "support";
        case TrackType::GhostKick: return "support";
        case TrackType::HatFX: return "accent_fx";
        case TrackType::Ride: return "support";
        case TrackType::Cymbal: return "crash";
        case TrackType::Perc: return "texture";
        case TrackType::Sub808: return "bass_anchor";
        default: break;
    }

    return "lane";
}

std::optional<TrackType> parseTrackTypeToken(const juce::String& text)
{
    const auto normalized = text.trim();
    if (normalized.isEmpty())
        return std::nullopt;

    for (const auto& info : TrackRegistry::all())
    {
        if (normalized == juce::String(toString(info.type)))
            return info.type;
    }

    return std::nullopt;
}

int clampedSubstyleIndex(GenreType genre, int substyleIndex)
{
    const auto names = [&]()
    {
        switch (genre)
        {
            case GenreType::Rap: return getRapSubstyleNames();
            case GenreType::Trap: return getTrapSubstyleNames();
            case GenreType::Drill: return getDrillSubstyleNames();
            case GenreType::BoomBap:
            default: return getBoomBapSubstyleNames();
        }
    }();

    if (names.isEmpty())
        return 0;

    return juce::jlimit(0, names.size() - 1, substyleIndex);
}

int substyleIndexForName(GenreType genre, const juce::String& substyleName)
{
    const auto names = [&]()
    {
        switch (genre)
        {
            case GenreType::Rap: return getRapSubstyleNames();
            case GenreType::Trap: return getTrapSubstyleNames();
            case GenreType::Drill: return getDrillSubstyleNames();
            case GenreType::BoomBap:
            default: return getBoomBapSubstyleNames();
        }
    }();

    for (int index = 0; index < names.size(); ++index)
    {
        if (names[index].equalsIgnoreCase(substyleName))
            return index;
    }

    return 0;
}

float clampUnit(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

ReferenceHatSkeleton extractReferenceHatSkeleton(const StyleLabReferenceRecord& record);
StyleLabReferenceDrillHatMotionSummary analyzeDrillHatReference(const StyleLabReferenceRecord& record);
ReferenceKickPattern extractReferenceKickPattern(const StyleLabReferenceRecord& record);

void applyDrillHatSummaryHints(ResolvedStyleDefinition& definition,
                               const StyleLabReferenceDrillHatMotionSummary& hatSummary)
{
    if (!hatSummary.available)
        return;

    definition.styleHints.set("drill.ref_hat_roll_length", hatSummary.averageRollLength);
    definition.styleHints.set("drill.ref_hat_density_variation", hatSummary.densityVariation);
    definition.styleHints.set("drill.ref_hat_accent_alternation", hatSummary.accentAlternation);
    definition.styleHints.set("drill.ref_hat_gap_intent", hatSummary.silenceGapIntent);
    definition.styleHints.set("drill.ref_hat_burst", hatSummary.burstClusterRate);
    definition.styleHints.set("drill.ref_hat_triplet", hatSummary.tripletRate);
    definition.styleHints.set("drill.hat_motion",
                              clampUnit(0.36f
                                        + hatSummary.averageRollLength * 0.18f
                                        + hatSummary.burstClusterRate * 0.18f
                                        + hatSummary.tripletRate * 0.12f
                                        + hatSummary.accentAlternation * 0.08f
                                        + hatSummary.densityVariation * 0.08f));
    definition.styleHints.set("drill.gap_intent",
                              clampUnit(0.45f * hatSummary.silenceGapIntent
                                        + 0.55f * static_cast<float>(definition.styleHints["drill.gap_intent"])));
}

StyleLabReferenceDrillHatMotionSummary averageDrillHatSummaries(const std::vector<StyleLabReferenceRecord>& records)
{
    StyleLabReferenceDrillHatMotionSummary averaged;
    float rollLength = 0.0f;
    float densityVariation = 0.0f;
    float accentAlternation = 0.0f;
    float silenceGapIntent = 0.0f;
    float burstClusterRate = 0.0f;
    float tripletRate = 0.0f;
    int count = 0;

    for (const auto& record : records)
    {
        const auto summary = analyzeDrillHatReference(record);
        if (!summary.available)
            continue;

        rollLength += summary.averageRollLength;
        densityVariation += summary.densityVariation;
        accentAlternation += summary.accentAlternation;
        silenceGapIntent += summary.silenceGapIntent;
        burstClusterRate += summary.burstClusterRate;
        tripletRate += summary.tripletRate;
        ++count;
    }

    if (count <= 0)
        return averaged;

    averaged.available = true;
    averaged.averageRollLength = rollLength / static_cast<float>(count);
    averaged.densityVariation = densityVariation / static_cast<float>(count);
    averaged.accentAlternation = accentAlternation / static_cast<float>(count);
    averaged.silenceGapIntent = silenceGapIntent / static_cast<float>(count);
    averaged.burstClusterRate = burstClusterRate / static_cast<float>(count);
    averaged.tripletRate = tripletRate / static_cast<float>(count);
    return averaged;
}

ReferenceHatCorpus buildReferenceHatCorpus(const std::vector<StyleLabReferenceRecord>& records)
{
    ReferenceHatCorpus corpus;
    for (const auto& record : records)
    {
        auto skeleton = extractReferenceHatSkeleton(record);
        if (!skeleton.available || skeleton.barMaps.empty())
            continue;
        corpus.variants.push_back(std::move(skeleton));
    }

    corpus.sourceReferenceCount = static_cast<int>(corpus.variants.size());
    corpus.available = !corpus.variants.empty();
    return corpus;
}

ReferenceKickCorpus buildReferenceKickCorpus(const std::vector<StyleLabReferenceRecord>& records)
{
    ReferenceKickCorpus corpus;
    for (const auto& record : records)
    {
        auto pattern = extractReferenceKickPattern(record);
        if (!pattern.available || pattern.barPatterns.empty())
            continue;
        corpus.variants.push_back(std::move(pattern));
    }

    corpus.sourceReferenceCount = static_cast<int>(corpus.variants.size());
    corpus.available = !corpus.variants.empty();
    return corpus;
}

float normalizedSwing(float swingPercent)
{
    return clampUnit((swingPercent - 50.0f) / 14.0f);
}

float stddevNormalized(const std::vector<float>& values, float divisorFloor)
{
    if (values.empty())
        return 0.0f;

    const float sum = std::accumulate(values.begin(), values.end(), 0.0f);
    const float mean = sum / static_cast<float>(values.size());
    float variance = 0.0f;
    for (const float value : values)
    {
        const float delta = value - mean;
        variance += delta * delta;
    }

    variance /= static_cast<float>(values.size());
    return clampUnit(std::sqrt(variance) / std::max(divisorFloor, mean));
}

struct ParsedReferenceNote
{
    int tick = 0;
    int velocity = 96;
};

struct ParsedReferenceRhythm
{
    int bars = 0;
    std::vector<ParsedReferenceNote> hats;
    std::vector<ParsedReferenceNote> kicks;
    std::vector<ParsedReferenceNote> snares;
};

bool metadataTrackMatches(const juce::DynamicObject& trackObject, TrackType type)
{
    const auto token = juce::String(toString(type));
    const auto runtimeTrackType = trackObject.getProperty("runtimeTrackType").toString().trim();
    const auto trackType = trackObject.getProperty("trackType").toString().trim();
    return runtimeTrackType == token || trackType == token;
}

std::vector<ParsedReferenceNote> collectMetadataNotes(const juce::Array<juce::var>& tracks,
                                                      std::initializer_list<TrackType> trackTypes)
{
    std::vector<ParsedReferenceNote> notes;

    for (const auto& trackVar : tracks)
    {
        auto* trackObject = trackVar.getDynamicObject();
        if (trackObject == nullptr)
            continue;

        bool matches = false;
        for (const auto trackType : trackTypes)
        {
            if (metadataTrackMatches(*trackObject, trackType))
            {
                matches = true;
                break;
            }
        }

        if (!matches)
            continue;

        auto* laneParams = trackObject->getProperty("laneParams").getDynamicObject();
        if (laneParams == nullptr)
            continue;

        auto* noteArray = laneParams->getProperty("notes").getArray();
        if (noteArray == nullptr)
            continue;

        notes.reserve(notes.size() + static_cast<size_t>(noteArray->size()));
        for (const auto& noteVar : *noteArray)
        {
            auto* noteObject = noteVar.getDynamicObject();
            if (noteObject == nullptr)
                continue;

            const auto stepVar = noteObject->getProperty("step");
            const auto microOffsetVar = noteObject->getProperty("microOffsetTicks");
            const auto startTickVar = noteObject->getProperty("startTick");
            const auto velocityVar = noteObject->getProperty("velocity");
            const int step = stepVar.isVoid() ? 0 : static_cast<int>(stepVar);
            const int microOffset = microOffsetVar.isVoid() ? 0 : static_cast<int>(microOffsetVar);
            const int startTick = startTickVar.isVoid()
                ? (step * HiResTiming::kTicks1_16 + microOffset)
                : static_cast<int>(startTickVar);
            const int velocity = velocityVar.isVoid() ? 96 : static_cast<int>(velocityVar);
            notes.push_back({ std::max(0, startTick), std::clamp(velocity, 1, 127) });
        }
    }

    std::sort(notes.begin(), notes.end(), [](const ParsedReferenceNote& left, const ParsedReferenceNote& right)
    {
        if (left.tick != right.tick)
            return left.tick < right.tick;
        return left.velocity > right.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const ParsedReferenceNote& left, const ParsedReferenceNote& right)
    {
        return left.tick == right.tick;
    }), notes.end());

    return notes;
}

std::optional<ParsedReferenceRhythm> parseReferenceRhythm(const StyleLabReferenceRecord& record)
{
    if (!record.metadataFile.existsAsFile())
        return std::nullopt;

    const auto json = juce::JSON::parse(record.metadataFile.loadFileAsString());
    auto* rootObject = json.getDynamicObject();
    if (rootObject == nullptr)
        return std::nullopt;

    auto* referenceProject = rootObject->getProperty("referenceProject").getDynamicObject();
    if (referenceProject == nullptr)
        return std::nullopt;

    auto* tracks = referenceProject->getProperty("tracks").getArray();
    if (tracks == nullptr)
        return std::nullopt;

    ParsedReferenceRhythm rhythm;
    rhythm.hats = collectMetadataNotes(*tracks, { TrackType::HiHat });
    rhythm.kicks = collectMetadataNotes(*tracks, { TrackType::Kick, TrackType::GhostKick });
    rhythm.snares = collectMetadataNotes(*tracks, { TrackType::Snare, TrackType::ClapGhostSnare });

    int inferredBars = 0;
    const auto inferBars = [&](const std::vector<ParsedReferenceNote>& notes)
    {
        if (!notes.empty())
            inferredBars = std::max(inferredBars, (notes.back().tick / HiResTiming::kTicksPerBar4_4) + 1);
    };
    inferBars(rhythm.hats);
    inferBars(rhythm.kicks);
    inferBars(rhythm.snares);
    rhythm.bars = std::max(1, record.bars > 0 ? record.bars : inferredBars);
    return rhythm;
}

template <typename T>
void sortAndUnique(std::vector<T>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

constexpr int referenceFastClusterGapThreshold()
{
    return std::max(HiResTiming::kTicks1_32, HiResTiming::kTicks1_24) + 8;
}

double anchorProximityValue(int tick, const std::vector<int>& anchors)
{
    double best = 0.0;
    for (const int anchor : anchors)
    {
        const int distance = std::abs(anchor - tick);
        if (distance <= HiResTiming::kTicks1_32)
            best = std::max(best, 1.0);
        else if (distance <= HiResTiming::kTicks1_16)
            best = std::max(best, 0.72);
        else if (distance <= HiResTiming::kTicks1_8)
            best = std::max(best, 0.44);
    }
    return best;
}

ReferenceHatSkeleton extractReferenceHatSkeleton(const StyleLabReferenceRecord& record)
{
    ReferenceHatSkeleton skeleton;
    if (record.genre.compareIgnoreCase("Drill") != 0)
        return skeleton;

    const auto rhythm = parseReferenceRhythm(record);
    if (!rhythm.has_value() || rhythm->hats.empty())
        return skeleton;

    skeleton.available = true;
    skeleton.sourceBars = std::max(1, rhythm->bars);
    skeleton.barMaps.resize(static_cast<size_t>(skeleton.sourceBars));
    for (int bar = 0; bar < skeleton.sourceBars; ++bar)
        skeleton.barMaps[static_cast<size_t>(bar)].barIndex = bar;

    for (int bar = 0; bar < skeleton.sourceBars; ++bar)
    {
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        const int barEnd = barStart + HiResTiming::kTicksPerBar4_4;
        auto& barMap = skeleton.barMaps[static_cast<size_t>(bar)];

        std::vector<ParsedReferenceNote> barHats;
        std::vector<int> barKickTicks;
        std::vector<int> barSnareTicks;
        for (const auto& note : rhythm->hats)
            if (note.tick >= barStart && note.tick < barEnd)
                barHats.push_back(note);
        for (const auto& note : rhythm->kicks)
            if (note.tick >= barStart && note.tick < barEnd)
                barKickTicks.push_back(note.tick);
        for (const auto& note : rhythm->snares)
            if (note.tick >= barStart && note.tick < barEnd)
                barSnareTicks.push_back(note.tick);

        if (barHats.empty())
            continue;

        const double velocityMean = std::accumulate(barHats.begin(), barHats.end(), 0.0,
            [](double sum, const ParsedReferenceNote& note) { return sum + static_cast<double>(note.velocity); })
            / static_cast<double>(barHats.size());

        for (const auto& note : barHats)
        {
            const int localTick = note.tick - barStart;
            const int step32 = std::clamp(HiResTiming::quantizeTicks(localTick, HiResTiming::kTicks1_32) / HiResTiming::kTicks1_32, 0, 31);
            const int step16 = std::clamp(HiResTiming::quantizeTicks(localTick, HiResTiming::kTicks1_16) / HiResTiming::kTicks1_16, 0, 15);
            const bool closeTo16 = std::abs(localTick - step16 * HiResTiming::kTicks1_16) <= HiResTiming::kTicks1_64;
            const bool strongMetric = (step32 % 8) == 0 || (step32 % 4) == 0;
            const bool nearKickAnchor = anchorProximityValue(note.tick, barKickTicks) >= 0.72;
            const bool phraseEdge = step32 == 0 || step32 >= 24;
            const bool hasVelocityAnchor = static_cast<double>(note.velocity) >= velocityMean;

            barMap.notes.push_back({ std::clamp(localTick, 0, HiResTiming::kTicksPerBar4_4 - 1), note.velocity });

            if (localTick <= HiResTiming::kTicks1_32)
                barMap.hasBarStartAnchor = true;

            if (closeTo16 && (strongMetric || nearKickAnchor || phraseEdge || hasVelocityAnchor))
                barMap.backboneSteps16.push_back(step16);
            else
                barMap.motionSteps32.push_back(step32);

            if (nearKickAnchor || phraseEdge || localTick <= HiResTiming::kTicks1_32)
                barMap.phraseAnchorSteps32.push_back(step32);

            for (const int snareTick : barSnareTicks)
            {
                const int delta = snareTick - note.tick;
                if (delta > 0 && delta <= HiResTiming::kTicks1_8)
                    barMap.preSnareZoneSteps32.push_back(step32);
                if (delta > 0 && delta <= HiResTiming::kTicks1_16)
                    barMap.phraseAnchorSteps32.push_back(step32);
            }
        }

        if (barMap.hasBarStartAnchor)
        {
            barMap.backboneSteps16.push_back(0);
            barMap.phraseAnchorSteps32.push_back(0);
        }

        if (barMap.backboneSteps16.empty())
            barMap.backboneSteps16.push_back(std::clamp(HiResTiming::quantizeTicks(barHats.front().tick - barStart, HiResTiming::kTicks1_16) / HiResTiming::kTicks1_16, 0, 15));

        sortAndUnique(barMap.backboneSteps16);
        sortAndUnique(barMap.motionSteps32);
        sortAndUnique(barMap.phraseAnchorSteps32);
        sortAndUnique(barMap.preSnareZoneSteps32);
        std::sort(barMap.notes.begin(), barMap.notes.end(), [](const ReferenceHatNote& left, const ReferenceHatNote& right)
        {
            if (left.tickInBar != right.tickInBar)
                return left.tickInBar < right.tickInBar;
            return left.velocity > right.velocity;
        });
        barMap.notes.erase(std::unique(barMap.notes.begin(), barMap.notes.end(), [](const ReferenceHatNote& left, const ReferenceHatNote& right)
        {
            return left.tickInBar == right.tickInBar;
        }), barMap.notes.end());
    }

    for (size_t index = 0; index < rhythm->hats.size();)
    {
        size_t next = index + 1;
        int tripletAdjacency = 0;
        int fastAdjacency = 0;
        while (next < rhythm->hats.size() && (rhythm->hats[next].tick - rhythm->hats[next - 1].tick) <= referenceFastClusterGapThreshold())
        {
            const int delta = rhythm->hats[next].tick - rhythm->hats[next - 1].tick;
            if (delta > 0)
            {
                ++fastAdjacency;
                if (std::abs(delta - HiResTiming::kTicks1_24) + 4 < std::abs(delta - HiResTiming::kTicks1_32))
                    ++tripletAdjacency;
            }
            ++next;
        }

        const int noteCount = static_cast<int>(next - index);
        if (noteCount >= 3)
        {
            const int startTick = rhythm->hats[index].tick;
            const int endTick = rhythm->hats[next - 1].tick;
            ReferenceHatCluster cluster;
            cluster.barIndex = std::max(0, startTick / HiResTiming::kTicksPerBar4_4);
            cluster.startStep32 = std::clamp((startTick % HiResTiming::kTicksPerBar4_4) / HiResTiming::kTicks1_32, 0, 31);
            cluster.endStep32 = std::clamp((endTick % HiResTiming::kTicksPerBar4_4) / HiResTiming::kTicks1_32, 0, 31);
            cluster.noteCount = noteCount;
            cluster.triplet = fastAdjacency > 0 && (tripletAdjacency * 2) >= fastAdjacency;
            skeleton.rollClusters.push_back(cluster);
            if (cluster.triplet)
                skeleton.tripletClusters.push_back(cluster);

            if (cluster.barIndex >= 0 && cluster.barIndex < static_cast<int>(skeleton.barMaps.size()))
            {
                auto& barMap = skeleton.barMaps[static_cast<size_t>(cluster.barIndex)];
                for (int step32 = cluster.startStep32; step32 <= cluster.endStep32; ++step32)
                    barMap.motionSteps32.push_back(step32);
                sortAndUnique(barMap.motionSteps32);
            }
        }

        index = next;
    }

    return skeleton;
}

ReferenceKickPattern extractReferenceKickPattern(const StyleLabReferenceRecord& record)
{
    ReferenceKickPattern pattern;
    if (record.genre.compareIgnoreCase("Drill") != 0)
        return pattern;

    const auto rhythm = parseReferenceRhythm(record);
    if (!rhythm.has_value() || rhythm->kicks.empty())
        return pattern;

    pattern.available = true;
    pattern.sourceBars = std::max(1, rhythm->bars);
    pattern.barPatterns.resize(static_cast<size_t>(pattern.sourceBars));
    for (int bar = 0; bar < pattern.sourceBars; ++bar)
        pattern.barPatterns[static_cast<size_t>(bar)].barIndex = bar;

    for (const auto& note : rhythm->kicks)
    {
        const int bar = std::clamp(note.tick / HiResTiming::kTicksPerBar4_4, 0, pattern.sourceBars - 1);
        const int localTick = std::max(0, note.tick % HiResTiming::kTicksPerBar4_4);
        const int step16 = std::clamp(HiResTiming::quantizeTicks(localTick, HiResTiming::kTicks1_16) / HiResTiming::kTicks1_16, 0, 15);
        pattern.barPatterns[static_cast<size_t>(bar)].notes.push_back({ step16, note.velocity });
    }

    for (auto& barPattern : pattern.barPatterns)
    {
        std::sort(barPattern.notes.begin(), barPattern.notes.end(), [](const ReferenceKickNote& left, const ReferenceKickNote& right)
        {
            if (left.step16 != right.step16)
                return left.step16 < right.step16;
            return left.velocity > right.velocity;
        });
        barPattern.notes.erase(std::unique(barPattern.notes.begin(), barPattern.notes.end(), [](const ReferenceKickNote& left, const ReferenceKickNote& right)
        {
            return left.step16 == right.step16;
        }), barPattern.notes.end());
    }

    return pattern;
}

StyleLabReferenceDrillHatMotionSummary analyzeDrillHatReference(const StyleLabReferenceRecord& record)
{
    StyleLabReferenceDrillHatMotionSummary summary;
    if (record.genre.compareIgnoreCase("Drill") != 0)
        return summary;

    const auto rhythm = parseReferenceRhythm(record);
    if (!rhythm.has_value() || rhythm->hats.empty())
        return summary;

    struct HatNote
    {
        int tick = 0;
        int velocity = 0;
        int bar = 0;
    };

    std::vector<HatNote> notes;
    notes.reserve(rhythm->hats.size());
    for (const auto& note : rhythm->hats)
        notes.push_back({ note.tick, note.velocity, std::max(0, note.tick / HiResTiming::kTicksPerBar4_4) });

    std::sort(notes.begin(), notes.end(), [](const HatNote& left, const HatNote& right)
    {
        if (left.tick != right.tick)
            return left.tick < right.tick;
        return left.velocity > right.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const HatNote& left, const HatNote& right)
    {
        return left.tick == right.tick;
    }), notes.end());

    const int totalBars = std::max(1, rhythm->bars > 0 ? rhythm->bars : (notes.back().bar + 1));
    std::vector<float> perBarCounts(static_cast<size_t>(totalBars), 0.0f);
    std::vector<float> perBarMaxGap(static_cast<size_t>(totalBars), 0.0f);
    for (const auto& note : notes)
        perBarCounts[static_cast<size_t>(std::clamp(note.bar, 0, totalBars - 1))] += 1.0f;

    std::vector<int> clusterSizes;
    int tripletAdjacency = 0;
    int fastAdjacency = 0;
    for (size_t i = 0; i < notes.size();)
    {
        size_t j = i + 1;
        while (j < notes.size() && (notes[j].tick - notes[j - 1].tick) <= referenceFastClusterGapThreshold())
        {
            const int delta = notes[j].tick - notes[j - 1].tick;
            if (delta > 0 && delta <= referenceFastClusterGapThreshold())
            {
                ++fastAdjacency;
                const int distance24 = std::abs(delta - HiResTiming::kTicks1_24);
                const int distance32 = std::abs(delta - HiResTiming::kTicks1_32);
                if (distance24 + 4 < distance32)
                    ++tripletAdjacency;
            }
            ++j;
        }

        const int clusterSize = static_cast<int>(j - i);
        if (clusterSize >= 3)
            clusterSizes.push_back(clusterSize);
        i = j;
    }

    int alternatingTriples = 0;
    int alternatingEligible = 0;
    for (size_t i = 1; i + 1 < notes.size(); ++i)
    {
        const int deltaA = notes[i].tick - notes[i - 1].tick;
        const int deltaB = notes[i + 1].tick - notes[i].tick;
        if (deltaA > HiResTiming::kTicks1_16 || deltaB > HiResTiming::kTicks1_16)
            continue;

        const int diffA = notes[i].velocity - notes[i - 1].velocity;
        const int diffB = notes[i + 1].velocity - notes[i].velocity;
        if (std::abs(diffA) < 10 || std::abs(diffB) < 10)
            continue;

        ++alternatingEligible;
        if ((diffA > 0 && diffB < 0) || (diffA < 0 && diffB > 0))
            ++alternatingTriples;
    }

    for (int bar = 0; bar < totalBars; ++bar)
    {
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        const int barEnd = barStart + HiResTiming::kTicksPerBar4_4;
        std::vector<int> ticks;
        for (const auto& note : notes)
        {
            if (note.tick >= barStart && note.tick < barEnd)
                ticks.push_back(note.tick - barStart);
        }

        if (ticks.empty())
        {
            perBarMaxGap[static_cast<size_t>(bar)] = static_cast<float>(HiResTiming::kTicksPerBar4_4);
            continue;
        }

        int maxGap = ticks.front();
        for (size_t i = 1; i < ticks.size(); ++i)
            maxGap = std::max(maxGap, ticks[i] - ticks[i - 1]);
        maxGap = std::max(maxGap, barEnd - barStart - ticks.back());
        perBarMaxGap[static_cast<size_t>(bar)] = static_cast<float>(maxGap);
    }

    const float averageClusterSize = clusterSizes.empty()
        ? 0.0f
        : std::accumulate(clusterSizes.begin(), clusterSizes.end(), 0.0f) / static_cast<float>(clusterSizes.size());
    const float clustersPerBar = static_cast<float>(clusterSizes.size()) / static_cast<float>(totalBars);
    const float averageGap = std::accumulate(perBarMaxGap.begin(), perBarMaxGap.end(), 0.0f) / static_cast<float>(perBarMaxGap.size());

    summary.available = true;
    summary.averageRollLength = clampUnit((averageClusterSize - 2.0f) / 4.0f);
    summary.densityVariation = stddevNormalized(perBarCounts, 3.0f);
    summary.accentAlternation = alternatingEligible > 0
        ? clampUnit(static_cast<float>(alternatingTriples) / static_cast<float>(alternatingEligible))
        : 0.0f;
    summary.silenceGapIntent = clampUnit(averageGap / static_cast<float>(HiResTiming::kTicksPerBar4_4 / 2));
    summary.burstClusterRate = clampUnit(clustersPerBar / 1.6f);
    summary.tripletRate = fastAdjacency > 0
        ? clampUnit(static_cast<float>(tripletAdjacency) / static_cast<float>(fastAdjacency))
        : 0.0f;
    return summary;
}

void setLaneHint(StyleDefinitionLane& lane, const juce::Identifier& key, float value)
{
    lane.skeletonHints.set(key, clampUnit(value));
}

void populateLaneHints(StyleDefinitionLane& lane, const LaneStyleDefaults& laneDefaults, TrackType trackType)
{
    setLaneHint(lane, "lane.densityBias", laneDefaults.densityBias);
    setLaneHint(lane, "lane.timingBias", laneDefaults.timingBias);
    setLaneHint(lane, "lane.humanizeBias", laneDefaults.humanizeBias);
    setLaneHint(lane, "lane.noteProbability", laneDefaults.noteProbability);
    setLaneHint(lane, "lane.phraseEndingProbability", laneDefaults.phraseEndingProbability);
    setLaneHint(lane, "lane.mutationIntensity", laneDefaults.mutationIntensity);
    setLaneHint(lane, "lane.rgVariationIntensity", laneDefaults.rgVariationIntensity);
    setLaneHint(lane, "lane.hatFxIntensity", laneDefaults.hatFxIntensity);
    setLaneHint(lane, "lane.sub808Activity", laneDefaults.sub808Activity);

    switch (trackType)
    {
        case TrackType::Kick:
            lane.notePriorityHints.set("groove.anchorStrength", clampUnit(laneDefaults.densityBias));
            break;
        case TrackType::HiHat:
            lane.notePriorityHints.set("groove.motionStrength", clampUnit(laneDefaults.densityBias));
            break;
        case TrackType::OpenHat:
            lane.notePriorityHints.set("groove.openAccent", clampUnit(laneDefaults.noteProbability));
            break;
        case TrackType::Perc:
            lane.notePriorityHints.set("groove.textureWeight", clampUnit(laneDefaults.densityBias));
            break;
        case TrackType::ClapGhostSnare:
            lane.notePriorityHints.set("groove.backbeatSupport", clampUnit(laneDefaults.noteProbability));
            break;
        case TrackType::Sub808:
            lane.notePriorityHints.set("groove.bassWeight", clampUnit(juce::jmax(laneDefaults.sub808Activity, laneDefaults.densityBias)));
            break;
        default:
            break;
    }
}

void populateSharedStyleHints(ResolvedStyleDefinition& definition,
                              const GenreStyleDefaults& styleDefaults)
{
    definition.styleHints.set("groove.swing", normalizedSwing(styleDefaults.swingDefault));
    definition.styleHints.set("groove.timing", clampUnit(styleDefaults.timingDefault));
    definition.styleHints.set("groove.humanize", clampUnit(styleDefaults.humanizeDefault));
    definition.styleHints.set("groove.density", clampUnit(styleDefaults.densityDefault));
}

void populateGenreStyleHints(ResolvedStyleDefinition& definition, GenreType genre, int substyleIndex)
{
    switch (genre)
    {
        case GenreType::BoomBap:
        {
            const auto& style = getBoomBapProfile(substyleIndex);
            definition.styleHints.set("boom_bap.groove_looseness", clampUnit(style.barVariationAmount + style.halfTimeReferenceBias * 0.35f));
            definition.styleHints.set("boom_bap.perc_sparsity", clampUnit(1.0f - style.percDensityBias));
            definition.styleHints.set("boom_bap.clap_focus", clampUnit(style.laneClapActivity));
            definition.styleHints.set("boom_bap.swing_feel", normalizedSwing(style.swingPercent));
            break;
        }
        case GenreType::Rap:
        {
            const auto& style = getRapProfile(substyleIndex);
            definition.styleHints.set("rap.groove_looseness", clampUnit(style.relaxedTiming));
            definition.styleHints.set("rap.support_density", clampUnit((style.openHatChance + style.percChance + style.rideChance) * 1.8f));
            definition.styleHints.set("rap.accent_push", clampUnit(style.clapLayerChance));
            definition.styleHints.set("rap.swing_feel", normalizedSwing(style.swingPercent));
            break;
        }
        case GenreType::Trap:
        {
            const auto& style = getTrapProfile(substyleIndex);
            const auto styleSpec = getTrapStyleSpec(style.substyle);
            definition.styleHints.set("trap.hat_subdivision", clampUnit((style.hatDensityBias + style.hatFxIntensity + styleSpec.hatFxDensityMax) / 3.0f));
            definition.styleHints.set("trap.bounce", clampUnit((styleSpec.burstWindowPrimaryBias + styleSpec.majorBurstPerBarMax * 0.25f) / 2.0f));
            definition.styleHints.set("trap.emphasis_808", clampUnit(style.sub808Activity));
            definition.styleHints.set("trap.open_hat_profile", clampUnit(style.openHatChance * 2.0f));
            break;
        }
        case GenreType::Drill:
        {
            const auto& style = getDrillProfile(substyleIndex);
            const auto styleSpec = getDrillStyleSpec(style.substyle);
            definition.styleHints.set("drill.anchor_rigidity", clampUnit(styleSpec.phraseEdgeAnchorProbability + styleSpec.followKickProbability * 0.25f));
            definition.styleHints.set("drill.hat_motion", clampUnit(static_cast<float>(style.baseRoll + style.baseAccent)));
            definition.styleHints.set("drill.kick_808_coupling", clampUnit(styleSpec.followKickProbability));
            definition.styleHints.set("drill.gap_intent", clampUnit(styleSpec.counterGapProbability + (styleSpec.preferSparseSpace ? 0.2f : 0.0f)));
            break;
        }
        default:
            break;
    }
}
}

std::optional<StyleDefinition> StyleDefinitionLoader::loadLatestForStyle(const juce::String& genreName,
                                                                         const juce::String& substyleName,
                                                                         const juce::File& rootDirectory,
                                                                         juce::String* errorMessage)
{
    juce::String status;
    const auto styleDirectory = rootDirectory
        .getChildFile(sanitizePathSegment(genreName))
        .getChildFile(sanitizePathSegment(substyleName));

    if (!styleDirectory.isDirectory())
    {
        status = "No Style Lab reference directory for " + genreName + " / " + substyleName + ".";
        if (errorMessage != nullptr)
            *errorMessage = status;
        return std::nullopt;
    }

    std::vector<juce::File> candidates;
    for (const auto& entry : juce::RangedDirectoryIterator(styleDirectory, false, "*", juce::File::findDirectories))
    {
        const auto directory = entry.getFile();
        if (directory.getChildFile("metadata.json").existsAsFile())
            candidates.push_back(directory);
    }

    std::sort(candidates.begin(), candidates.end(), [](const juce::File& lhs, const juce::File& rhs)
    {
        return lhs.getFileName() > rhs.getFileName();
    });

    GenreType genre = GenreType::BoomBap;
    if (genreName.equalsIgnoreCase("Rap"))
        genre = GenreType::Rap;
    else if (genreName.equalsIgnoreCase("Trap"))
        genre = GenreType::Trap;
    else if (genreName.equalsIgnoreCase("Drill"))
        genre = GenreType::Drill;

    std::vector<StyleLabReferenceRecord> matchingRecords;
    for (const auto& directory : candidates)
    {
        juce::String parseError;
        const auto metadataJson = directory.getChildFile("metadata.json").loadFileAsString();
        const auto record = StyleLabReferenceBrowserService::parseMetadataJson(metadataJson, directory, &parseError);
        if (!record.has_value())
        {
            status = "Failed to parse Style Lab metadata at " + directory.getFullPathName() + ": " + parseError;
            continue;
        }

        if (!record->genre.equalsIgnoreCase(genreName) || !record->substyle.equalsIgnoreCase(substyleName))
            continue;

        matchingRecords.push_back(*record);
    }

    if (!matchingRecords.empty())
    {
        auto definition = fromReferenceRecord(genre, matchingRecords.front());
        if (genre == GenreType::Drill && substyleName.equalsIgnoreCase("UKDrill"))
        {
            const auto corpus = buildReferenceHatCorpus(matchingRecords);
            if (corpus.available)
                definition.referenceHatCorpus = corpus;
            const auto kickCorpus = buildReferenceKickCorpus(matchingRecords);
            if (kickCorpus.available)
                definition.referenceKickCorpus = kickCorpus;
            applyDrillHatSummaryHints(definition, averageDrillHatSummaries(matchingRecords));
        }

        if (errorMessage != nullptr)
            *errorMessage = {};
        return definition;
    }

    if (errorMessage != nullptr)
        *errorMessage = status.isNotEmpty() ? status : "No valid Style Lab reference metadata found.";
    return std::nullopt;
}

StyleDefinition StyleDefinitionLoader::buildFallback(GenreType genre, int substyleIndex)
{
    const auto clampedIndex = clampedSubstyleIndex(genre, substyleIndex);
    StyleDefinition definition;
    definition.genre = genre;
    definition.genreName = genreDisplayName(genre);
    definition.substyleName = substyleNameFor(genre, clampedIndex);
    definition.source = "StyleDefaults fallback";
    definition.loadedFromReference = false;

    const auto& styleDefaults = getGenreStyleDefaults(genre, clampedIndex);
    auto profile = TrackRegistry::createDefaultRuntimeLaneProfile();
    profile.genre = definition.genreName;
    profile.substyle = definition.substyleName;

    populateSharedStyleHints(definition, styleDefaults);
    populateGenreStyleHints(definition, genre, clampedIndex);

    definition.lanes.reserve(profile.lanes.size());
    for (const auto& runtimeLane : profile.lanes)
    {
        StyleDefinitionLane lane;
        lane.laneId = runtimeLane.laneId;
        lane.laneName = runtimeLane.laneName;
        lane.groupName = runtimeLane.groupName;
        lane.dependencyName = runtimeLane.dependencyName;
        lane.generationPriority = runtimeLane.generationPriority;
        lane.isCore = runtimeLane.isCore;
        lane.isVisibleInEditor = runtimeLane.isVisibleInEditor;
        lane.enabledByDefault = runtimeLane.enabledByDefault;
        lane.supportsDragExport = runtimeLane.supportsDragExport;
        lane.isGhostTrack = runtimeLane.isGhostTrack;
        lane.defaultMidiNote = runtimeLane.defaultMidiNote;
        lane.isRuntimeRegistryLane = runtimeLane.isRuntimeRegistryLane;
        lane.runtimeTrackType = runtimeLane.runtimeTrackType;

        if (runtimeLane.runtimeTrackType.has_value())
        {
            const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, *runtimeLane.runtimeTrackType);
            lane.enabledByDefault = laneDefaults.enabledByDefault;
            lane.laneParams.available = true;
            lane.laneParams.enabled = laneDefaults.enabledByDefault;
            lane.laneParams.laneVolume = laneDefaults.volumeDefault;
            lane.laneParams.laneRole = defaultLaneRoleForTrack(*runtimeLane.runtimeTrackType);
            populateLaneHints(lane, laneDefaults, *runtimeLane.runtimeTrackType);
        }

        definition.lanes.push_back(std::move(lane));
    }

    return definition;
}

StyleDefinition StyleDefinitionLoader::fromReferenceRecord(GenreType genre, const StyleLabReferenceRecord& record)
{
    auto definition = buildFallback(genre, substyleIndexForName(genre, record.substyle));
    definition.genreName = record.genre;
    definition.substyleName = record.substyle;
    definition.source = record.directory.getFileName();
    definition.loadedFromReference = true;
    definition.lanes.clear();
    definition.lanes.reserve(record.runtimeLanes.size());

    if (genre == GenreType::Drill)
    {
        definition.referenceHatSkeleton = extractReferenceHatSkeleton(record);
        applyDrillHatSummaryHints(definition, analyzeDrillHatReference(record));
    }

    for (const auto& sourceLane : record.runtimeLanes)
    {
        StyleDefinitionLane lane;
        lane.laneId = sourceLane.laneId;
        lane.laneName = sourceLane.laneName;
        lane.groupName = sourceLane.groupName;
        lane.dependencyName = sourceLane.dependencyName;
        lane.generationPriority = sourceLane.generationPriority;
        lane.isCore = sourceLane.isCore;
        lane.isVisibleInEditor = sourceLane.isVisibleInEditor;
        lane.enabledByDefault = sourceLane.enabledByDefault;
        lane.supportsDragExport = sourceLane.supportsDragExport;
        lane.isGhostTrack = sourceLane.isGhostTrack;
        lane.defaultMidiNote = sourceLane.defaultMidiNote;
        lane.isRuntimeRegistryLane = sourceLane.isRuntimeRegistryLane;
        lane.laneParams = sourceLane.laneParams;

        if (sourceLane.bindingKind == "backed")
        {
            lane.runtimeTrackType = parseTrackTypeToken(sourceLane.runtimeTrackType);
            if (lane.runtimeTrackType.has_value())
            {
                const auto& styleDefaults = getGenreStyleDefaults(genre, substyleIndexForName(genre, record.substyle));
                const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, *lane.runtimeTrackType);
                populateLaneHints(lane, laneDefaults, *lane.runtimeTrackType);
            }
        }

        definition.lanes.push_back(std::move(lane));
    }

    return definition;
}

juce::String StyleDefinitionLoader::genreDisplayName(GenreType genre)
{
    switch (genre)
    {
        case GenreType::Rap: return "Rap";
        case GenreType::Trap: return "Trap";
        case GenreType::Drill: return "Drill";
        case GenreType::BoomBap:
        default: return "Boom Bap";
    }
}

juce::String StyleDefinitionLoader::substyleNameFor(GenreType genre, int substyleIndex)
{
    const auto index = clampedSubstyleIndex(genre, substyleIndex);
    const auto names = [&]()
    {
        switch (genre)
        {
            case GenreType::Rap: return getRapSubstyleNames();
            case GenreType::Trap: return getTrapSubstyleNames();
            case GenreType::Drill: return getDrillSubstyleNames();
            case GenreType::BoomBap:
            default: return getBoomBapSubstyleNames();
        }
    }();

    if (names.isEmpty())
        return getGenreStyleDefaults(genre, 0).substyleName;

    return names[index];
}
} // namespace bbg