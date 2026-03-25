#pragma once

#include "StyleDefinitionResolver.h"

namespace bbg
{
struct StyleInfluenceApplicationOptions
{
    bool applyRuntimeLaneSkeleton = true;
    bool applyEnabledState = true;
    bool applyMuteState = false;
    bool applySoloState = false;
    bool applyLockState = false;
    bool applyLaneRole = true;
    bool applyLaneVolume = false;
    bool applySampleSelection = false;
    bool applySoundLayer = false;
};

class StyleInfluenceHelpers
{
public:
    static ResolvedStyleDefinition resolveForProject(GenreType genre,
                                                     int substyleIndex,
                                                     juce::String* statusMessage = nullptr);

    static bool applyToProject(const ResolvedStyleDefinition& definition,
                               PatternProject& project,
                               const StyleInfluenceApplicationOptions& options,
                               juce::String* errorMessage = nullptr);
};

class BoomBapStyleInfluence
{
public:
    static StyleInfluenceApplicationOptions applicationOptions();
    static bool applyResolvedStyle(const ResolvedStyleDefinition& definition,
                                   PatternProject& project,
                                   juce::String* errorMessage = nullptr);
    static bool apply(PatternProject& project, juce::String* errorMessage = nullptr);
};

class RapStyleInfluence
{
public:
    static StyleInfluenceApplicationOptions applicationOptions();
    static bool applyResolvedStyle(const ResolvedStyleDefinition& definition,
                                   PatternProject& project,
                                   juce::String* errorMessage = nullptr);
    static bool apply(PatternProject& project, juce::String* errorMessage = nullptr);
};

class TrapStyleInfluence
{
public:
    static StyleInfluenceApplicationOptions applicationOptions();
    static bool applyResolvedStyle(const ResolvedStyleDefinition& definition,
                                   PatternProject& project,
                                   juce::String* errorMessage = nullptr);
    static bool apply(PatternProject& project, juce::String* errorMessage = nullptr);
};

class DrillStyleInfluence
{
public:
    static StyleInfluenceApplicationOptions applicationOptions();
    static bool applyResolvedStyle(const ResolvedStyleDefinition& definition,
                                   PatternProject& project,
                                   juce::String* errorMessage = nullptr);
    static bool apply(PatternProject& project, juce::String* errorMessage = nullptr);
};
} // namespace bbg
