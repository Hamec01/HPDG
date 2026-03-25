#pragma once

#include "StyleDefinitionLoader.h"

namespace bbg
{
class StyleDefinitionResolver
{
public:
    static ResolvedStyleDefinition resolve(GenreType genre,
                                           int substyleIndex,
                                           juce::String* statusMessage = nullptr);

    static bool applyToProject(const ResolvedStyleDefinition& definition,
                               PatternProject& project,
                               juce::String* errorMessage = nullptr);
};
} // namespace bbg