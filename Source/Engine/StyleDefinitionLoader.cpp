#include "StyleDefinitionLoader.h"

#include <algorithm>
#include <map>

#include "BoomBap/BoomBapStyleProfile.h"
#include "Rap/RapStyleProfile.h"
#include "StyleDefaults.h"
#include "Trap/TrapStyleProfile.h"
#include "HiResTiming.h"
#include "../Core/TrackSemantics.h"
#include "../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
constexpr int kDefaultMaxRankedReferences = 0;

int configuredMaxRankedReferences()
{
    const auto value = juce::SystemStats::getEnvironmentVariable("HPDG_STYLELAB_TOP_N", {});
    if (value.trim().isEmpty())
        return kDefaultMaxRankedReferences;

    const int parsed = value.getIntValue();
    return parsed > 0 ? parsed : 0;
}

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
            case GenreType::Drill: return getDrillSubstyleNames();
            case GenreType::Rap: return getRapSubstyleNames();
            case GenreType::Trap: return getTrapSubstyleNames();
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
            case GenreType::Drill: return getDrillSubstyleNames();
            case GenreType::Rap: return getRapSubstyleNames();
            case GenreType::Trap: return getTrapSubstyleNames();
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
                               const StyleLabReferenceDrillHatMotionSummary& hatSummary);
ReferenceHatCorpus buildReferenceHatCorpus(const std::vector<StyleLabReferenceRecord>& records);
ReferenceKickCorpus buildReferenceKickCorpus(const std::vector<StyleLabReferenceRecord>& records);
BrooklynReferenceProfile buildBrooklynReferenceProfile(const std::vector<StyleLabReferenceRecord>& records,
                                                       const std::vector<float>& weights);
struct ParsedReferenceRhythm;
std::optional<ParsedReferenceRhythm> parseReferenceRhythm(const StyleLabReferenceRecord& record);

struct BrooklynParsedReferenceNote
{
    int tick = 0;
    int velocity = 96;
};

bool metadataTrackMatchesBrooklyn(const juce::DynamicObject& trackObject, TrackType type)
{
    const auto token = juce::String(toString(type));
    const auto runtimeTrackType = trackObject.getProperty("runtimeTrackType").toString().trim();
    const auto trackType = trackObject.getProperty("trackType").toString().trim();
    return runtimeTrackType == token || trackType == token;
}

std::vector<BrooklynParsedReferenceNote> collectBrooklynMetadataNotes(const juce::Array<juce::var>& tracks,
                                                                      std::initializer_list<TrackType> trackTypes)
{
    std::vector<BrooklynParsedReferenceNote> notes;

    for (const auto& trackVar : tracks)
    {
        auto* trackObject = trackVar.getDynamicObject();
        if (trackObject == nullptr)
            continue;

        bool matches = false;
        for (const auto trackType : trackTypes)
        {
            if (metadataTrackMatchesBrooklyn(*trackObject, trackType))
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

    std::sort(notes.begin(), notes.end(), [](const BrooklynParsedReferenceNote& left, const BrooklynParsedReferenceNote& right)
    {
        if (left.tick != right.tick)
            return left.tick < right.tick;
        return left.velocity > right.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const BrooklynParsedReferenceNote& left, const BrooklynParsedReferenceNote& right)
    {
        return left.tick == right.tick;
    }), notes.end());

    return notes;
}

constexpr int brooklynFastClusterGapThreshold()
{
    return std::max(HiResTiming::kTicks1_32, HiResTiming::kTicks1_24) + 8;
}

float weightedAverage(const std::vector<float>& values, const std::vector<float>& weights)
{
    double weightedSum = 0.0;
    double totalWeight = 0.0;

    for (size_t index = 0; index < values.size() && index < weights.size(); ++index)
    {
        weightedSum += static_cast<double>(values[index]) * static_cast<double>(weights[index]);
        totalWeight += static_cast<double>(weights[index]);
    }

    if (totalWeight <= 0.0)
        return 0.0f;

    return static_cast<float>(weightedSum / totalWeight);
}

GenreType genreTypeFromDisplayName(const juce::String& genreName)
{
    if (genreName.equalsIgnoreCase("Drill"))
        return GenreType::Drill;
    if (genreName.equalsIgnoreCase("Rap"))
        return GenreType::Rap;
    if (genreName.equalsIgnoreCase("Trap"))
        return GenreType::Trap;
    return GenreType::BoomBap;
}

juce::Time referenceSortTime(const StyleLabReferenceRecord& record)
{
    if (record.exportedAt.isNotEmpty())
    {
        const auto parsed = juce::Time::fromISO8601(record.exportedAt);
        if (parsed.toMilliseconds() > 0)
            return parsed;
    }

    if (record.metadataFile.existsAsFile())
        return record.metadataFile.getLastModificationTime();

    return record.directory.getLastModificationTime();
}

float taggedNoteRatio(const StyleLabReferenceRecord& record)
{
    if (record.totalNotes <= 0)
        return 0.0f;
    return clampUnit(static_cast<float>(record.taggedNotes) / static_cast<float>(record.totalNotes));
}

float normalizedNoteDensity(const StyleLabReferenceRecord& record)
{
    if (record.totalNotes <= 0 || record.bars <= 0)
        return 0.0f;

    const auto notesPerBar = static_cast<float>(record.totalNotes) / static_cast<float>(record.bars);
    return clampUnit(notesPerBar / 24.0f);
}

float backedLaneRatio(const StyleLabReferenceRecord& record)
{
    if (record.totalRuntimeLaneCount <= 0)
        return 0.0f;
    return clampUnit(static_cast<float>(record.backedLaneCount) / static_cast<float>(record.totalRuntimeLaneCount));
}

int availableSubstyleDirectoryCount(const juce::File& genreDirectory)
{
    if (!genreDirectory.isDirectory())
        return 0;

    int count = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(genreDirectory, false, "*", juce::File::findDirectories))
    {
        juce::ignoreUnused(entry);
        ++count;
    }
    return count;
}

std::vector<float> referenceAggregationWeights(size_t count)
{
    static constexpr float kBaseWeights[] = { 1.0f, 0.65f, 0.45f };

    std::vector<float> weights;
    weights.reserve(count);
    for (size_t index = 0; index < count; ++index)
        weights.push_back(kBaseWeights[index < std::size(kBaseWeights) ? index : std::size(kBaseWeights) - 1]);
    return weights;
}

struct RankedReferenceEntry
{
    StyleLabReferenceRecord record;
    juce::Time sortTime;
};

enum class ReferenceLaneTarget
{
    Hat = 0,
    Kick
};

StyleLabReferenceZeroReason dominantZeroReason(int parsingFailed,
                                               int laneMappingEmpty,
                                               int incompatibleSpan,
                                               int filteredByDensity)
{
    const int counts[] { parsingFailed, laneMappingEmpty, incompatibleSpan, filteredByDensity };
    const StyleLabReferenceZeroReason reasons[] {
        StyleLabReferenceZeroReason::ParsingFailed,
        StyleLabReferenceZeroReason::LaneMappingEmpty,
        StyleLabReferenceZeroReason::IncompatibleReferenceSpan,
        StyleLabReferenceZeroReason::FilteredByDensity
    };

    int bestIndex = -1;
    int bestCount = 0;
    for (int index = 0; index < 4; ++index)
    {
        if (counts[index] > bestCount)
        {
            bestIndex = index;
            bestCount = counts[index];
        }
    }

    return bestIndex >= 0 ? reasons[bestIndex] : StyleLabReferenceZeroReason::NoRefsSelected;
}

juce::String buildLaneDiagnosticDetail(ReferenceLaneTarget target,
                                       int requestedCount,
                                       int parsingFailed,
                                       int laneMappingEmpty,
                                       int incompatibleSpan,
                                       int filteredByDensity)
{
    const auto laneLabel = target == ReferenceLaneTarget::Hat ? "hat" : "kick";
    juce::StringArray parts;

    if (parsingFailed > 0)
        parts.add(juce::String(parsingFailed) + "/" + juce::String(requestedCount) + " selected refs could not be parsed into " + laneLabel + " input.");
    if (laneMappingEmpty > 0)
        parts.add(juce::String(laneMappingEmpty) + "/" + juce::String(requestedCount) + " selected refs had no " + laneLabel + " lane note data in referenceProject.tracks[].laneParams.notes.");
    if (incompatibleSpan > 0)
        parts.add(juce::String(incompatibleSpan) + "/" + juce::String(requestedCount) + " selected refs had an incompatible bar span.");
    if (filteredByDensity > 0)
        parts.add(juce::String(filteredByDensity) + "/" + juce::String(requestedCount) + " selected refs were filtered by density.");

    return parts.joinIntoString(" ");
}

StyleLabReferenceLaneDiagnostics zeroLaneDiagnostics(bool styleSwitchDisabled,
                                                     int candidateDirectoryCount,
                                                     int parseFailureCount,
                                                     const juce::String& loadMessage)
{
    StyleLabReferenceLaneDiagnostics diagnostics;
    diagnostics.requestedCount = 0;
    diagnostics.resolvedCount = 0;

    if (styleSwitchDisabled)
    {
        diagnostics.zeroReason = StyleLabReferenceZeroReason::RefsDisabledByStyleSwitch;
        diagnostics.detail = "Current genre/substyle has no Style Lab reference folder, but other style folders exist in this genre.";
        return diagnostics;
    }

    if (candidateDirectoryCount > 0 && parseFailureCount >= candidateDirectoryCount)
    {
        diagnostics.zeroReason = StyleLabReferenceZeroReason::ParsingFailed;
        diagnostics.detail = juce::String(parseFailureCount) + "/" + juce::String(candidateDirectoryCount) + " candidate metadata files failed to parse.";
        return diagnostics;
    }

    diagnostics.zeroReason = StyleLabReferenceZeroReason::NoRefsSelected;
    diagnostics.detail = loadMessage.isNotEmpty() ? loadMessage : "No matching Style Lab refs were selected for this style.";
    return diagnostics;
}

bool isNumericVar(const juce::var& value)
{
    return value.isInt() || value.isInt64() || value.isDouble() || value.isBool();
}

StyleLabReferenceDrillHatMotionSummary weightedDrillHatSummary(const std::vector<StyleLabReferenceRecord>& records,
                                                               const std::vector<float>& weights)
{
    StyleLabReferenceDrillHatMotionSummary averaged;
    std::vector<float> rollLength;
    std::vector<float> densityVariation;
    std::vector<float> accentAlternation;
    std::vector<float> silenceGapIntent;
    std::vector<float> burstClusterRate;
    std::vector<float> tripletRate;
    std::vector<float> usedWeights;

    for (size_t index = 0; index < records.size() && index < weights.size(); ++index)
    {
        const auto summary = analyzeDrillHatReference(records[index]);
        if (!summary.available)
            continue;

        rollLength.push_back(summary.averageRollLength);
        densityVariation.push_back(summary.densityVariation);
        accentAlternation.push_back(summary.accentAlternation);
        silenceGapIntent.push_back(summary.silenceGapIntent);
        burstClusterRate.push_back(summary.burstClusterRate);
        tripletRate.push_back(summary.tripletRate);
        usedWeights.push_back(weights[index]);
    }

    if (usedWeights.empty())
        return averaged;

    averaged.available = true;
    averaged.averageRollLength = weightedAverage(rollLength, usedWeights);
    averaged.densityVariation = weightedAverage(densityVariation, usedWeights);
    averaged.accentAlternation = weightedAverage(accentAlternation, usedWeights);
    averaged.silenceGapIntent = weightedAverage(silenceGapIntent, usedWeights);
    averaged.burstClusterRate = weightedAverage(burstClusterRate, usedWeights);
    averaged.tripletRate = weightedAverage(tripletRate, usedWeights);
    return averaged;
}

void applyReferenceMetadataHints(ResolvedStyleDefinition& definition,
                                 const std::vector<StyleLabReferenceRecord>& records,
                                 const std::vector<float>& weights)
{
    if (records.empty())
        return;

    std::vector<float> priorities;
    std::vector<float> noteCoverage;
    std::vector<float> noteDensity;
    std::vector<float> laneCoverage;

    priorities.reserve(records.size());
    noteCoverage.reserve(records.size());
    noteDensity.reserve(records.size());
    laneCoverage.reserve(records.size());

    for (const auto& record : records)
    {
        priorities.push_back(clampUnit(static_cast<float>(record.referencePriority) / 100.0f));
        noteCoverage.push_back(taggedNoteRatio(record));
        noteDensity.push_back(normalizedNoteDensity(record));
        laneCoverage.push_back(backedLaneRatio(record));
    }

    definition.styleHints.set("library.reference_priority", weightedAverage(priorities, weights));
    definition.styleHints.set("library.tagged_note_ratio", weightedAverage(noteCoverage, weights));
    definition.styleHints.set("library.note_density", weightedAverage(noteDensity, weights));
    definition.styleHints.set("library.backed_lane_ratio", weightedAverage(laneCoverage, weights));
    definition.styleHints.set("library.reference_count", static_cast<int>(records.size()));
}

void applyWeightedStyleHints(ResolvedStyleDefinition& definition,
                             const std::vector<ResolvedStyleDefinition>& definitions,
                             const std::vector<float>& weights)
{
    if (definitions.size() <= 1)
        return;

    struct AggregateValue
    {
        double weightedSum = 0.0;
        double totalWeight = 0.0;
    };

    std::map<juce::String, AggregateValue> aggregates;

    for (size_t defIndex = 0; defIndex < definitions.size() && defIndex < weights.size(); ++defIndex)
    {
        const auto& hints = definitions[defIndex].styleHints;
        const auto weight = static_cast<double>(weights[defIndex]);
        for (int hintIndex = 0; hintIndex < hints.size(); ++hintIndex)
        {
            const auto& value = hints.getValueAt(hintIndex);
            if (!isNumericVar(value))
                continue;

            auto& aggregate = aggregates[hints.getName(hintIndex).toString()];
            aggregate.weightedSum += static_cast<double>(value) * weight;
            aggregate.totalWeight += weight;
        }
    }

    for (const auto& entry : aggregates)
    {
        if (entry.second.totalWeight <= 0.0)
            continue;

        definition.styleHints.set(juce::Identifier(entry.first),
                                  static_cast<float>(entry.second.weightedSum / entry.second.totalWeight));
    }
}

std::vector<StyleLabReferenceRecord> rankAndSelectReferences(std::vector<StyleLabReferenceRecord> matchingRecords)
{
    std::vector<RankedReferenceEntry> rankedEntries;
    rankedEntries.reserve(matchingRecords.size());

    for (auto& record : matchingRecords)
    {
        const auto sortTime = referenceSortTime(record);
        rankedEntries.push_back({ std::move(record), sortTime });
    }

    std::sort(rankedEntries.begin(), rankedEntries.end(), [](const RankedReferenceEntry& lhs, const RankedReferenceEntry& rhs)
    {
        if (lhs.record.referencePriority != rhs.record.referencePriority)
            return lhs.record.referencePriority > rhs.record.referencePriority;
        if (lhs.record.taggedNotes != rhs.record.taggedNotes)
            return lhs.record.taggedNotes > rhs.record.taggedNotes;
        if (lhs.record.totalNotes != rhs.record.totalNotes)
            return lhs.record.totalNotes > rhs.record.totalNotes;
        if (lhs.sortTime != rhs.sortTime)
            return lhs.sortTime > rhs.sortTime;
        return lhs.record.directory.getFullPathName() < rhs.record.directory.getFullPathName();
    });

    std::vector<StyleLabReferenceRecord> selected;
    const int configuredLimit = configuredMaxRankedReferences();
    const auto limit = configuredLimit > 0
        ? std::min<size_t>(rankedEntries.size(), static_cast<size_t>(configuredLimit))
        : rankedEntries.size();
    selected.reserve(limit);
    for (size_t index = 0; index < limit; ++index)
        selected.push_back(std::move(rankedEntries[index].record));
    return selected;
}

void applyRankedReferenceAggregation(ResolvedStyleDefinition& definition,
                                     GenreType genre,
                                     const std::vector<StyleLabReferenceRecord>& selectedRecords)
{
    if (selectedRecords.empty())
        return;

    definition.sourceReferenceCountUsed = static_cast<int>(selectedRecords.size());
    definition.primaryReferenceId = selectedRecords.front().directory.getFileName();
    const int configuredLimit = configuredMaxRankedReferences();
    if (selectedRecords.size() <= 1)
        definition.loadStrategy = "single-best-reference";
    else if (configuredLimit > 0)
        definition.loadStrategy = "ranked-reference-top-n";
    else
        definition.loadStrategy = "ranked-reference-all";
    definition.styleHints.set("stylelab.reference_top_n", configuredLimit);
    definition.styleHints.set("stylelab.reference_selected_count", static_cast<int>(selectedRecords.size()));

    const auto weights = referenceAggregationWeights(selectedRecords.size());
    std::vector<ResolvedStyleDefinition> resolvedReferences;
    resolvedReferences.reserve(selectedRecords.size());
    for (const auto& record : selectedRecords)
        resolvedReferences.push_back(StyleDefinitionLoader::fromReferenceRecord(genre, record));

    applyWeightedStyleHints(definition, resolvedReferences, weights);
    applyReferenceMetadataHints(definition, selectedRecords, weights);

    auto referenceHatCorpus = buildReferenceHatCorpus(selectedRecords);
    if (referenceHatCorpus.available)
        definition.referenceHatCorpus = std::move(referenceHatCorpus);
    else
        definition.referenceHatCorpus.reset();

    auto referenceKickCorpus = buildReferenceKickCorpus(selectedRecords);
    if (referenceKickCorpus.available)
        definition.referenceKickCorpus = std::move(referenceKickCorpus);
    else
        definition.referenceKickCorpus.reset();

    auto referenceHatSkeleton = extractReferenceHatSkeleton(selectedRecords.front());
    if (referenceHatSkeleton.available && !referenceHatSkeleton.barMaps.empty())
    {
        referenceHatSkeleton.sourceId = selectedRecords.front().directory.getFileName();
        definition.referenceHatSkeleton = std::move(referenceHatSkeleton);
    }
    else
    {
        definition.referenceHatSkeleton.reset();
    }

    if (genre == GenreType::Drill)
        applyDrillHatSummaryHints(definition, weightedDrillHatSummary(selectedRecords, weights));

}

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
        skeleton.sourceId = record.directory.getFileName();
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

BrooklynReferenceBarRole roleForReferenceBar(int totalBars, int barIndex)
{
    if (totalBars <= 1)
        return BrooklynReferenceBarRole::Ending;
    if (totalBars == 2)
        return barIndex == 0 ? BrooklynReferenceBarRole::Statement : BrooklynReferenceBarRole::Ending;
    if (totalBars == 3)
    {
        if (barIndex == 0)
            return BrooklynReferenceBarRole::Statement;
        if (barIndex == 1)
            return BrooklynReferenceBarRole::Response;
        return BrooklynReferenceBarRole::Ending;
    }

    switch (barIndex % 4)
    {
        case 0: return BrooklynReferenceBarRole::Statement;
        case 1: return BrooklynReferenceBarRole::Response;
        case 2: return BrooklynReferenceBarRole::Lift;
        default: return BrooklynReferenceBarRole::Ending;
    }
}

void normalizeBrooklynStepWeights(std::array<float, 16>& weights, float divisor)
{
    if (divisor <= 0.0f)
        return;

    for (auto& weight : weights)
        weight = clampUnit(weight / divisor);
}

BrooklynReferenceProfile buildBrooklynReferenceProfile(const std::vector<StyleLabReferenceRecord>& records,
                                                       const std::vector<float>& weights)
{
    BrooklynReferenceProfile profile;
    std::array<float, 4> roleWeightTotals { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> subLengthTotals { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> subLengthWeights { 0.0f, 0.0f, 0.0f, 0.0f };

    for (size_t recordIndex = 0; recordIndex < records.size() && recordIndex < weights.size(); ++recordIndex)
    {
        const auto& record = records[recordIndex];
        if (!record.metadataFile.existsAsFile())
            continue;

        const auto json = juce::JSON::parse(record.metadataFile.loadFileAsString());
        auto* rootObject = json.getDynamicObject();
        if (rootObject == nullptr)
            continue;

        auto* referenceProject = rootObject->getProperty("referenceProject").getDynamicObject();
        if (referenceProject == nullptr)
            continue;

        auto* tracks = referenceProject->getProperty("tracks").getArray();
        if (tracks == nullptr)
            continue;

        const auto kicks = collectBrooklynMetadataNotes(*tracks, { TrackType::Kick, TrackType::GhostKick });
        const auto hats = collectBrooklynMetadataNotes(*tracks, { TrackType::HiHat });
        const auto snares = collectBrooklynMetadataNotes(*tracks, { TrackType::Snare, TrackType::ClapGhostSnare });
        const auto openHats = collectBrooklynMetadataNotes(*tracks, { TrackType::OpenHat });
        const auto subs = collectBrooklynMetadataNotes(*tracks, { TrackType::Sub808 });
        const int totalBars = std::max(1, record.bars);
        const float recordWeight = weights[recordIndex];

        for (int bar = 0; bar < totalBars; ++bar)
        {
            const auto role = roleForReferenceBar(totalBars, bar);
            const auto roleIndex = static_cast<size_t>(role);
            auto& roleProfile = profile.roles[roleIndex];
            roleWeightTotals[roleIndex] += recordWeight;

            const int barStart = bar * HiResTiming::kTicksPerBar4_4;
            const int barEnd = barStart + HiResTiming::kTicksPerBar4_4;
            int kickCount = 0;
            int hatCount = 0;
            int openHatCount = 0;
            int subCount = 0;
            bool hadBurst = false;

            for (const auto& note : kicks)
            {
                if (note.tick < barStart || note.tick >= barEnd)
                    continue;

                const int localTick = note.tick - barStart;
                const int step16 = std::clamp(HiResTiming::quantizeTicks(localTick, HiResTiming::kTicks1_16) / HiResTiming::kTicks1_16, 0, 15);
                roleProfile.kickStepWeight[static_cast<size_t>(step16)] += recordWeight;
                ++kickCount;
                if (step16 >= 13)
                    roleProfile.phraseEdgeKickRate += recordWeight;
            }

            std::vector<int> barSnareTicks;
            for (const auto& note : snares)
                if (note.tick >= barStart && note.tick < barEnd)
                    barSnareTicks.push_back(note.tick);

            std::vector<int> barHatTicks;
            for (const auto& note : hats)
            {
                if (note.tick < barStart || note.tick >= barEnd)
                    continue;

                const int localTick = note.tick - barStart;
                const int step16 = std::clamp(HiResTiming::quantizeTicks(localTick, HiResTiming::kTicks1_16) / HiResTiming::kTicks1_16, 0, 15);
                roleProfile.hatStepWeight[static_cast<size_t>(step16)] += recordWeight;
                barHatTicks.push_back(note.tick);
                ++hatCount;

                for (const int snareTick : barSnareTicks)
                {
                    const int delta = snareTick - note.tick;
                    if (delta > 0 && delta <= HiResTiming::kTicks1_8)
                    {
                        roleProfile.preSnareAccentRate += recordWeight;
                        break;
                    }
                }
            }

            std::sort(barHatTicks.begin(), barHatTicks.end());
            for (size_t tickIndex = 1; tickIndex < barHatTicks.size(); ++tickIndex)
            {
                const int delta = barHatTicks[tickIndex] - barHatTicks[tickIndex - 1];
                if (delta > 0 && delta <= brooklynFastClusterGapThreshold())
                {
                    hadBurst = true;
                    break;
                }
            }

            for (const auto& note : openHats)
            {
                if (note.tick < barStart || note.tick >= barEnd)
                    continue;

                const int localTick = note.tick - barStart;
                const int step16 = std::clamp(HiResTiming::quantizeTicks(localTick, HiResTiming::kTicks1_16) / HiResTiming::kTicks1_16, 0, 15);
                roleProfile.openHatStepWeight[static_cast<size_t>(step16)] += recordWeight;
                ++openHatCount;
            }

            std::vector<int> barSubTicks;
            for (const auto& note : subs)
            {
                if (note.tick < barStart || note.tick >= barEnd)
                    continue;

                const int localTick = note.tick - barStart;
                const int step16 = std::clamp(HiResTiming::quantizeTicks(localTick, HiResTiming::kTicks1_16) / HiResTiming::kTicks1_16, 0, 15);
                roleProfile.subStartStepWeight[static_cast<size_t>(step16)] += recordWeight;
                barSubTicks.push_back(note.tick);
                ++subCount;
            }

            std::sort(barSubTicks.begin(), barSubTicks.end());
            for (size_t tickIndex = 0; tickIndex < barSubTicks.size(); ++tickIndex)
            {
                const int tick = barSubTicks[tickIndex];
                const int nextTick = tickIndex + 1 < barSubTicks.size() ? barSubTicks[tickIndex + 1] : barEnd;
                const int effectiveNextTick = std::clamp(nextTick, tick + HiResTiming::kTicks1_16, barEnd);
                const float lengthSteps = static_cast<float>(std::max(1, effectiveNextTick - tick)) / static_cast<float>(HiResTiming::kTicks1_16);
                subLengthTotals[roleIndex] += lengthSteps * recordWeight;
                subLengthWeights[roleIndex] += recordWeight;
            }

            roleProfile.avgKickHitsPerBar += static_cast<float>(kickCount) * recordWeight;
            roleProfile.avgHatHitsPerBar += static_cast<float>(hatCount) * recordWeight;
            roleProfile.avgOpenHatHitsPerBar += static_cast<float>(openHatCount) * recordWeight;
            roleProfile.avgSubStartsPerBar += static_cast<float>(subCount) * recordWeight;
            if (hadBurst)
                roleProfile.burstRate += recordWeight;
        }
    }

    for (size_t roleIndex = 0; roleIndex < profile.roles.size(); ++roleIndex)
    {
        auto& roleProfile = profile.roles[roleIndex];
        const float roleWeight = roleWeightTotals[roleIndex];
        if (roleWeight <= 0.0f)
            continue;

        normalizeBrooklynStepWeights(roleProfile.kickStepWeight, roleWeight);
        normalizeBrooklynStepWeights(roleProfile.hatStepWeight, roleWeight);
        normalizeBrooklynStepWeights(roleProfile.openHatStepWeight, roleWeight);
        normalizeBrooklynStepWeights(roleProfile.subStartStepWeight, roleWeight);
        roleProfile.avgKickHitsPerBar /= roleWeight;
        roleProfile.avgHatHitsPerBar /= roleWeight;
        roleProfile.avgOpenHatHitsPerBar /= roleWeight;
        roleProfile.avgSubStartsPerBar /= roleWeight;
        roleProfile.avgSubLengthSteps = subLengthWeights[roleIndex] > 0.0f
            ? subLengthTotals[roleIndex] / subLengthWeights[roleIndex]
            : 0.0f;
        roleProfile.phraseEdgeKickRate = clampUnit(roleProfile.phraseEdgeKickRate / roleWeight);
        roleProfile.preSnareAccentRate = clampUnit(roleProfile.preSnareAccentRate / roleWeight);
        roleProfile.burstRate = clampUnit(roleProfile.burstRate / roleWeight);
        profile.available = true;
    }

    profile.sourceCount = profile.available ? static_cast<int>(records.size()) : 0;
    return profile;
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

StyleLabReferenceLaneDiagnostics diagnoseLaneResolution(const std::vector<StyleLabReferenceRecord>& selectedRecords,
                                                        ReferenceLaneTarget target)
{
    StyleLabReferenceLaneDiagnostics diagnostics;
    diagnostics.requestedCount = static_cast<int>(selectedRecords.size());

    int parsingFailed = 0;
    int laneMappingEmpty = 0;
    int incompatibleSpan = 0;
    int filteredByDensity = 0;

    for (const auto& record : selectedRecords)
    {
        const auto rhythm = parseReferenceRhythm(record);
        if (!rhythm.has_value())
        {
            ++parsingFailed;
            continue;
        }

        if (rhythm->bars <= 0)
        {
            ++incompatibleSpan;
            continue;
        }

        if (target == ReferenceLaneTarget::Hat)
        {
            auto skeleton = extractReferenceHatSkeleton(record);
            if (skeleton.available && !skeleton.barMaps.empty())
            {
                ++diagnostics.resolvedCount;
                continue;
            }

            if (rhythm->hats.empty())
                ++laneMappingEmpty;
            else
                ++filteredByDensity;
            continue;
        }

        auto pattern = extractReferenceKickPattern(record);
        if (pattern.available && !pattern.barPatterns.empty())
        {
            ++diagnostics.resolvedCount;
            continue;
        }

        if (rhythm->kicks.empty())
            ++laneMappingEmpty;
        else
            ++filteredByDensity;
    }

    if (diagnostics.resolvedCount <= 0)
    {
        diagnostics.zeroReason = dominantZeroReason(parsingFailed,
                                                    laneMappingEmpty,
                                                    incompatibleSpan,
                                                    filteredByDensity);
        diagnostics.detail = buildLaneDiagnosticDetail(target,
                                                       diagnostics.requestedCount,
                                                       parsingFailed,
                                                       laneMappingEmpty,
                                                       incompatibleSpan,
                                                       filteredByDensity);
    }

    return diagnostics;
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
            const auto& style = getGenreStyleDefaults(genre, substyleIndex);
            const auto& hatLane = getLaneStyleDefaults(style, TrackType::HiHat);
            const auto& hatFxLane = getLaneStyleDefaults(style, TrackType::HatFX);
            const auto& clapLane = getLaneStyleDefaults(style, TrackType::ClapGhostSnare);
            const auto& subLane = getLaneStyleDefaults(style, TrackType::Sub808);

            definition.styleHints.set("drill.hat_motion",
                                      clampUnit(hatLane.densityBias * 0.40f
                                                + hatLane.rgVariationIntensity * 0.36f
                                                + hatFxLane.hatFxIntensity * 0.24f));
            definition.styleHints.set("drill.hat_roll_length",
                                      clampUnit(hatLane.rgVariationIntensity * 0.58f
                                                + hatFxLane.hatFxIntensity * 0.26f));
            definition.styleHints.set("drill.hat_density_variation",
                                      clampUnit(hatLane.rgVariationIntensity * 0.72f
                                                + hatLane.phraseEndingProbability * 0.14f));
            definition.styleHints.set("drill.hat_accent_pattern",
                                      clampUnit(hatFxLane.hatFxIntensity * 0.46f
                                                + clapLane.noteProbability * 0.28f
                                                + hatLane.phraseEndingProbability * 0.14f));
            definition.styleHints.set("drill.hat_burst",
                                      clampUnit(hatFxLane.hatFxIntensity * 0.52f
                                                + hatLane.rgVariationIntensity * 0.22f
                                                + hatLane.phraseEndingProbability * 0.12f));
            definition.styleHints.set("drill.hat_triplet",
                                      clampUnit(hatLane.rgVariationIntensity * 0.38f
                                                + hatLane.phraseEndingProbability * 0.34f
                                                + hatLane.densityBias * 0.12f));
            definition.styleHints.set("drill.gap_intent",
                                      clampUnit(0.22f
                                                + hatLane.phraseEndingProbability * 0.46f
                                                + (1.0f - clampUnit(style.densityDefault)) * 0.20f));
            definition.styleHints.set("drill.support_accent",
                                      clampUnit(0.20f
                                                + clapLane.noteProbability * 0.44f
                                                + hatFxLane.hatFxIntensity * 0.26f));
            definition.styleHints.set("drill.low_end_coupling",
                                      clampUnit(0.22f
                                                + subLane.sub808Activity * 0.62f
                                                + subLane.densityBias * 0.10f));
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
                                                                         juce::String* errorMessage,
                                                                         StyleLabReferenceDebugDiagnostics* referenceDiagnostics)
{
    StyleLabReferenceDebugDiagnostics diagnostics;
    juce::String status;
    const auto genreDirectory = rootDirectory.getChildFile(sanitizePathSegment(genreName));
    const auto genre = genreTypeFromDisplayName(genreName);
    const auto substyleNames = juce::StringArray { substyleName };
    const bool styleSwitchDisabled = !genreDirectory.getChildFile(sanitizePathSegment(substyleName)).isDirectory()
        && availableSubstyleDirectoryCount(genreDirectory) > 0;

    std::vector<juce::File> styleDirectories;
    for (const auto& compatibleSubstyleName : substyleNames)
    {
        const auto styleDirectory = genreDirectory.getChildFile(sanitizePathSegment(compatibleSubstyleName));
        if (styleDirectory.isDirectory())
            styleDirectories.push_back(styleDirectory);
    }

    if (styleDirectories.empty())
    {
        status = "No Style Lab reference directory for " + genreName + " / " + substyleName + ".";
        diagnostics.loadMessage = status;
        diagnostics.hat = zeroLaneDiagnostics(styleSwitchDisabled, 0, 0, status);
        diagnostics.kick = diagnostics.hat;
        if (referenceDiagnostics != nullptr)
            *referenceDiagnostics = diagnostics;
        if (errorMessage != nullptr)
            *errorMessage = status;
        return std::nullopt;
    }

    std::vector<juce::File> candidates;
    for (const auto& styleDirectory : styleDirectories)
    {
        for (const auto& entry : juce::RangedDirectoryIterator(styleDirectory, false, "*", juce::File::findDirectories))
        {
            const auto directory = entry.getFile();
            if (directory.getChildFile("metadata.json").existsAsFile())
                candidates.push_back(directory);
        }
    }
    diagnostics.candidateDirectoryCount = static_cast<int>(candidates.size());

    std::vector<StyleLabReferenceRecord> matchingRecords;
    int parseFailureCount = 0;
    for (const auto& directory : candidates)
    {
        juce::String parseError;
        const auto metadataJson = directory.getChildFile("metadata.json").loadFileAsString();
        const auto record = StyleLabReferenceBrowserService::parseMetadataJson(metadataJson, directory, &parseError);
        if (!record.has_value())
        {
            ++parseFailureCount;
            status = "Failed to parse Style Lab metadata at " + directory.getFullPathName() + ": " + parseError;
            continue;
        }

        const bool substyleMatch = record->substyle.equalsIgnoreCase(substyleName);
        if (!record->genre.equalsIgnoreCase(genreName) || !substyleMatch)
            continue;

        matchingRecords.push_back(*record);
    }
    diagnostics.parseFailureCount = parseFailureCount;
    diagnostics.matchingRecordCount = static_cast<int>(matchingRecords.size());

    if (!matchingRecords.empty())
    {
        const int candidateRecordCount = static_cast<int>(matchingRecords.size());
        const auto selectedRecords = rankAndSelectReferences(std::move(matchingRecords));
        diagnostics.selectedRecordCount = static_cast<int>(selectedRecords.size());
        diagnostics.hat = diagnoseLaneResolution(selectedRecords, ReferenceLaneTarget::Hat);
        diagnostics.kick = diagnoseLaneResolution(selectedRecords, ReferenceLaneTarget::Kick);
        auto definition = fromReferenceRecord(genre, selectedRecords.front());
        applyRankedReferenceAggregation(definition, genre, selectedRecords);
        definition.styleHints.set("stylelab.reference_candidate_count", candidateRecordCount);
        diagnostics.loadMessage = {};
        definition.referenceDebugDiagnostics = diagnostics;

        if (referenceDiagnostics != nullptr)
            *referenceDiagnostics = diagnostics;

        if (errorMessage != nullptr)
            *errorMessage = {};
        return definition;
    }

    diagnostics.selectedRecordCount = 0;
    diagnostics.loadMessage = status.isNotEmpty() ? status : "No valid Style Lab reference metadata found.";
    diagnostics.hat = zeroLaneDiagnostics(styleSwitchDisabled,
                                          diagnostics.candidateDirectoryCount,
                                          diagnostics.parseFailureCount,
                                          diagnostics.loadMessage);
    diagnostics.kick = diagnostics.hat;

    if (referenceDiagnostics != nullptr)
        *referenceDiagnostics = diagnostics;

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
    definition.sourceReferenceCountUsed = 0;
    definition.primaryReferenceId = {};
    definition.loadStrategy = "fallback";

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
            lane.laneParams.laneRole = defaultLaneRoleForTrackType(*runtimeLane.runtimeTrackType);
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
    definition.sourceReferenceCountUsed = 1;
    definition.primaryReferenceId = record.directory.getFileName();
    definition.loadStrategy = "single-best-reference";
    definition.lanes.clear();
    definition.lanes.reserve(record.runtimeLanes.size());

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

    const std::vector<StyleLabReferenceRecord> singleRecord { record };
    auto referenceHatSkeleton = extractReferenceHatSkeleton(record);
    if (referenceHatSkeleton.available && !referenceHatSkeleton.barMaps.empty())
    {
        referenceHatSkeleton.sourceId = record.directory.getFileName();
        definition.referenceHatSkeleton = std::move(referenceHatSkeleton);
    }

    auto referenceHatCorpus = buildReferenceHatCorpus(singleRecord);
    if (referenceHatCorpus.available)
        definition.referenceHatCorpus = std::move(referenceHatCorpus);

    auto referenceKickCorpus = buildReferenceKickCorpus(singleRecord);
    if (referenceKickCorpus.available)
        definition.referenceKickCorpus = std::move(referenceKickCorpus);

    if (genre == GenreType::Drill)
        applyDrillHatSummaryHints(definition, analyzeDrillHatReference(record));

    return definition;
}

juce::String StyleDefinitionLoader::genreDisplayName(GenreType genre)
{
    switch (genre)
    {
        case GenreType::Drill: return "Drill";
        case GenreType::Rap: return "Rap";
        case GenreType::Trap: return "Trap";
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
            case GenreType::Drill: return getDrillSubstyleNames();
            case GenreType::Rap: return getRapSubstyleNames();
            case GenreType::Trap: return getTrapSubstyleNames();
            case GenreType::BoomBap:
            default: return getBoomBapSubstyleNames();
        }
    }();

    if (names.isEmpty())
        return getGenreStyleDefaults(genre, 0).substyleName;

    return names[index];
}
} // namespace bbg