#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../Core/PatternProject.h"
#include "../Core/PatternProjectSerialization.h"
#include "../Core/RuntimeLaneLifecycle.h"
#include "../Core/TrackRegistry.h"
#include "../Engine/BoomBap/BoomBapKickGenerator.h"
#include "../Engine/Drill/Drill808Generator.h"
#include "../Engine/Drill/DrillHatGenerator.h"
#include "../Engine/Drill/DrillKickGenerator.h"
#include "../Engine/Rap/RapHatGenerator.h"
#include "../Engine/Rap/RapKickGenerator.h"
#include "../Engine/Trap/Trap808Generator.h"
#include "../Engine/Trap/TrapHatGenerator.h"
#include "../Engine/Trap/TrapKickGenerator.h"
#include "../Engine/StyleDefinitionLoader.h"
#include "../Engine/StyleInfluence.h"
#include "../Engine/StyleDefinitionResolver.h"
#include "../Engine/HiResTiming.h"
#include "../Services/StyleLabReferenceBrowserService.h"
#include "../Services/StyleLabReferenceService.h"

namespace bbg
{
namespace
{
[[noreturn]] void fail(const juce::String& message)
{
    throw std::runtime_error(message.toStdString());
}

void expect(bool condition, const juce::String& message)
{
    if (!condition)
        fail(message);
}

juce::var parseJson(const juce::String& jsonText)
{
    const auto parsed = juce::JSON::parse(jsonText);
    expect(!parsed.isVoid(), "Metadata JSON parse failed.");
    return parsed;
}

juce::DynamicObject* requireObject(const juce::var& value, const juce::String& label)
{
    auto* object = value.getDynamicObject();
    expect(object != nullptr, label + " must be an object.");
    return object;
}

juce::var propertyOf(const juce::var& value, const juce::String& propertyName, const juce::String& ownerLabel)
{
    auto* object = requireObject(value, ownerLabel);
    const auto property = object->getProperty(propertyName);
    expect(!property.isVoid(), ownerLabel + "." + propertyName + " is missing.");
    return property;
}

const juce::Array<juce::var>& requireArray(const juce::var& value, const juce::String& label)
{
    auto* array = value.getArray();
    expect(array != nullptr, label + " must be an array.");
    return *array;
}

juce::String requireString(const juce::var& value, const juce::String& label)
{
    const auto text = value.toString();
    expect(!text.isEmpty() || value.isString(), label + " must be a string.");
    return text;
}

int requireInt(const juce::var& value, const juce::String& label)
{
    expect(value.isInt() || value.isInt64() || value.isDouble() || value.isBool(), label + " must be numeric.");
    return static_cast<int>(value);
}

double requireDouble(const juce::var& value, const juce::String& label)
{
    expect(value.isInt() || value.isInt64() || value.isDouble() || value.isBool(), label + " must be numeric.");
    return static_cast<double>(value);
}

bool requireBool(const juce::var& value, const juce::String& label)
{
    expect(value.isBool() || value.isInt() || value.isInt64(), label + " must be bool-like.");
    return static_cast<bool>(value);
}

const juce::var* findObjectByStringProperty(const juce::Array<juce::var>& array,
                                            const juce::String& propertyName,
                                            const juce::String& expectedValue)
{
    for (const auto& item : array)
    {
        auto* object = item.getDynamicObject();
        if (object == nullptr)
            continue;

        if (object->getProperty(propertyName).toString() == expectedValue)
            return &item;
    }

    return nullptr;
}

TrackState* findTrackByType(PatternProject& project, TrackType type)
{
    for (auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

TrackState makeTrack(TrackType type)
{
    TrackState track;
    track.type = type;
    track.runtimeTrackType = type;
    track.enabled = true;
    return track;
}

int countNonAnchorDrillKicks(const TrackState& track)
{
    int count = 0;
    for (const auto& note : track.notes)
    {
        const int stepInBar = note.step % 16;
        if (stepInBar != 0 && stepInBar != 10)
            ++count;
    }
    return count;
}

int countOddStepNotes(const TrackState& track)
{
    int count = 0;
    for (const auto& note : track.notes)
        if ((note.step % 2) == 1)
            ++count;
    return count;
}

int countAbsoluteMicroOffset(const TrackState& track)
{
    int total = 0;
    for (const auto& note : track.notes)
        total += std::abs(note.microOffset);
    return total;
}

juce::String joinInts(const std::vector<int>& values)
{
    juce::StringArray parts;
    for (const int value : values)
        parts.add(juce::String(value));
    return parts.joinIntoString(",");
}

std::vector<int> sortedUniqueSteps(const TrackState& track)
{
    std::vector<int> steps;
    steps.reserve(track.notes.size());
    for (const auto& note : track.notes)
        steps.push_back(note.step);
    std::sort(steps.begin(), steps.end());
    steps.erase(std::unique(steps.begin(), steps.end()), steps.end());
    return steps;
}

std::vector<int> differenceSteps(const std::vector<int>& left, const std::vector<int>& right)
{
    std::vector<int> out;
    std::set_difference(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(out));
    return out;
}

std::vector<int> takeFirst(const std::vector<int>& values, int count)
{
    const auto endIt = values.begin() + std::min<int>(count, static_cast<int>(values.size()));
    return std::vector<int>(values.begin(), endIt);
}

const ReferenceHatBarSkeleton* findReferenceBar(const ReferenceHatSkeleton& skeleton, int barIndex)
{
    for (const auto& bar : skeleton.barMaps)
    {
        if (bar.barIndex == barIndex)
            return &bar;
    }

    return nullptr;
}

struct GeneratedHatReport
{
    juce::String label;
    juce::String referenceDirName;
    juce::String substyleName;
    juce::String skeletonBar0Backbone;
    juce::String skeletonBar0Motion;
    int rollClusterCount = 0;
    int tripletClusterCount = 0;
    int preSnareStepCountBar0 = 0;
    int generatedHatCount = 0;
    juce::String generatedStepsBar0;
    juce::String generatedStepsBar7;
    std::vector<int> generatedUniqueSteps;
};

struct GeneratedKickReport
{
    juce::String label;
    juce::String sourceLabel;
    std::vector<int> generatedUniqueSteps;
    juce::String generatedStepsBar0;
    juce::String generatedStepsBar7;
    int generatedKickCount = 0;
};

int drillSubstyleIndexForName(const juce::String& substyleName)
{
    for (int index = 0; index < 8; ++index)
    {
        if (StyleDefinitionLoader::substyleNameFor(GenreType::Drill, index) == substyleName)
            return index;
    }

    fail("Unknown Drill substyle in validation: " + substyleName);
}

juce::File requireReferenceMetadataFile(const juce::File& substyleRoot, const juce::String& directoryName)
{
    const auto directory = substyleRoot.getChildFile(directoryName);
    expect(directory.isDirectory(), "Reference directory is missing: " + directoryName);

    const auto metadataFile = directory.getChildFile("metadata.json");
    expect(metadataFile.existsAsFile(), "Reference metadata file is missing: " + directoryName);
    return metadataFile;
}

ResolvedStyleDefinition loadDrillReferenceDefinition(const juce::File& metadataFile)
{
    juce::String parseError;
    const auto metadataText = metadataFile.loadFileAsString();
    const auto parsed = StyleLabReferenceBrowserService::parseMetadataJson(metadataText,
                                                                           metadataFile.getParentDirectory(),
                                                                           &parseError);
    expect(parsed.has_value(), "Failed to parse drill metadata: " + parseError);
    return StyleDefinitionLoader::fromReferenceRecord(GenreType::Drill, *parsed);
}

std::vector<int> collectReferenceTrackStartTicks(const juce::File& metadataFile, TrackType trackType)
{
    std::vector<int> ticks;
    const auto root = parseJson(metadataFile.loadFileAsString());
    const auto referenceProject = propertyOf(root, "referenceProject", "metadata");
    const auto& tracks = requireArray(propertyOf(referenceProject, "tracks", "referenceProject"),
                                      "referenceProject.tracks");
    const auto trackToken = juce::String(toString(trackType));

    for (const auto& trackVar : tracks)
    {
        auto* trackObject = trackVar.getDynamicObject();
        if (trackObject == nullptr)
            continue;

        const auto runtimeTrackType = trackObject->getProperty("runtimeTrackType").toString().trim();
        const auto serializedTrackType = trackObject->getProperty("trackType").toString().trim();
        if (runtimeTrackType != trackToken && serializedTrackType != trackToken)
            continue;

        const auto laneParams = trackObject->getProperty("laneParams");
        const auto notesValue = propertyOf(laneParams, "notes", "laneParams");
        const auto& notes = requireArray(notesValue, "laneParams.notes");
        for (const auto& noteVar : notes)
        {
            const auto startTick = requireInt(propertyOf(noteVar, "startTick", "note"), "note.startTick");
            ticks.push_back(startTick);
        }
    }

    std::sort(ticks.begin(), ticks.end());
    ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
    return ticks;
}

struct TripletAdjacencyInspection
{
    int fastAdjacency = 0;
    int tripletAdjacency = 0;
    std::vector<int> fastDeltas;
};

TripletAdjacencyInspection inspectTripletAdjacency(const juce::File& metadataFile)
{
    TripletAdjacencyInspection inspection;
    const auto hatTicks = collectReferenceTrackStartTicks(metadataFile, TrackType::HiHat);
    const int threshold = std::max(HiResTiming::kTicks1_32, HiResTiming::kTicks1_24) + 8;

    for (size_t index = 1; index < hatTicks.size(); ++index)
    {
        const int delta = hatTicks[index] - hatTicks[index - 1];
        if (delta <= 0 || delta > threshold)
            continue;

        ++inspection.fastAdjacency;
        inspection.fastDeltas.push_back(delta);

        if (std::abs(delta - HiResTiming::kTicks1_24) + 4 < std::abs(delta - HiResTiming::kTicks1_32))
            ++inspection.tripletAdjacency;
    }

    return inspection;
}

void printDrillReferenceValidationResult(const juce::String& title,
                                        int seed,
                                        const GeneratedHatReport& reportA,
                                        const GeneratedHatReport& reportB,
                                        const std::vector<int>& onlyA,
                                        const std::vector<int>& onlyB)
{
    std::cout << title << " seed=" << seed << std::endl;
    std::cout << "  substyle=" << reportA.substyleName << std::endl;
    std::cout << "  A dir=" << reportA.referenceDirName << std::endl;
    std::cout << "    skeleton bar0 backbone16=" << reportA.skeletonBar0Backbone << std::endl;
    std::cout << "    skeleton bar0 motion32=" << reportA.skeletonBar0Motion << std::endl;
    std::cout << "    rollClusters=" << reportA.rollClusterCount
              << " tripletClusters=" << reportA.tripletClusterCount
              << " preSnareBar0=" << reportA.preSnareStepCountBar0 << std::endl;
    std::cout << "    generated hats=" << reportA.generatedHatCount
              << " bar0 steps=" << reportA.generatedStepsBar0
              << " bar7 steps=" << reportA.generatedStepsBar7 << std::endl;
    std::cout << "  B dir=" << reportB.referenceDirName << std::endl;
    std::cout << "    skeleton bar0 backbone16=" << reportB.skeletonBar0Backbone << std::endl;
    std::cout << "    skeleton bar0 motion32=" << reportB.skeletonBar0Motion << std::endl;
    std::cout << "    rollClusters=" << reportB.rollClusterCount
              << " tripletClusters=" << reportB.tripletClusterCount
              << " preSnareBar0=" << reportB.preSnareStepCountBar0 << std::endl;
    std::cout << "    generated hats=" << reportB.generatedHatCount
              << " bar0 steps=" << reportB.generatedStepsBar0
              << " bar7 steps=" << reportB.generatedStepsBar7 << std::endl;
    std::cout << "  only-in-A steps=" << joinInts(onlyA) << std::endl;
    std::cout << "  only-in-B steps=" << joinInts(onlyB) << std::endl;
}

GeneratedHatReport generateDrillHatReportForReference(const juce::File& metadataFile,
                                                      const juce::String& label,
                                                      int seed)
{
    juce::String parseError;
    const auto metadataText = metadataFile.loadFileAsString();
    const auto parsed = StyleLabReferenceBrowserService::parseMetadataJson(metadataText,
                                                                           metadataFile.getParentDirectory(),
                                                                           &parseError);
    expect(parsed.has_value(), "Failed to parse drill metadata for validation: " + parseError);

    const auto definition = StyleDefinitionLoader::fromReferenceRecord(GenreType::Drill, *parsed);
    expect(definition.referenceHatSkeleton.has_value() && definition.referenceHatSkeleton->available,
           "Validation reference must expose ReferenceHatSkeleton.");

    const int drillSubstyleIndex = drillSubstyleIndexForName(definition.substyleName);

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.drillSubstyle = drillSubstyleIndex;
    project.params.bars = 8;
    project.params.seed = seed;
    project.generationCounter = 0;

    juce::String applyError;
    expect(DrillStyleInfluence::applyResolvedStyle(definition, project, &applyError),
           "Failed to apply drill reference during validation: " + applyError);

    GeneratorParams params;
    params.bars = 8;
    params.bpm = 142.0f;
    params.densityAmount = 0.64f;
    params.keyRoot = 0;
    params.scaleMode = 0;

    const auto& style = getDrillProfile(drillSubstyleIndex);
    const std::vector<DrillPhraseRole> phrase {
        DrillPhraseRole::Statement,
        DrillPhraseRole::Response,
        DrillPhraseRole::Statement,
        DrillPhraseRole::Tension,
        DrillPhraseRole::Statement,
        DrillPhraseRole::Response,
        DrillPhraseRole::Release,
        DrillPhraseRole::Ending
    };

    TrackState hatTrack = makeTrack(TrackType::HiHat);
    TrackState kickTrack = makeTrack(TrackType::Kick);
    TrackState snareTrack = makeTrack(TrackType::Snare);
    TrackState clapTrack = makeTrack(TrackType::ClapGhostSnare);
    kickTrack.notes = {
        { 36, 0, 1, 120, 0, false },
        { 36, 6, 1, 112, 0, false },
        { 36, 12, 1, 124, 0, false },
        { 36, 16, 1, 120, 0, false },
        { 36, 22, 1, 110, 0, false },
        { 36, 28, 1, 124, 0, false },
        { 36, 32, 1, 120, 0, false },
        { 36, 38, 1, 112, 0, false },
        { 36, 44, 1, 124, 0, false },
        { 36, 48, 1, 120, 0, false },
        { 36, 54, 1, 112, 0, false },
        { 36, 60, 1, 124, 0, false },
        { 36, 64, 1, 120, 0, false },
        { 36, 70, 1, 112, 0, false },
        { 36, 76, 1, 124, 0, false },
        { 36, 80, 1, 120, 0, false },
        { 36, 86, 1, 110, 0, false },
        { 36, 92, 1, 124, 0, false },
        { 36, 96, 1, 120, 0, false },
        { 36, 102, 1, 112, 0, false },
        { 36, 108, 1, 124, 0, false },
        { 36, 112, 1, 120, 0, false },
        { 36, 118, 1, 112, 0, false },
        { 36, 124, 1, 127, 0, false }
    };
    snareTrack.notes = {
        { 38, 8, 1, 110, 0, false },
        { 38, 12, 1, 112, 0, false },
        { 38, 24, 1, 110, 0, false },
        { 38, 28, 1, 112, 0, false },
        { 38, 40, 1, 110, 0, false },
        { 38, 44, 1, 112, 0, false },
        { 38, 56, 1, 110, 0, false },
        { 38, 60, 1, 112, 0, false },
        { 38, 72, 1, 110, 0, false },
        { 38, 76, 1, 112, 0, false },
        { 38, 88, 1, 110, 0, false },
        { 38, 92, 1, 112, 0, false },
        { 38, 104, 1, 110, 0, false },
        { 38, 108, 1, 112, 0, false },
        { 38, 120, 1, 110, 0, false },
        { 38, 124, 1, 118, 0, false }
    };

    DrillHatGenerator generator;
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
    generator.generate(hatTrack,
                       params,
                       style,
                       project.styleInfluence,
                       phrase,
                       nullptr,
                       &snareTrack,
                       &clapTrack,
                       &kickTrack,
                       rng);

    GeneratedHatReport report;
    report.label = label;
    report.referenceDirName = metadataFile.getParentDirectory().getFileName();
    report.substyleName = definition.substyleName;

    const auto& skeleton = *definition.referenceHatSkeleton;
    if (const auto* bar0 = findReferenceBar(skeleton, 0))
    {
        report.skeletonBar0Backbone = joinInts(bar0->backboneSteps16);
        report.skeletonBar0Motion = joinInts(bar0->motionSteps32);
        report.preSnareStepCountBar0 = static_cast<int>(bar0->preSnareZoneSteps32.size());
    }

    report.rollClusterCount = static_cast<int>(skeleton.rollClusters.size());
    report.tripletClusterCount = static_cast<int>(skeleton.tripletClusters.size());
    report.generatedHatCount = static_cast<int>(hatTrack.notes.size());

    const auto uniqueSteps = sortedUniqueSteps(hatTrack);
    report.generatedUniqueSteps = uniqueSteps;

    std::vector<int> bar0Steps;
    std::vector<int> bar7Steps;
    for (const auto& note : hatTrack.notes)
    {
        if (note.step >= 0 && note.step < 16)
            bar0Steps.push_back(note.step);
        if (note.step >= 112 && note.step < 128)
            bar7Steps.push_back(note.step);
    }

    std::sort(bar0Steps.begin(), bar0Steps.end());
    bar0Steps.erase(std::unique(bar0Steps.begin(), bar0Steps.end()), bar0Steps.end());
    std::sort(bar7Steps.begin(), bar7Steps.end());
    bar7Steps.erase(std::unique(bar7Steps.begin(), bar7Steps.end()), bar7Steps.end());
    report.generatedStepsBar0 = joinInts(bar0Steps);
    report.generatedStepsBar7 = joinInts(bar7Steps);
    return report;
}

GeneratedKickReport generateUKDrillKickReport(const ResolvedStyleDefinition& definition,
                                              const juce::String& sourceLabel,
                                              int seed)
{
    const int drillSubstyleIndex = drillSubstyleIndexForName(definition.substyleName);

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.drillSubstyle = drillSubstyleIndex;
    project.params.bars = 8;
    project.params.seed = seed;

    juce::String applyError;
    expect(DrillStyleInfluence::applyResolvedStyle(definition, project, &applyError),
           "Failed to apply UKDrill definition during kick validation: " + applyError);

    GeneratorParams params;
    params.bars = 8;
    params.bpm = 142.0f;
    params.densityAmount = 0.64f;
    params.keyRoot = 0;
    params.scaleMode = 0;

    const auto& style = getDrillProfile(drillSubstyleIndex);
    const std::vector<DrillPhraseRole> phrase {
        DrillPhraseRole::Statement,
        DrillPhraseRole::Response,
        DrillPhraseRole::Statement,
        DrillPhraseRole::Tension,
        DrillPhraseRole::Statement,
        DrillPhraseRole::Response,
        DrillPhraseRole::Release,
        DrillPhraseRole::Ending
    };

    TrackState kickTrack = makeTrack(TrackType::Kick);
    DrillKickGenerator generator;
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
    generator.generate(kickTrack, params, style, project.styleInfluence, phrase, nullptr, rng);

    GeneratedKickReport report;
    report.label = definition.substyleName;
    report.sourceLabel = sourceLabel;
    report.generatedKickCount = static_cast<int>(kickTrack.notes.size());
    report.generatedUniqueSteps = sortedUniqueSteps(kickTrack);

    std::vector<int> bar0Steps;
    std::vector<int> bar7Steps;
    for (const auto& note : kickTrack.notes)
    {
        if (note.step >= 0 && note.step < 16)
            bar0Steps.push_back(note.step);
        if (note.step >= 112 && note.step < 128)
            bar7Steps.push_back(note.step);
    }
    std::sort(bar0Steps.begin(), bar0Steps.end());
    bar0Steps.erase(std::unique(bar0Steps.begin(), bar0Steps.end()), bar0Steps.end());
    std::sort(bar7Steps.begin(), bar7Steps.end());
    bar7Steps.erase(std::unique(bar7Steps.begin(), bar7Steps.end()), bar7Steps.end());
    report.generatedStepsBar0 = joinInts(bar0Steps);
    report.generatedStepsBar7 = joinInts(bar7Steps);
    return report;
}

int findTrackIndexByType(const PatternProject& project, TrackType type)
{
    for (int index = 0; index < static_cast<int>(project.tracks.size()); ++index)
    {
        if (project.tracks[static_cast<size_t>(index)].type == type)
            return index;
    }

    return -1;
}

const RuntimeLaneDefinition* findLaneByType(const PatternProject& project, TrackType type)
{
    return findRuntimeLaneForTrack(project.runtimeLaneProfile, type);
}

void moveLaneToFront(PatternProject& project, const RuntimeLaneId& laneId)
{
    auto it = std::find(project.runtimeLaneOrder.begin(), project.runtimeLaneOrder.end(), laneId);
    expect(it != project.runtimeLaneOrder.end(), "Lane missing from runtimeLaneOrder: " + laneId);
    project.runtimeLaneOrder.erase(it);
    project.runtimeLaneOrder.insert(project.runtimeLaneOrder.begin(), laneId);
}

PatternProject roundTrip(const PatternProject& project)
{
    juce::ValueTree root("ROOT");
    root.addChild(PatternProjectSerialization::serialize(project), -1, nullptr);

    PatternProject restored;
    expect(PatternProjectSerialization::deserialize(root, restored), "PatternProject deserialize failed in round-trip.");
    return restored;
}

void checkOrderAlignment(const juce::Array<juce::var>& runtimeLaneOrder, const juce::Array<juce::var>& laneLayout)
{
    expect(runtimeLaneOrder.size() == laneLayout.size(), "runtimeLaneOrder and laneLayout size mismatch.");
    for (int index = 0; index < runtimeLaneOrder.size(); ++index)
    {
        const auto laneId = runtimeLaneOrder.getReference(index).toString();
        const auto& laneEntry = laneLayout.getReference(index);
        expect(propertyOf(laneEntry, "laneId", "laneLayout[]").toString() == laneId,
               "laneLayout order must match runtimeLaneOrder.");
        expect(requireInt(propertyOf(laneEntry, "orderIndex", "laneLayout[]"), "laneLayout[].orderIndex") == index,
               "laneLayout.orderIndex mismatch.");
    }
}

void checkBackedLaneParams(const juce::var& metadataRoot, const PatternProject& project)
{
    const auto& rootRuntimeLaneOrder = requireArray(propertyOf(metadataRoot, "runtimeLaneOrder", "metadataRoot"), "metadataRoot.runtimeLaneOrder");
    const auto& rootLaneLayout = requireArray(propertyOf(metadataRoot, "laneLayout", "metadataRoot"), "metadataRoot.laneLayout");
    const auto referenceProject = propertyOf(metadataRoot, "referenceProject", "metadataRoot");
    const auto& projectTracks = requireArray(propertyOf(referenceProject, "tracks", "referenceProject"), "referenceProject.tracks");

    expect(requireString(propertyOf(metadataRoot, "laneLayoutSource", "metadataRoot"), "metadataRoot.laneLayoutSource") == "PatternProject.runtime",
           "laneLayoutSource must be PatternProject.runtime.");
    expect(rootRuntimeLaneOrder.size() == static_cast<int>(project.runtimeLaneOrder.size()), "runtimeLaneOrder array size mismatch.");
    checkOrderAlignment(rootRuntimeLaneOrder, rootLaneLayout);

    const auto* kickLane = findLaneByType(project, TrackType::Kick);
    expect(kickLane != nullptr, "Kick lane missing from runtime profile.");
    const auto* kickLayout = findObjectByStringProperty(rootLaneLayout, "laneId", kickLane->laneId);
    expect(kickLayout != nullptr, "Kick runtime lane missing from laneLayout.");
    expect(requireString(propertyOf(*kickLayout, "laneName", "kickLayout"), "kickLayout.laneName") == kickLane->laneName,
           "Kick laneName mismatch.");
    expect(requireString(propertyOf(*kickLayout, "bindingKind", "kickLayout"), "kickLayout.bindingKind") == "backed",
           "Backed lane must export bindingKind=backed.");
    expect(requireBool(propertyOf(*kickLayout, "hasBackingTrack", "kickLayout"), "kickLayout.hasBackingTrack"),
           "Backed lane must report hasBackingTrack=true.");
    expect(requireBool(propertyOf(*kickLayout, "laneParamsAvailable", "kickLayout"), "kickLayout.laneParamsAvailable"),
           "Backed lane must report laneParamsAvailable=true.");

    const auto laneParams = propertyOf(*kickLayout, "laneParams", "kickLayout");
    expect(requireDouble(propertyOf(laneParams, "laneVolume", "kickLayout.laneParams"), "kickLayout.laneParams.laneVolume") > 1.19,
           "Backed lane params must include laneVolume.");
        expect(requireDouble(propertyOf(propertyOf(laneParams, "sound", "kickLayout.laneParams"), "pan", "kickLayout.laneParams.sound"),
                    "kickLayout.laneParams.sound.pan") < -0.2,
            "Sound layer pan must be serialized into laneParams.");
    expect(requireInt(propertyOf(laneParams, "noteCount", "kickLayout.laneParams"), "kickLayout.laneParams.noteCount") == 1,
           "Kick noteCount contract mismatch.");
    expect(requireInt(propertyOf(laneParams, "taggedNoteCount", "kickLayout.laneParams"), "kickLayout.laneParams.taggedNoteCount") == 1,
           "Kick taggedNoteCount contract mismatch.");

    const auto* kickTrack = findObjectByStringProperty(projectTracks, "laneId", kickLane->laneId);
    expect(kickTrack != nullptr, "Kick track missing from referenceProject.tracks.");
    expect(requireString(propertyOf(*kickTrack, "laneName", "referenceProject.tracks[]"), "referenceProject.tracks[].laneName") == kickLane->laneName,
           "Track export must carry laneName.");

    expect(requireString(propertyOf(referenceProject, "selectedTrackLaneId", "referenceProject"), "referenceProject.selectedTrackLaneId") == kickLane->laneId,
           "selectedTrackLaneId contract mismatch.");

    const auto* subLane = findLaneByType(project, TrackType::Sub808);
    expect(subLane != nullptr, "Sub808 lane missing from runtime profile.");
    expect(requireString(propertyOf(referenceProject, "soundModuleTrackLaneId", "referenceProject"), "referenceProject.soundModuleTrackLaneId") == subLane->laneId,
           "soundModuleTrackLaneId contract mismatch.");
}

void runDefaultBackedLaneContract()
{
    auto project = createDefaultProject();
    project.params.bpm = 92.0f;
    project.params.bars = 4;

    auto* kickTrack = findTrackByType(project, TrackType::Kick);
    expect(kickTrack != nullptr, "Kick track missing.");
    kickTrack->laneVolume = 1.2f;
    kickTrack->selectedSampleIndex = 3;
    kickTrack->selectedSampleName = "ContractKick";
    kickTrack->sound.pan = -0.25f;
    kickTrack->sound.width = 1.4f;
    kickTrack->sound.drive = 0.35f;
    kickTrack->notes = { NoteEvent { 36, 0, 1, 112, 12, false, "anchor" } };

    const int kickIndex = findTrackIndexByType(project, TrackType::Kick);
    const int subIndex = findTrackIndexByType(project, TrackType::Sub808);
    expect(kickIndex >= 0 && subIndex >= 0, "Failed to resolve default selected/sound targets.");
    project.selectedTrackIndex = kickIndex;
    project.soundModuleTrackIndex = subIndex;

    PatternProjectSerialization::validate(project);
    const auto state = StyleLabReferenceService::createDefaultState(project, "Boom Bap", "Classic", 4, 92);
    const auto metadataJson = StyleLabReferenceService::buildReferenceMetadataJson(project, state);
    const auto metadata = parseJson(metadataJson);
    checkBackedLaneParams(metadata, project);

    juce::String parseError;
    const auto parsedRecord = StyleLabReferenceBrowserService::parseMetadataJson(metadataJson, juce::File("C:/contract/default"), &parseError);
    expect(parsedRecord.has_value(), "Browser parser failed for backed-lane metadata: " + parseError);
    expect(parsedRecord->runtimeLanes.size() == project.runtimeLaneOrder.size(), "Browser parser lane count mismatch for backed-lane metadata.");
}

void runLifecycleCompositeContract()
{
    auto project = createDefaultProject();

    const auto* kickLaneBefore = findLaneByType(project, TrackType::Kick);
    const auto* subLaneBefore = findLaneByType(project, TrackType::Sub808);
    const auto* ghostKickLaneBefore = findLaneByType(project, TrackType::GhostKick);
    expect(kickLaneBefore != nullptr && subLaneBefore != nullptr && ghostKickLaneBefore != nullptr,
           "Required default lanes are missing.");

    const auto kickLaneId = kickLaneBefore->laneId;
    const auto subLaneId = subLaneBefore->laneId;
    const auto deletedLaneId = ghostKickLaneBefore->laneId;

    expect(RuntimeLaneLifecycle::renameLane(project, kickLaneId, "Main Kick"), "Kick rename failed.");

    auto customLane = RuntimeLaneLifecycle::createCustomLaneDefinition(project, "Texture Vox");
    const auto customLaneId = customLane.laneId;
    expect(RuntimeLaneLifecycle::addLane(project, std::move(customLane), 2), "Custom lane add failed.");
    expect(RuntimeLaneLifecycle::deleteLane(project, deletedLaneId), "GhostKick delete failed.");

    moveLaneToFront(project, subLaneId);
    auto customOrderIt = std::find(project.runtimeLaneOrder.begin(), project.runtimeLaneOrder.end(), customLaneId);
    expect(customOrderIt != project.runtimeLaneOrder.end(), "Custom lane missing from runtimeLaneOrder.");
    project.runtimeLaneOrder.erase(customOrderIt);
    project.runtimeLaneOrder.insert(project.runtimeLaneOrder.begin() + 1, customLaneId);

    auto* kickTrack = findTrackByType(project, TrackType::Kick);
    expect(kickTrack != nullptr, "Kick track missing after lifecycle mutations.");
    kickTrack->laneVolume = 0.77f;
    kickTrack->selectedSampleName = "MainKickContract";
    kickTrack->sound.pan = 0.18f;
    kickTrack->notes = { NoteEvent { 36, 4, 1, 104, 0, false, "impact" } };

    PatternProjectSerialization::validate(project);
    const auto restoredProject = roundTrip(project);
    const auto state = StyleLabReferenceService::createDefaultState(restoredProject, "Boom Bap", "Classic", 4, 92);

    juce::String conflictMessage;
    const auto metadataJson = StyleLabReferenceService::buildReferenceMetadataJson(restoredProject, state, &conflictMessage);
    const auto metadata = parseJson(metadataJson);
    const auto& runtimeLaneOrder = requireArray(propertyOf(metadata, "runtimeLaneOrder", "metadataRoot"), "metadataRoot.runtimeLaneOrder");
    const auto& laneLayout = requireArray(propertyOf(metadata, "laneLayout", "metadataRoot"), "metadataRoot.laneLayout");
    const auto referenceProject = propertyOf(metadata, "referenceProject", "metadataRoot");
    const auto& tracks = requireArray(propertyOf(referenceProject, "tracks", "referenceProject"), "referenceProject.tracks");

    checkOrderAlignment(runtimeLaneOrder, laneLayout);
    expect(runtimeLaneOrder.getReference(0).toString() == subLaneId, "Reordered Sub808 lane must be first in runtimeLaneOrder.");
    expect(runtimeLaneOrder.getReference(1).toString() == customLaneId, "Custom lane must be second in runtimeLaneOrder.");

    const auto* renamedKickLane = findObjectByStringProperty(laneLayout, "laneId", kickLaneId);
    expect(renamedKickLane != nullptr, "Renamed Kick lane missing from laneLayout.");
    expect(requireString(propertyOf(*renamedKickLane, "laneName", "renamedKickLane"), "renamedKickLane.laneName") == "Main Kick",
           "Renamed laneName not exported.");
    expect(requireString(propertyOf(*renamedKickLane, "bindingKind", "renamedKickLane"), "renamedKickLane.bindingKind") == "backed",
           "Renamed backed lane bindingKind mismatch.");

    const auto* customLaneEntry = findObjectByStringProperty(laneLayout, "laneId", customLaneId);
    expect(customLaneEntry != nullptr, "Custom lane missing from laneLayout.");
    expect(requireString(propertyOf(*customLaneEntry, "laneName", "customLane"), "customLane.laneName") == "Texture Vox",
           "Custom laneName mismatch.");
    expect(requireString(propertyOf(*customLaneEntry, "bindingKind", "customLane"), "customLane.bindingKind") == "unbacked",
           "Custom lane must export bindingKind=unbacked.");
    expect(!requireBool(propertyOf(*customLaneEntry, "hasBackingTrack", "customLane"), "customLane.hasBackingTrack"),
           "Custom lane must export hasBackingTrack=false.");
    expect(!requireBool(propertyOf(*customLaneEntry, "laneParamsAvailable", "customLane"), "customLane.laneParamsAvailable"),
           "Custom lane must export laneParamsAvailable=false.");
    expect(propertyOf(*customLaneEntry, "runtimeTrackType", "customLane").toString().isEmpty(),
           "Custom lane runtimeTrackType must be empty.");

    expect(findObjectByStringProperty(laneLayout, "laneId", deletedLaneId) == nullptr,
           "Deleted backed lane must not reappear in laneLayout after round-trip.");
    expect(findObjectByStringProperty(tracks, "laneId", deletedLaneId) == nullptr,
           "Deleted backed lane must not reappear in referenceProject.tracks after round-trip.");

    expect(requireInt(propertyOf(referenceProject, "unbackedLaneCount", "referenceProject"), "referenceProject.unbackedLaneCount") == 1,
           "unbackedLaneCount contract mismatch.");
    expect(requireInt(propertyOf(referenceProject, "backedLaneCount", "referenceProject"), "referenceProject.backedLaneCount")
               == static_cast<int>(restoredProject.tracks.size()),
           "backedLaneCount contract mismatch.");
    expect(conflictMessage.contains("Custom runtime lanes are exported as unbacked layout entries"),
           "Custom lane export must emit the unbacked runtime note.");

    juce::String parseError;
    const auto parsedRecord = StyleLabReferenceBrowserService::parseMetadataJson(metadataJson, juce::File("C:/contract/lifecycle"), &parseError);
    expect(parsedRecord.has_value(), "Browser parser failed for lifecycle metadata: " + parseError);
    expect(parsedRecord->runtimeLanes.size() == restoredProject.runtimeLaneProfile.lanes.size(), "Browser parser runtime lane count mismatch after lifecycle mutations.");
    expect(parsedRecord->runtimeLanes[0].laneId == subLaneId, "Browser parser must preserve runtimeLaneOrder primary ordering.");
    expect(parsedRecord->runtimeLanes[1].laneId == customLaneId, "Browser parser must place custom lane according to runtimeLaneOrder.");
    expect(parsedRecord->runtimeLanes[1].bindingKind == "unbacked", "Browser parser must preserve unbacked binding kind.");
    expect(!parsedRecord->runtimeLanes[1].laneParamsAvailable, "Browser parser must not synthesize lane params for unbacked lanes.");

        auto applyTarget = createDefaultProject();
        auto* applyKickTrack = findTrackByType(applyTarget, TrackType::Kick);
        auto* applyGhostKickTrack = findTrackByType(applyTarget, TrackType::GhostKick);
        expect(applyKickTrack != nullptr && applyGhostKickTrack != nullptr, "Apply target missing default backed tracks.");
        const auto originalApplyOrder = applyTarget.runtimeLaneOrder;
        const auto originalApplyLaneCount = applyTarget.runtimeLaneProfile.lanes.size();
        const auto originalApplyTrackCount = applyTarget.tracks.size();
        const auto* applyGhostKickLane = findLaneByType(applyTarget, TrackType::GhostKick);
        expect(applyGhostKickLane != nullptr, "Apply target GhostKick lane missing from runtime profile.");
        const auto applyGhostKickLaneId = applyGhostKickLane->laneId;
        applyKickTrack->laneVolume = 0.31f;
        applyKickTrack->selectedSampleName = "PreApplyKick";
        applyKickTrack->notes = {
         NoteEvent { 36, 0, 1, 100, 0, false, "pre_a" },
         NoteEvent { 36, 8, 1, 96, 0, false, "pre_b" }
        };
        applyGhostKickTrack->notes = { NoteEvent { 35, 4, 1, 72, 0, true, "ghost" } };

        juce::String applyError;
        expect(StyleLabReferenceBrowserService::applyReferenceToProject(*parsedRecord, applyTarget, &applyError),
            "Browser apply failed for lifecycle metadata: " + applyError);

        expect(applyTarget.runtimeLaneOrder == originalApplyOrder,
            "Apply target must preserve its existing lane order instead of adopting reference layout order.");
        expect(applyTarget.runtimeLaneProfile.lanes.size() == originalApplyLaneCount,
            "Apply target must preserve its existing runtime lane count.");
        expect(applyTarget.tracks.size() == originalApplyTrackCount,
            "Apply target must preserve its existing backed track count.");

        const auto* appliedCustomLane = findRuntimeLaneById(applyTarget.runtimeLaneProfile, customLaneId);
        expect(appliedCustomLane == nullptr,
            "Reference apply must not add unbacked custom lanes into an existing project layout.");

        const auto* appliedKickLane = findRuntimeLaneById(applyTarget.runtimeLaneProfile, kickLaneId);
        expect(appliedKickLane != nullptr, "Renamed kick lane missing from applied runtime profile.");
        expect(appliedKickLane->laneName != "Main Kick",
            "Reference apply must not rename existing runtime lanes when preserving project layout.");

        expect(findRuntimeLaneById(applyTarget.runtimeLaneProfile, applyGhostKickLaneId) != nullptr,
            "Reference apply must preserve existing lanes even if the reference deleted them.");
        expect(findTrackByType(applyTarget, TrackType::GhostKick) != nullptr,
            "Reference apply must preserve existing backing tracks even if the reference lacks them.");

        applyKickTrack = findTrackByType(applyTarget, TrackType::Kick);
        expect(applyKickTrack != nullptr, "Kick track missing after apply.");
        expect(static_cast<int>(applyKickTrack->notes.size()) == 2,
            "Safe apply must preserve existing note content instead of importing reference notes.");
        expect(std::abs(applyKickTrack->laneVolume - 0.77f) < 0.001f,
            "Safe apply must restore lane params onto backed tracks.");
        expect(applyKickTrack->selectedSampleName == "MainKickContract",
            "Safe apply must restore selectedSampleName from lane params.");
        expect(std::abs(applyKickTrack->sound.pan - 0.18f) < 0.001f,
            "Safe apply must restore sound layer params from lane params.");

            const auto reAppliedState = StyleLabReferenceService::createDefaultState(applyTarget, "Boom Bap", "Classic", 4, 92);
            const auto reAppliedMetadataJson = StyleLabReferenceService::buildReferenceMetadataJson(applyTarget, reAppliedState);
            juce::String reAppliedParseError;
            const auto reAppliedRecord = StyleLabReferenceBrowserService::parseMetadataJson(reAppliedMetadataJson,
                                                        juce::File("C:/contract/reapplied"),
                                                        &reAppliedParseError);
            expect(reAppliedRecord.has_value(), "Re-export parse failed after apply: " + reAppliedParseError);
            expect(reAppliedRecord->runtimeLanes.size() == originalApplyLaneCount,
                "Re-exported runtime lane count must match the preserved apply-target layout, not the reference subset/shape.");
            expect(reAppliedRecord->runtimeLanes[0].laneId == originalApplyOrder[0],
                "Re-export after apply must preserve the original apply-target ordering.");
                const auto reAppliedKickLane = std::find_if(reAppliedRecord->runtimeLanes.begin(),
                                          reAppliedRecord->runtimeLanes.end(),
                                          [&](const StyleLabReferenceLaneRecord& lane)
                                          {
                                           return lane.laneId == kickLaneId;
                                          });
                expect(reAppliedKickLane != reAppliedRecord->runtimeLanes.end(),
                    "Re-export after apply must preserve kick lane identity in the existing project layout.");
                expect(reAppliedKickLane->laneName != "Main Kick",
                    "Re-export after apply must preserve the apply-target lane naming instead of importing reference layout names.");
}

        void runStyleDefinitionResolverContract()
        {
            auto referenceProject = createDefaultProject();
            referenceProject.params.genre = GenreType::Rap;
            referenceProject.params.rapSubstyle = 0;

            auto* kickTrack = findTrackByType(referenceProject, TrackType::Kick);
            auto* subTrack = findTrackByType(referenceProject, TrackType::Sub808);
            expect(kickTrack != nullptr && subTrack != nullptr, "Resolver contract requires Kick and Sub808 tracks.");
            const auto subLaneId = subTrack->laneId;

            kickTrack->laneRole = "style_core";
            kickTrack->laneVolume = 1.1f;
            kickTrack->selectedSampleName = "ResolverKick";
            kickTrack->sound.pan = -0.3f;

            expect(RuntimeLaneLifecycle::renameLane(referenceProject, kickTrack->laneId, "Resolver Kick"),
                "Resolver contract kick rename failed.");
            moveLaneToFront(referenceProject, subLaneId);
            PatternProjectSerialization::validate(referenceProject);

            const auto state = StyleLabReferenceService::createDefaultState(referenceProject, "Rap", "EastCoast", 4, 92);
            const auto saveResult = StyleLabReferenceService::saveReferencePattern(referenceProject, state);
            expect(saveResult.success, "Failed to save resolver contract reference pattern.");

            juce::String loadError;
            const auto loaded = StyleDefinitionLoader::loadLatestForStyle("Rap",
                                               "EastCoast",
                                               StyleLabReferenceService::getReferenceRootDirectory(),
                                               &loadError);
            expect(loaded.has_value(), "StyleDefinitionLoader failed to load saved reference: " + loadError);
            expect(loaded->loadedFromReference, "Loaded style definition must report reference source.");
            expect(!loaded->lanes.empty(), "Loaded style definition must include lanes.");
            expect(loaded->lanes.front().laneId == subLaneId,
                "StyleDefinitionLoader must preserve runtime lane order from metadata.");

            auto applyTarget = createDefaultProject();
            applyTarget.params.genre = GenreType::Rap;
            applyTarget.params.rapSubstyle = 0;
            const auto originalOrder = applyTarget.runtimeLaneOrder;
            expect(StyleDefinitionResolver::applyToProject(*loaded, applyTarget, &loadError),
                "StyleDefinitionResolver failed to apply loaded definition: " + loadError);

            expect(!applyTarget.runtimeLaneOrder.empty(), "Resolved project must keep runtime lane order.");
            expect(applyTarget.runtimeLaneOrder == originalOrder,
                "Reference-loaded style influence must preserve the current project lane order instead of overriding layout.");

            auto* appliedKick = findTrackByType(applyTarget, TrackType::Kick);
            expect(appliedKick != nullptr, "Resolved project lost Kick backing track.");
            expect(appliedKick->laneRole == "style_core", "Resolved project must restore laneRole from Style Lab metadata.");
            expect(std::abs(appliedKick->laneVolume - 1.1f) < 0.001f,
                "Resolved project must restore laneVolume from Style Lab metadata.");
            expect(appliedKick->selectedSampleName == "ResolverKick",
                "Resolved project must restore selected sample metadata.");

            auto fallbackEast = createDefaultProject();
            fallbackEast.params.genre = GenreType::Rap;
            fallbackEast.params.rapSubstyle = 0;
            auto fallbackDirtySouth = createDefaultProject();
            fallbackDirtySouth.params.genre = GenreType::Rap;
            fallbackDirtySouth.params.rapSubstyle = 2;

            const auto eastFallback = StyleDefinitionLoader::buildFallback(GenreType::Rap, 0);
            const auto dirtySouthFallback = StyleDefinitionLoader::buildFallback(GenreType::Rap, 2);
            expect(StyleDefinitionResolver::applyToProject(eastFallback, fallbackEast, &loadError),
                "Failed to apply EastCoast fallback style definition: " + loadError);
            expect(StyleDefinitionResolver::applyToProject(dirtySouthFallback, fallbackDirtySouth, &loadError),
                "Failed to apply DirtySouth fallback style definition: " + loadError);

            auto* eastSub = findTrackByType(fallbackEast, TrackType::Sub808);
            auto* dirtySouthSub = findTrackByType(fallbackDirtySouth, TrackType::Sub808);
            expect(eastSub != nullptr && dirtySouthSub != nullptr, "Fallback styles must preserve Sub808 backing track.");
            expect(!eastSub->enabled && dirtySouthSub->enabled,
                "Changing rap substyle must change resolved project structure through lane enable state.");
        }

        void runPartialReferencePreservationContract()
        {
            const auto drillSubstyleName = StyleDefinitionLoader::substyleNameFor(GenreType::Drill, 0);

            auto referenceProject = createDefaultProject();
            referenceProject.params.genre = GenreType::Drill;
            referenceProject.params.drillSubstyle = 0;

            auto* referenceHat = findTrackByType(referenceProject, TrackType::HiHat);
            auto* referenceKick = findTrackByType(referenceProject, TrackType::Kick);
            auto* referenceSnare = findTrackByType(referenceProject, TrackType::Snare);
            expect(referenceHat != nullptr && referenceKick != nullptr && referenceSnare != nullptr,
                "Partial reference contract requires HiHat, Kick and Snare tracks.");

            referenceHat->laneRole = "ref_hat_only";
            referenceHat->laneVolume = 0.66f;
            referenceHat->selectedSampleName = "ReferenceHatOnly";
            referenceHat->enabled = false;
            referenceHat->notes = {
                { 42, 0, 1, 94, 0, false },
                { 42, 2, 1, 66, 0, false },
                { 42, 4, 1, 98, 0, false },
                { 42, 7, 1, 72, 0, false },
                { 42, 8, 1, 92, 0, false },
                { 42, 10, 1, 62, 0, false },
                { 42, 12, 1, 96, 0, false },
                { 42, 15, 1, 70, 0, false }
            };
            referenceKick->notes = { { 36, 0, 1, 112, 0, false, "keep_project_only" } };
            referenceSnare->notes = { { 38, 4, 1, 108, 0, false, "keep_project_only" } };

            PatternProjectSerialization::validate(referenceProject);
            const auto state = StyleLabReferenceService::createDefaultState(referenceProject, "Drill", drillSubstyleName, 4, 142);
            const auto metadataJson = StyleLabReferenceService::buildReferenceMetadataJson(referenceProject, state);
            auto metadataRoot = parseJson(metadataJson);

            auto& rootRuntimeLaneOrder = const_cast<juce::Array<juce::var>&>(requireArray(propertyOf(metadataRoot, "runtimeLaneOrder", "metadataRoot"), "metadataRoot.runtimeLaneOrder"));
            auto& rootLaneLayout = const_cast<juce::Array<juce::var>&>(requireArray(propertyOf(metadataRoot, "laneLayout", "metadataRoot"), "metadataRoot.laneLayout"));
            auto referenceProjectVar = propertyOf(metadataRoot, "referenceProject", "metadataRoot");
            auto* referenceProjectObject = requireObject(referenceProjectVar, "referenceProject");
            auto* referenceTracks = referenceProjectObject->getProperty("tracks").getArray();
            expect(referenceTracks != nullptr, "referenceProject.tracks must be an array.");

            const auto* hatLane = findLaneByType(referenceProject, TrackType::HiHat);
            expect(hatLane != nullptr, "HiHat lane missing from reference runtime profile.");

            const juce::String hatLaneId = hatLane->laneId;
            const auto* hatLayout = findObjectByStringProperty(rootLaneLayout, "laneId", hatLaneId);
            expect(hatLayout != nullptr, "HiHat lane missing from metadata laneLayout.");

            juce::Array<juce::var> filteredOrder;
            filteredOrder.add(hatLaneId);
            rootRuntimeLaneOrder = filteredOrder;

            juce::Array<juce::var> filteredLayout;
            filteredLayout.add(*hatLayout);
            rootLaneLayout = filteredLayout;

            auto* hatLayoutObject = filteredLayout.getReference(0).getDynamicObject();
            expect(hatLayoutObject != nullptr, "Filtered HiHat layout must remain an object.");
            auto laneParamsVar = hatLayoutObject->getProperty("laneParams");
            auto* laneParamsObject = requireObject(laneParamsVar, "filteredHatLayout.laneParams");
            laneParamsObject->setProperty("enabled", false);
            laneParamsObject->setProperty("laneRole", "ref_hat_only");
            laneParamsObject->setProperty("laneVolume", 0.66f);
            laneParamsObject->setProperty("selectedSampleName", "ReferenceHatOnly");

            referenceProjectObject->setProperty("totalRuntimeLaneCount", 1);
            referenceProjectObject->setProperty("backedLaneCount", 1);
            referenceProjectObject->setProperty("unbackedLaneCount", 0);

            juce::String partialParseError;
            const auto partialRecord = StyleLabReferenceBrowserService::parseMetadataJson(juce::JSON::toString(metadataRoot),
                                                                                          juce::File("C:/contract/hihat_only"),
                                                                                          &partialParseError);
            expect(partialRecord.has_value(), "Failed to parse HiHat-only reference metadata: " + partialParseError);
            expect(partialRecord->runtimeLanes.size() == 1, "HiHat-only reference must parse as a single runtime lane.");
            expect(partialRecord->runtimeLanes.front().runtimeTrackType == juce::String(toString(TrackType::HiHat)),
                "HiHat-only reference must keep HiHat runtime type.");

            auto applyTarget = createDefaultProject();
            applyTarget.params.genre = GenreType::Drill;
            applyTarget.params.drillSubstyle = 0;
            const auto originalOrder = applyTarget.runtimeLaneOrder;
            const auto originalLaneCount = applyTarget.runtimeLaneProfile.lanes.size();
            const auto originalTrackCount = applyTarget.tracks.size();

            auto* applyKick = findTrackByType(applyTarget, TrackType::Kick);
            auto* applySnare = findTrackByType(applyTarget, TrackType::Snare);
            auto* applyHat = findTrackByType(applyTarget, TrackType::HiHat);
            expect(applyKick != nullptr && applySnare != nullptr && applyHat != nullptr,
                "Apply target must have Kick, Snare and HiHat tracks.");

            applyKick->enabled = true;
            applyKick->muted = true;
            applyKick->laneRole = "kick_keep";
            applyKick->laneVolume = 1.17f;
            applySnare->enabled = false;
            applySnare->muted = false;
            applySnare->laneRole = "snare_keep";
            applySnare->laneVolume = 0.91f;
            applyHat->enabled = true;
            applyHat->laneRole = "hat_before_ref";
            applyHat->laneVolume = 0.82f;

            juce::String applyError;
            expect(StyleLabReferenceBrowserService::applyReferenceToProject(*partialRecord, applyTarget, &applyError),
                "HiHat-only browser apply failed: " + applyError);

            applyKick = findTrackByType(applyTarget, TrackType::Kick);
            applySnare = findTrackByType(applyTarget, TrackType::Snare);
            applyHat = findTrackByType(applyTarget, TrackType::HiHat);
            expect(applyKick != nullptr && applySnare != nullptr && applyHat != nullptr,
                "Selective apply must preserve Kick, Snare and HiHat tracks.");
            expect(applyTarget.runtimeLaneOrder == originalOrder,
                "HiHat-only browser apply must preserve the existing lane order.");
            expect(applyTarget.runtimeLaneProfile.lanes.size() == originalLaneCount,
                "HiHat-only browser apply must preserve the existing runtime lane count.");
            expect(applyTarget.tracks.size() == originalTrackCount,
                "HiHat-only browser apply must preserve the existing backed track count.");
            expect(applyKick->enabled && applyKick->muted && applyKick->laneRole == "kick_keep" && std::abs(applyKick->laneVolume - 1.17f) < 0.001f,
                "Kick must remain untouched when the reference contains only HiHat.");
            expect(!applySnare->enabled && !applySnare->muted && applySnare->laneRole == "snare_keep" && std::abs(applySnare->laneVolume - 0.91f) < 0.001f,
                "Snare must remain untouched when the reference contains only HiHat.");
            expect(!applyHat->enabled && applyHat->laneRole == "ref_hat_only" && std::abs(applyHat->laneVolume - 0.66f) < 0.001f,
                "HiHat-only browser apply must still influence the matching HiHat lane.");

            auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("DRUMENGINE_RuntimeMetadataContract")
                .getChildFile("HiHatOnlyReference");
            tempRoot.deleteRecursively();
            const auto referenceDirectory = tempRoot
                .getChildFile(StyleDefinitionLoader::genreDisplayName(GenreType::Drill))
                .getChildFile(drillSubstyleName)
                .getChildFile("2099-01-01_00-00-00_hihat_only");
            expect(referenceDirectory.createDirectory(), "Failed to create temporary reference directory.");
            const auto metadataFile = referenceDirectory.getChildFile("metadata.json");
            expect(metadataFile.replaceWithText(juce::JSON::toString(metadataRoot)),
                "Failed to write temporary HiHat-only metadata.json.");

            juce::String loadError;
            const auto loaded = StyleDefinitionLoader::loadLatestForStyle(StyleDefinitionLoader::genreDisplayName(GenreType::Drill),
                                                                          drillSubstyleName,
                                                                          tempRoot,
                                                                          &loadError);
            expect(loaded.has_value(), "Failed to load temporary HiHat-only reference: " + loadError);
            expect(loaded->loadedFromReference, "Temporary HiHat-only definition must be marked as loadedFromReference.");
            expect(loaded->lanes.size() == 1, "HiHat-only loaded definition must contain only one matching lane.");
            expect(loaded->lanes.front().runtimeTrackType.has_value() && *loaded->lanes.front().runtimeTrackType == TrackType::HiHat,
                "HiHat-only loaded definition must target HiHat.");
            expect(loaded->referenceHatSkeleton.has_value() && loaded->referenceHatSkeleton->available,
                "Drill HiHat-only reference must extract explicit ReferenceHatSkeleton.");
            expect(!loaded->referenceHatSkeleton->barMaps.empty()
                   && !loaded->referenceHatSkeleton->barMaps.front().backboneSteps16.empty(),
                "ReferenceHatSkeleton must contain per-bar backbone step map.");

            auto influenceTarget = createDefaultProject();
            influenceTarget.params.genre = GenreType::Drill;
            influenceTarget.params.drillSubstyle = 0;
            const auto influenceOrder = influenceTarget.runtimeLaneOrder;
            const auto influenceLaneCount = influenceTarget.runtimeLaneProfile.lanes.size();
            const auto influenceTrackCount = influenceTarget.tracks.size();

            auto* influenceKick = findTrackByType(influenceTarget, TrackType::Kick);
            auto* influenceSnare = findTrackByType(influenceTarget, TrackType::Snare);
            auto* influenceHat = findTrackByType(influenceTarget, TrackType::HiHat);
            expect(influenceKick != nullptr && influenceSnare != nullptr && influenceHat != nullptr,
                "Influence target must have Kick, Snare and HiHat tracks.");

            influenceKick->enabled = true;
            influenceKick->laneRole = "kick_keep";
            influenceKick->laneVolume = 1.07f;
            influenceSnare->enabled = false;
            influenceSnare->laneRole = "snare_keep";
            influenceSnare->laneVolume = 0.88f;
            influenceHat->enabled = true;
            influenceHat->laneRole = "hat_before_ref";
            influenceHat->laneVolume = 0.79f;

            expect(DrillStyleInfluence::applyResolvedStyle(*loaded, influenceTarget, &loadError),
                "DrillStyleInfluence failed to apply HiHat-only loaded definition: " + loadError);

            influenceKick = findTrackByType(influenceTarget, TrackType::Kick);
            influenceSnare = findTrackByType(influenceTarget, TrackType::Snare);
            influenceHat = findTrackByType(influenceTarget, TrackType::HiHat);
            expect(influenceKick != nullptr && influenceSnare != nullptr && influenceHat != nullptr,
                "Reference influence path must preserve Kick, Snare and HiHat tracks.");
            expect(influenceTarget.runtimeLaneOrder == influenceOrder,
                "HiHat-only reference influence must preserve the current lane order.");
            expect(influenceTarget.runtimeLaneProfile.lanes.size() == influenceLaneCount,
                "HiHat-only reference influence must preserve the current runtime lane count.");
            expect(influenceTarget.tracks.size() == influenceTrackCount,
                "HiHat-only reference influence must preserve the current backed track count.");
            expect(influenceKick->enabled && influenceKick->laneRole == "kick_keep" && std::abs(influenceKick->laneVolume - 1.07f) < 0.001f,
                "Kick must remain untouched by HiHat-only style influence.");
            expect(!influenceSnare->enabled && influenceSnare->laneRole == "snare_keep" && std::abs(influenceSnare->laneVolume - 0.88f) < 0.001f,
                "Snare must remain untouched by HiHat-only style influence.");
            expect(!influenceHat->enabled && influenceHat->laneRole == "ref_hat_only" && std::abs(influenceHat->laneVolume - 0.66f) < 0.001f,
                "HiHat-only style influence must still affect the matching HiHat lane.");
            expect(influenceTarget.styleInfluence.referenceHatSkeleton.available,
                "Drill style influence must propagate ReferenceHatSkeleton into project.styleInfluence.");
            expect(!influenceTarget.styleInfluence.referenceHatSkeleton.rollClusters.empty(),
                "ReferenceHatSkeleton should preserve extracted roll clusters for Drill generation.");

            tempRoot.deleteRecursively();
        }

        void runGenreSpecificStyleInfluenceContract()
        {
            auto definition = StyleDefinitionLoader::buildFallback(GenreType::Rap, 0);
            auto laneIt = std::find_if(definition.lanes.begin(), definition.lanes.end(), [](const auto& lane)
            {
                return lane.runtimeTrackType.has_value() && *lane.runtimeTrackType == TrackType::Kick;
            });
            expect(laneIt != definition.lanes.end(), "Shared resolved style contract is missing Kick lane.");

            laneIt->laneParams.available = true;
            laneIt->laneParams.enabled = false;
            laneIt->laneParams.muted = true;
            laneIt->laneParams.solo = true;
            laneIt->laneParams.locked = true;
            laneIt->laneParams.laneRole = "adapter_role";
            laneIt->laneParams.laneVolume = 1.23f;
            laneIt->laneParams.selectedSampleIndex = 7;
            laneIt->laneParams.selectedSampleName = "AdapterKick";
            laneIt->laneParams.sound.pan = -0.41f;

            definition.styleHints.set("groove.swing", 0.78f);
            definition.styleHints.set("groove.timing", 0.52f);
            definition.styleHints.set("groove.humanize", 0.61f);
            definition.styleHints.set("groove.density", 0.68f);
            definition.styleHints.set("boom_bap.groove_looseness", 0.88f);
            definition.styleHints.set("boom_bap.perc_sparsity", 0.72f);
            definition.styleHints.set("boom_bap.clap_focus", 0.95f);
            definition.styleHints.set("boom_bap.swing_feel", 0.84f);
            definition.styleHints.set("rap.groove_looseness", 0.28f);
            definition.styleHints.set("rap.support_density", 0.35f);
            definition.styleHints.set("rap.accent_push", 0.62f);
            definition.styleHints.set("trap.hat_subdivision", 0.90f);
            definition.styleHints.set("trap.bounce", 0.82f);
            definition.styleHints.set("trap.emphasis_808", 0.93f);
            definition.styleHints.set("trap.open_hat_profile", 0.74f);
            definition.styleHints.set("drill.anchor_rigidity", 0.92f);
            definition.styleHints.set("drill.hat_motion", 0.79f);
            definition.styleHints.set("drill.kick_808_coupling", 0.88f);
            definition.styleHints.set("drill.gap_intent", 0.64f);

            juce::String applyError;

            auto rapProject = createDefaultProject();
            auto boomBapProject = createDefaultProject();
            auto trapProject = createDefaultProject();
            auto drillProject = createDefaultProject();

            expect(RapStyleInfluence::applyResolvedStyle(definition, rapProject, &applyError),
                "RapStyleInfluence failed to apply shared resolved style contract: " + applyError);
            expect(BoomBapStyleInfluence::applyResolvedStyle(definition, boomBapProject, &applyError),
                "BoomBapStyleInfluence failed to apply shared resolved style contract: " + applyError);
            expect(TrapStyleInfluence::applyResolvedStyle(definition, trapProject, &applyError),
                "TrapStyleInfluence failed to apply shared resolved style contract: " + applyError);
            expect(DrillStyleInfluence::applyResolvedStyle(definition, drillProject, &applyError),
                "DrillStyleInfluence failed to apply shared resolved style contract: " + applyError);

            auto* rapKick = findTrackByType(rapProject, TrackType::Kick);
            auto* boomBapKick = findTrackByType(boomBapProject, TrackType::Kick);
            auto* trapKick = findTrackByType(trapProject, TrackType::Kick);
            auto* drillKick = findTrackByType(drillProject, TrackType::Kick);
            expect(rapKick != nullptr && boomBapKick != nullptr && trapKick != nullptr && drillKick != nullptr,
                "Genre-specific style influence must preserve Kick backing track across all engines.");

            expect(!rapKick->enabled && !boomBapKick->enabled && !trapKick->enabled && !drillKick->enabled,
                "Shared resolved style contract must drive common lane enable state across genres.");
            expect(rapKick->laneRole == "adapter_role"
                    && boomBapKick->laneRole == "adapter_role"
                    && trapKick->laneRole == "adapter_role"
                    && drillKick->laneRole == "adapter_role",
                "All genre adapters must understand the same lane role field.");

            expect(rapKick->selectedSampleName == "AdapterKick"
                    && boomBapKick->selectedSampleName == "AdapterKick"
                    && trapKick->selectedSampleName == "AdapterKick"
                    && drillKick->selectedSampleName == "AdapterKick",
                "Shared sample-selection params must project consistently before genre-specific biasing begins.");
            expect(rapKick->sound.pan < -0.4f
                    && boomBapKick->sound.pan < -0.4f
                    && trapKick->sound.pan < -0.4f
                    && drillKick->sound.pan < -0.4f,
                "Shared sound-layer params must project consistently before genre-specific biasing begins.");

            expect(!rapKick->locked && !boomBapKick->locked && !trapKick->locked && !drillKick->locked,
                "Genre-specific style influence must not inherit editor lock state from the shared style contract by default.");
            expect(std::abs(boomBapKick->laneVolume - 1.23f) < 0.001f
                    && std::abs(trapKick->laneVolume - 1.23f) < 0.001f
                    && std::abs(drillKick->laneVolume - 1.23f) < 0.001f
                    && std::abs(rapKick->laneVolume - 1.23f) < 0.001f,
                "Genre-specific biasing must not overwrite shared lane-volume params.");
            expect(rapKick->laneId == laneIt->laneId
                    && boomBapKick->laneId == laneIt->laneId
                    && trapKick->laneId == laneIt->laneId
                    && drillKick->laneId == laneIt->laneId,
                "Genre-specific biasing must not rewrite shared runtime layout identity.");

            expect(boomBapProject.params.swingPercent > rapProject.params.swingPercent
                    && rapProject.params.swingPercent > trapProject.params.swingPercent
                    && trapProject.params.swingPercent > drillProject.params.swingPercent,
                "The same shared swing hint must be interpreted with different genre-specific groove depth.");
            expect(boomBapProject.params.humanizeAmount > rapProject.params.humanizeAmount
                    && rapProject.params.humanizeAmount > drillProject.params.humanizeAmount,
                "BoomBap, Rap and Drill must digest the same looseness/rigidity hints into different humanize amounts.");
            expect(trapProject.params.densityAmount > rapProject.params.densityAmount,
                "Trap must turn the shared contract into denser hat/808 motion than Rap.");
            expect(drillProject.params.timingAmount < rapProject.params.timingAmount,
                "Drill must digest the shared contract into a tighter, more rigid timing result than Rap.");

            expect(laneBiasFor(boomBapProject.styleInfluence, TrackType::ClapGhostSnare).balanceWeight
                    > laneBiasFor(rapProject.styleInfluence, TrackType::ClapGhostSnare).balanceWeight,
                "BoomBap biasing must push clap/backbeat balance harder than Rap for the same shared definition.");
            expect(laneBiasFor(trapProject.styleInfluence, TrackType::OpenHat).activityWeight
                    > laneBiasFor(boomBapProject.styleInfluence, TrackType::OpenHat).activityWeight,
                "Trap biasing must react to open-hat profile hints more strongly than BoomBap.");
            expect(laneBiasFor(trapProject.styleInfluence, TrackType::Sub808).balanceWeight
                        > laneBiasFor(rapProject.styleInfluence, TrackType::Sub808).balanceWeight
                    && laneBiasFor(drillProject.styleInfluence, TrackType::Sub808).balanceWeight
                        > laneBiasFor(rapProject.styleInfluence, TrackType::Sub808).balanceWeight,
                "Trap and Drill must translate bass-coupling hints into stronger 808 balance than Rap.");
            expect(laneBiasFor(boomBapProject.styleInfluence, TrackType::Perc).activityWeight
                    < laneBiasFor(rapProject.styleInfluence, TrackType::Perc).activityWeight,
                "BoomBap biasing must turn perc sparsity hints into a leaner texture activity than Rap.");
            expect(drillProject.styleInfluence.lowEndCouplingWeight > rapProject.styleInfluence.lowEndCouplingWeight,
                "Drill biasing must raise low-end coupling beyond Rap.");
            expect(trapProject.styleInfluence.bounceWeight > rapProject.styleInfluence.bounceWeight,
                "Trap biasing must raise bounce weight beyond Rap.");
            expect(drillProject.styleInfluence.anchorRigidityWeight > boomBapProject.styleInfluence.anchorRigidityWeight,
                "Drill biasing must keep anchor rigidity above BoomBap.");
        }

        void runStyleInfluenceGenerationPathContract()
        {
            GeneratorParams params;
            params.bars = 2;
            params.bpm = 142.0f;
            params.densityAmount = 0.64f;
            params.keyRoot = 0;
            params.scaleMode = 0;

            StyleInfluenceState lowInfluence;
            StyleInfluenceState highInfluence;

            laneBiasFor(lowInfluence, TrackType::Kick).activityWeight = 0.58f;
            laneBiasFor(highInfluence, TrackType::Kick).activityWeight = 1.34f;
            lowInfluence.supportAccentWeight = 0.78f;
            highInfluence.supportAccentWeight = 1.32f;

            const auto& boomBapStyle = getBoomBapProfile(0);
            std::vector<PhraseRole> boomBapPhrase { PhraseRole::Base, PhraseRole::Ending };
            TrackState boomBapKickLow = makeTrack(TrackType::Kick);
            TrackState boomBapKickHigh = makeTrack(TrackType::Kick);
            BoomBapKickGenerator boomBapKickGenerator;
            std::mt19937 boomLowRng(101);
            std::mt19937 boomHighRng(101);
            boomBapKickGenerator.generate(boomBapKickLow, params, boomBapStyle, lowInfluence, boomBapPhrase, boomLowRng);
            boomBapKickGenerator.generate(boomBapKickHigh, params, boomBapStyle, highInfluence, boomBapPhrase, boomHighRng);
            expect(boomBapKickHigh.notes.size() > boomBapKickLow.notes.size(),
                "BoomBap kick generation must react to styleInfluence kick/support bias.");

            lowInfluence.supportAccentWeight = 0.56f;
            highInfluence.supportAccentWeight = 1.46f;
            laneBiasFor(lowInfluence, TrackType::OpenHat).activityWeight = 0.42f;
            laneBiasFor(highInfluence, TrackType::OpenHat).activityWeight = 1.52f;
            const auto& rapStyle = getRapProfile(0);
            std::vector<RapPhraseRole> rapPhrase { RapPhraseRole::Base, RapPhraseRole::Ending };
            TrackState rapKickLow = makeTrack(TrackType::Kick);
            TrackState rapKickHigh = makeTrack(TrackType::Kick);
            TrackState rapHatLow = makeTrack(TrackType::HiHat);
            TrackState rapHatHigh = makeTrack(TrackType::HiHat);
            RapKickGenerator rapKickGenerator;
            RapHatGenerator rapHatGenerator;
            std::mt19937 rapKickLowRng(202);
            std::mt19937 rapKickHighRng(202);
            std::mt19937 rapHatLowRng(203);
            std::mt19937 rapHatHighRng(203);
            rapKickGenerator.generate(rapKickLow, params, rapStyle, lowInfluence, rapPhrase, rapKickLowRng);
            rapKickGenerator.generate(rapKickHigh, params, rapStyle, highInfluence, rapPhrase, rapKickHighRng);
            rapHatGenerator.generate(rapHatLow, params, rapStyle, lowInfluence, rapPhrase, rapHatLowRng);
            rapHatGenerator.generate(rapHatHigh, params, rapStyle, highInfluence, rapPhrase, rapHatHighRng);
            expect(rapKickHigh.notes.size() > rapKickLow.notes.size(),
                "Rap kick generation must react to styleInfluence support-accent bias.");
            expect(countOddStepNotes(rapHatHigh) > countOddStepNotes(rapHatLow),
                "Rap hat generation must react to styleInfluence support-lane bias.");

            lowInfluence = {};
            highInfluence = {};
            lowInfluence.lowEndCouplingWeight = 0.62f;
            highInfluence.lowEndCouplingWeight = 1.42f;
            lowInfluence.bounceWeight = 0.58f;
            highInfluence.bounceWeight = 1.48f;
            laneBiasFor(lowInfluence, TrackType::HiHat).activityWeight = 0.40f;
            laneBiasFor(highInfluence, TrackType::HiHat).activityWeight = 1.55f;
            laneBiasFor(lowInfluence, TrackType::Sub808).balanceWeight = 0.76f;
            laneBiasFor(highInfluence, TrackType::Sub808).balanceWeight = 1.30f;
            const auto& trapStyle = getTrapProfile(0);
            const auto trapSpec = getTrapStyleSpec(trapStyle.substyle);
            auto trapParams = params;
            trapParams.bars = 4;
            trapParams.bpm = 124.0f;
            std::vector<TrapPhraseRole> trapPhrase { TrapPhraseRole::Base, TrapPhraseRole::Lift, TrapPhraseRole::Base, TrapPhraseRole::Ending };
            TrackState trapKickLow = makeTrack(TrackType::Kick);
            TrackState trapKickHigh = makeTrack(TrackType::Kick);
            TrackState trapKickReference = makeTrack(TrackType::Kick);
            TrackState trapHatLow = makeTrack(TrackType::HiHat);
            TrackState trapHatHigh = makeTrack(TrackType::HiHat);
            TrackState trapSubLow = makeTrack(TrackType::Sub808);
            TrackState trapSubHigh = makeTrack(TrackType::Sub808);
            TrapKickGenerator trapKickGenerator;
            TrapHatGenerator trapHatGenerator;
            Trap808Generator trap808Generator;
            StyleInfluenceState neutralTrapInfluence;
            std::mt19937 trapKickRng(301);
            std::mt19937 trapKickLowRng(304);
            std::mt19937 trapKickHighRng(304);
            std::mt19937 trapHatLowRng(302);
            std::mt19937 trapHatHighRng(302);
            std::mt19937 trapSubLowRng(303);
            std::mt19937 trapSubHighRng(303);
            trapKickGenerator.generate(trapKickReference, trapParams, trapStyle, neutralTrapInfluence, trapPhrase, trapKickRng);
            trapKickGenerator.generate(trapKickLow, trapParams, trapStyle, lowInfluence, trapPhrase, trapKickLowRng);
            trapKickGenerator.generate(trapKickHigh, trapParams, trapStyle, highInfluence, trapPhrase, trapKickHighRng);
            trapHatGenerator.generate(trapHatLow, trapParams, trapStyle, lowInfluence, trapPhrase, trapHatLowRng);
            trapHatGenerator.generate(trapHatHigh, trapParams, trapStyle, highInfluence, trapPhrase, trapHatHighRng);
            trap808Generator.generateRhythm(trapSubLow, trapKickReference, &trapHatLow, nullptr, nullptr, nullptr, trapParams, trapStyle, trapSpec, lowInfluence, 0.85f, trapPhrase, trapSubLowRng);
            trap808Generator.generateRhythm(trapSubHigh, trapKickReference, &trapHatHigh, nullptr, nullptr, nullptr, trapParams, trapStyle, trapSpec, highInfluence, 0.85f, trapPhrase, trapSubHighRng);
            expect(trapKickHigh.notes.size() != trapKickLow.notes.size(),
                "Trap kick generation must react to styleInfluence low-end coupling bias.");
            expect(trapSubHigh.notes.size() > trapSubLow.notes.size(),
                "Trap 808 generation must react to styleInfluence low-end coupling and sub balance.");

            lowInfluence = {};
            highInfluence = {};
            lowInfluence.anchorRigidityWeight = 0.80f;
            highInfluence.anchorRigidityWeight = 1.34f;
            lowInfluence.hatMotionWeight = 0.54f;
            highInfluence.hatMotionWeight = 1.52f;
            lowInfluence.lowEndCouplingWeight = 0.82f;
            highInfluence.lowEndCouplingWeight = 1.34f;
            laneBiasFor(lowInfluence, TrackType::HiHat).activityWeight = 0.38f;
            laneBiasFor(highInfluence, TrackType::HiHat).activityWeight = 1.48f;
            laneBiasFor(lowInfluence, TrackType::Sub808).balanceWeight = 0.78f;
            laneBiasFor(highInfluence, TrackType::Sub808).balanceWeight = 1.28f;
            const auto& drillStyle = getDrillProfile(0);
            const auto drillSpec = getDrill808StyleSpec(drillStyle.substyle);
            std::vector<DrillPhraseRole> drillPhrase { DrillPhraseRole::Statement, DrillPhraseRole::Response, DrillPhraseRole::Tension, DrillPhraseRole::Ending };
            auto drillParams = params;
            drillParams.bars = 4;
            TrackState drillKickLow = makeTrack(TrackType::Kick);
            TrackState drillKickHigh = makeTrack(TrackType::Kick);
            TrackState drillHatLow = makeTrack(TrackType::HiHat);
            TrackState drillHatHigh = makeTrack(TrackType::HiHat);
            TrackState drillSubLow = makeTrack(TrackType::Sub808);
            TrackState drillSubHigh = makeTrack(TrackType::Sub808);
            TrackState drillSnare = makeTrack(TrackType::Snare);
            drillSnare.notes = {
                { 38, 4, 1, 110, 0, false },
                { 38, 12, 1, 112, 0, false },
                { 38, 20, 1, 110, 0, false },
                { 38, 28, 1, 112, 0, false }
            };
            TrackState drillClap = makeTrack(TrackType::ClapGhostSnare);
            DrillKickGenerator drillKickGenerator;
            DrillHatGenerator drillHatGenerator;
            Drill808Generator drill808Generator;
            std::mt19937 drillKickLowRng(401);
            std::mt19937 drillKickHighRng(401);
            std::mt19937 drillHatLowRng(402);
            std::mt19937 drillHatHighRng(402);
            std::mt19937 drillSubLowRng(403);
            std::mt19937 drillSubHighRng(403);
            drillKickGenerator.generate(drillKickLow, drillParams, drillStyle, lowInfluence, drillPhrase, nullptr, drillKickLowRng);
            drillKickGenerator.generate(drillKickHigh, drillParams, drillStyle, highInfluence, drillPhrase, nullptr, drillKickHighRng);
            drillHatGenerator.generate(drillHatLow, drillParams, drillStyle, lowInfluence, drillPhrase, nullptr, &drillSnare, &drillClap, &drillKickLow, drillHatLowRng);
            drillHatGenerator.generate(drillHatHigh, drillParams, drillStyle, highInfluence, drillPhrase, nullptr, &drillSnare, &drillClap, &drillKickHigh, drillHatHighRng);
            drill808Generator.generateRhythm(drillSubLow, drillKickLow, drillParams, drillStyle, drillSpec, lowInfluence, 0.9f, drillPhrase, nullptr, drillSubLowRng);
            drill808Generator.generateRhythm(drillSubHigh, drillKickHigh, drillParams, drillStyle, drillSpec, highInfluence, 0.9f, drillPhrase, nullptr, drillSubHighRng);
            expect(countNonAnchorDrillKicks(drillKickHigh) < countNonAnchorDrillKicks(drillKickLow),
                "Drill kick generation must react to styleInfluence anchor rigidity.");
            expect(drillHatHigh.notes.size() >= drillHatLow.notes.size()
                    && countAbsoluteMicroOffset(drillHatHigh) > countAbsoluteMicroOffset(drillHatLow),
                "Drill hat generation must react to styleInfluence hat activity and motion.");
            expect(drillSubHigh.notes.size() > drillSubLow.notes.size(),
                "Drill 808 generation must react to styleInfluence low-end coupling and sub balance.");
        }

void runDrillReferenceGenerationValidationContract()
{
    const juce::File substyleRoot = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StyleLabReferences/Drill/BrooklynDrill");
    expect(substyleRoot.isDirectory(), "BrooklynDrill reference directory is missing.");

    juce::Array<juce::File> candidateMetadata;
    substyleRoot.findChildFiles(candidateMetadata, juce::File::findFiles, true, "metadata.json");
    expect(candidateMetadata.size() >= 2, "Need at least two BrooklynDrill metadata files for validation.");

    constexpr int seed = 424242;
    std::optional<GeneratedHatReport> selectedA;
    std::optional<GeneratedHatReport> selectedB;
    std::vector<int> onlyA;
    std::vector<int> onlyB;

    for (int i = 0; i < candidateMetadata.size() && !selectedA.has_value(); ++i)
    {
        const auto reportA = generateDrillHatReportForReference(candidateMetadata.getReference(i), "A", seed);
        for (int j = i + 1; j < candidateMetadata.size(); ++j)
        {
            const auto reportB = generateDrillHatReportForReference(candidateMetadata.getReference(j), "B", seed);
            const auto diffA = differenceSteps(reportA.generatedUniqueSteps, reportB.generatedUniqueSteps);
            const auto diffB = differenceSteps(reportB.generatedUniqueSteps, reportA.generatedUniqueSteps);
            if (!diffA.empty() || !diffB.empty())
            {
                selectedA = reportA;
                selectedB = reportB;
                onlyA = takeFirst(diffA, 16);
                onlyB = takeFirst(diffB, 16);
                break;
            }
        }
    }

    expect(selectedA.has_value() && selectedB.has_value(),
           "Different saved drill references must produce different HiHat step outputs for the same seed.");

    const auto& reportA = *selectedA;
    const auto& reportB = *selectedB;

    printDrillReferenceValidationResult("Drill reference validation", seed, reportA, reportB, onlyA, onlyB);
}

void runUKDrillTripletReferenceGenerationValidationContract()
{
    const juce::File substyleRoot = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StyleLabReferences/Drill/UKDrill");
    expect(substyleRoot.isDirectory(), "UKDrill reference directory is missing.");

    juce::Array<juce::File> candidateMetadata;
    substyleRoot.findChildFiles(candidateMetadata, juce::File::findFiles, true, "metadata.json");
    expect(candidateMetadata.size() >= 2, "Need at least two UKDrill metadata files for validation.");

    constexpr int seed = 424242;
    std::optional<GeneratedHatReport> selectedA;
    std::optional<GeneratedHatReport> selectedB;
    std::vector<int> selectedOnlyA;
    std::vector<int> selectedOnlyB;
    int bestScore = std::numeric_limits<int>::min();

    for (int i = 0; i < candidateMetadata.size(); ++i)
    {
        const auto reportA = generateDrillHatReportForReference(candidateMetadata.getReference(i), "A", seed);
        for (int j = i + 1; j < candidateMetadata.size(); ++j)
        {
            const auto reportB = generateDrillHatReportForReference(candidateMetadata.getReference(j), "B", seed);
            const auto diffA = differenceSteps(reportA.generatedUniqueSteps, reportB.generatedUniqueSteps);
            const auto diffB = differenceSteps(reportB.generatedUniqueSteps, reportA.generatedUniqueSteps);
            if (diffA.empty() && diffB.empty())
                continue;

            const int tripletSignal = reportA.tripletClusterCount + reportB.tripletClusterCount;
            const int tripletSpread = std::abs(reportA.tripletClusterCount - reportB.tripletClusterCount);
            const int diffSignal = static_cast<int>(std::min<size_t>(16, diffA.size()) + std::min<size_t>(16, diffB.size()));
            const int score = tripletSignal * 100 + tripletSpread * 10 + diffSignal;

            if (score > bestScore)
            {
                bestScore = score;
                selectedA = reportA;
                selectedB = reportB;
                selectedOnlyA = takeFirst(diffA, 16);
                selectedOnlyB = takeFirst(diffB, 16);
            }
        }
    }

    expect(selectedA.has_value() && selectedB.has_value(),
           "UKDrill validation could not find a differing reference pair.");

    printDrillReferenceValidationResult("UKDrill triplet validation",
                                        seed,
                                        *selectedA,
                                        *selectedB,
                                        selectedOnlyA,
                                        selectedOnlyB);
}

    void runUKDrillExtractorRegression141920Contract()
    {
        const juce::File substyleRoot = juce::File::getCurrentWorkingDirectory()
         .getChildFile("StyleLabReferences/Drill/UKDrill");
        expect(substyleRoot.isDirectory(), "UKDrill reference directory is missing.");

        const auto metadataFile = requireReferenceMetadataFile(substyleRoot, "20260321_141920_bars8_bpm136-148");
        const auto definition = loadDrillReferenceDefinition(metadataFile);
        expect(definition.referenceHatSkeleton.has_value() && definition.referenceHatSkeleton->available,
            "Regression reference must expose ReferenceHatSkeleton.");

        const auto& skeleton = *definition.referenceHatSkeleton;
        const auto* bar0 = findReferenceBar(skeleton, 0);
        expect(bar0 != nullptr, "Regression reference must expose bar 0 skeleton.");
        expect(static_cast<int>(skeleton.tripletClusters.size()) == 2,
            "UKDrill 141920 must preserve 2 extracted triplet clusters.");
        expect(static_cast<int>(skeleton.rollClusters.size()) == 2,
            "UKDrill 141920 must preserve 2 extracted roll clusters.");
        expect(joinInts(bar0->backboneSteps16) == "0,3,6,8,14",
            "UKDrill 141920 bar0 backbone regression changed unexpectedly.");

        const auto inspection = inspectTripletAdjacency(metadataFile);
        expect(inspection.fastAdjacency > 0, "UKDrill 141920 must contain short hat adjacencies.");
        expect((inspection.tripletAdjacency * 2) >= inspection.fastAdjacency,
            "UKDrill 141920 must satisfy the current majority-triplet rule.");

        std::cout << "UKDrill extractor regression 141920" << std::endl;
        std::cout << "  dir=20260321_141920_bars8_bpm136-148" << std::endl;
        std::cout << "  fastAdjacency=" << inspection.fastAdjacency
            << " tripletAdjacency=" << inspection.tripletAdjacency
            << " deltas=" << joinInts(inspection.fastDeltas) << std::endl;
        std::cout << "  backbone16Bar0=" << joinInts(bar0->backboneSteps16)
            << " motion32Bar0=" << joinInts(bar0->motionSteps32)
            << " tripletClusters=" << skeleton.tripletClusters.size()
            << " rollClusters=" << skeleton.rollClusters.size()
            << std::endl;
    }

    void runUKDrillMixedBurstVerdict235114Contract()
    {
        const juce::File substyleRoot = juce::File::getCurrentWorkingDirectory()
         .getChildFile("StyleLabReferences/Drill/UKDrill");
        expect(substyleRoot.isDirectory(), "UKDrill reference directory is missing.");

        const auto metadataFile = requireReferenceMetadataFile(substyleRoot, "20260322_235114_bars8_bpm139-151");
        const auto definition = loadDrillReferenceDefinition(metadataFile);
        expect(definition.referenceHatSkeleton.has_value() && definition.referenceHatSkeleton->available,
            "Mixed-burst reference must expose ReferenceHatSkeleton.");

        const auto inspection = inspectTripletAdjacency(metadataFile);
        expect(inspection.fastAdjacency > 0, "Mixed-burst reference must contain short hat adjacencies.");
        expect(inspection.tripletAdjacency > 0, "Mixed-burst reference must contain at least some triplet-like gaps.");

        const bool majorityTriplet = (inspection.tripletAdjacency * 2) >= inspection.fastAdjacency;
        expect(!majorityTriplet,
            "UKDrill 235114 should remain non-triplet under the current majority-triplet rule.");
        expect(definition.referenceHatSkeleton->tripletClusters.empty(),
            "UKDrill 235114 should not expose explicit triplet clusters under the current rule.");

        std::cout << "UKDrill mixed burst verdict 235114" << std::endl;
        std::cout << "  dir=20260322_235114_bars8_bpm139-151" << std::endl;
        std::cout << "  fastAdjacency=" << inspection.fastAdjacency
            << " tripletAdjacency=" << inspection.tripletAdjacency
            << " deltas=" << joinInts(inspection.fastDeltas) << std::endl;
        std::cout << "  majorityRuleVerdict=" << (majorityTriplet ? "triplet" : "non-triplet")
            << " explicitTripletClusters=" << definition.referenceHatSkeleton->tripletClusters.size()
            << " rollClusters=" << definition.referenceHatSkeleton->rollClusters.size()
            << std::endl;
    }

void runUKDrillReferenceSkeletonScanContract()
{
    const juce::File substyleRoot = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StyleLabReferences/Drill/UKDrill");
    expect(substyleRoot.isDirectory(), "UKDrill reference directory is missing.");

    juce::Array<juce::File> candidateMetadata;
    substyleRoot.findChildFiles(candidateMetadata, juce::File::findFiles, true, "metadata.json");
    expect(candidateMetadata.size() >= 1, "Need at least one UKDrill metadata file for scan.");

    struct ScanRow
    {
        juce::String dirName;
        int tripletClusters = 0;
        int rollClusters = 0;
        int preSnareBar0 = 0;
        juce::String backboneBar0;
        juce::String motionBar0;
    };

    std::vector<ScanRow> rows;
    rows.reserve(static_cast<size_t>(candidateMetadata.size()));

    for (const auto& metadataFile : candidateMetadata)
    {
        juce::String parseError;
        const auto metadataText = metadataFile.loadFileAsString();
        const auto parsed = StyleLabReferenceBrowserService::parseMetadataJson(metadataText,
                                                                               metadataFile.getParentDirectory(),
                                                                               &parseError);
        expect(parsed.has_value(), "Failed to parse UKDrill metadata during scan: " + parseError);

        const auto definition = StyleDefinitionLoader::fromReferenceRecord(GenreType::Drill, *parsed);
        expect(definition.referenceHatSkeleton.has_value() && definition.referenceHatSkeleton->available,
               "UKDrill scan reference must expose ReferenceHatSkeleton.");

        ScanRow row;
        row.dirName = metadataFile.getParentDirectory().getFileName();
        row.tripletClusters = static_cast<int>(definition.referenceHatSkeleton->tripletClusters.size());
        row.rollClusters = static_cast<int>(definition.referenceHatSkeleton->rollClusters.size());
        if (const auto* bar0 = findReferenceBar(*definition.referenceHatSkeleton, 0))
        {
            row.preSnareBar0 = static_cast<int>(bar0->preSnareZoneSteps32.size());
            row.backboneBar0 = joinInts(bar0->backboneSteps16);
            row.motionBar0 = joinInts(bar0->motionSteps32);
        }

        rows.push_back(std::move(row));
    }

    std::sort(rows.begin(), rows.end(), [](const ScanRow& a, const ScanRow& b)
    {
        if (a.tripletClusters != b.tripletClusters)
            return a.tripletClusters > b.tripletClusters;
        if (a.rollClusters != b.rollClusters)
            return a.rollClusters > b.rollClusters;
        return a.dirName < b.dirName;
    });

    int tripletBearingCount = 0;
    for (const auto& row : rows)
        if (row.tripletClusters > 0)
            ++tripletBearingCount;

    std::cout << "UKDrill reference skeleton scan" << std::endl;
    std::cout << "  totalReferences=" << rows.size() << std::endl;
    std::cout << "  referencesWithTriplets=" << tripletBearingCount << std::endl;
    for (const auto& row : rows)
    {
        std::cout << "  dir=" << row.dirName
                  << " tripletClusters=" << row.tripletClusters
                  << " rollClusters=" << row.rollClusters
                  << " preSnareBar0=" << row.preSnareBar0
                  << " backbone16Bar0=" << row.backboneBar0
                  << " motion32Bar0=" << row.motionBar0
                  << std::endl;
    }
}

void runUKDrillKickCorpusValidationContract()
{
    constexpr int seed = 424242;
    juce::String loadError;
    const auto loaded = StyleDefinitionLoader::loadLatestForStyle("Drill",
                                                                  "UKDrill",
                                                                  juce::File::getCurrentWorkingDirectory().getChildFile("StyleLabReferences"),
                                                                  &loadError);
    expect(loaded.has_value(), "Failed to load UKDrill reference corpus: " + loadError);

    const auto fallback = StyleDefinitionLoader::buildFallback(GenreType::Drill, drillSubstyleIndexForName("UKDrill"));
    const auto corpusReport = generateUKDrillKickReport(*loaded, "reference-corpus", seed);
    const auto fallbackReport = generateUKDrillKickReport(fallback, "fallback", seed);

    const auto onlyCorpus = takeFirst(differenceSteps(corpusReport.generatedUniqueSteps, fallbackReport.generatedUniqueSteps), 16);
    const auto onlyFallback = takeFirst(differenceSteps(fallbackReport.generatedUniqueSteps, corpusReport.generatedUniqueSteps), 16);

    expect(!onlyCorpus.empty() || !onlyFallback.empty(),
           "UKDrill kick corpus must change generated kick steps compared with fallback generation.");

    std::cout << "UKDrill kick corpus validation seed=" << seed << std::endl;
    std::cout << "  corpus kicks=" << corpusReport.generatedKickCount
              << " bar0 steps=" << corpusReport.generatedStepsBar0
              << " bar7 steps=" << corpusReport.generatedStepsBar7 << std::endl;
    std::cout << "  fallback kicks=" << fallbackReport.generatedKickCount
              << " bar0 steps=" << fallbackReport.generatedStepsBar0
              << " bar7 steps=" << fallbackReport.generatedStepsBar7 << std::endl;
    std::cout << "  only-in-corpus=" << joinInts(onlyCorpus) << std::endl;
    std::cout << "  only-in-fallback=" << joinInts(onlyFallback) << std::endl;
}
} // namespace
} // namespace bbg

int main(int argc, char** argv)
{
    try
    {
        const juce::String requestedContract = argc > 1 ? juce::String(argv[1]) : juce::String();
        const auto runNamed = [](const char* name, const auto& fn)
        {
            std::cout << "Running contract: " << name << std::endl;
            fn();
        };

        const auto runIfRequested = [&](const char* name, const auto& fn)
        {
            if (requestedContract.isNotEmpty() && requestedContract != name)
                return;
            runNamed(name, fn);
        };

        runIfRequested("DefaultBackedLane", [] { bbg::runDefaultBackedLaneContract(); });
        runIfRequested("LifecycleComposite", [] { bbg::runLifecycleCompositeContract(); });
        runIfRequested("StyleDefinitionResolver", [] { bbg::runStyleDefinitionResolverContract(); });
        runIfRequested("PartialReferencePreservation", [] { bbg::runPartialReferencePreservationContract(); });
        runIfRequested("GenreSpecificStyleInfluence", [] { bbg::runGenreSpecificStyleInfluenceContract(); });
        runIfRequested("StyleInfluenceGenerationPath", [] { bbg::runStyleInfluenceGenerationPathContract(); });
        runIfRequested("DrillReferenceGenerationValidation", [] { bbg::runDrillReferenceGenerationValidationContract(); });
        runIfRequested("UKDrillTripletReferenceGenerationValidation", [] { bbg::runUKDrillTripletReferenceGenerationValidationContract(); });
        runIfRequested("UKDrillExtractorRegression141920", [] { bbg::runUKDrillExtractorRegression141920Contract(); });
        runIfRequested("UKDrillMixedBurstVerdict235114", [] { bbg::runUKDrillMixedBurstVerdict235114Contract(); });
        runIfRequested("UKDrillReferenceSkeletonScan", [] { bbg::runUKDrillReferenceSkeletonScanContract(); });
        runIfRequested("UKDrillKickCorpusValidation", [] { bbg::runUKDrillKickCorpusValidationContract(); });
        std::cout << "HPDG runtime metadata contract: OK" << std::endl;
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "HPDG runtime metadata contract: FAILED\n" << error.what() << std::endl;
        return EXIT_FAILURE;
    }
}