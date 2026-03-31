#include "StyleDefinitionResolver.h"

#include "../Services/StyleLabReferenceService.h"
#include "StyleInfluence.h"

namespace bbg
{
ResolvedStyleDefinition StyleDefinitionResolver::resolve(GenreType genre,
                                                         int substyleIndex,
                                                         juce::String* statusMessage)
{
    const auto genreName = StyleDefinitionLoader::genreDisplayName(genre);
    const auto substyleName = StyleDefinitionLoader::substyleNameFor(genre, substyleIndex);

    juce::String loadMessage;
    if (const auto loaded = StyleDefinitionLoader::loadLatestForStyle(genreName,
                                                                      substyleName,
                                                                      StyleLabReferenceService::getReferenceRootDirectory(),
                                                                      &loadMessage))
    {
        auto definition = *loaded;
        definition.genre = genre;
        if (statusMessage != nullptr)
            *statusMessage = definition.loadStrategy == "ranked-reference-set"
                ? "Style Lab ranked reference set"
                : "Style Lab reference";
        return definition;
    }

    if (statusMessage != nullptr)
        *statusMessage = loadMessage;
    return StyleDefinitionLoader::buildFallback(genre, substyleIndex);
}

bool StyleDefinitionResolver::applyToProject(const ResolvedStyleDefinition& definition,
                                             PatternProject& project,
                                             juce::String* errorMessage)
{
    StyleInfluenceApplicationOptions options;
    options.applyMuteState = true;
    options.applySoloState = true;
    options.applyLockState = true;
    options.applyLaneVolume = true;
    options.applySampleSelection = true;
    options.applySoundLayer = true;
    return StyleInfluenceHelpers::applyToProject(definition, project, options, errorMessage);
}
} // namespace bbg