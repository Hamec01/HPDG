#pragma once

#include <optional>

#include <juce_data_structures/juce_data_structures.h>

#include "Bass808/BassSettings.h"
#include "Bass808/BassSample.h"

namespace bbg
{
// BassSettings as a ValueTree ("BassSettings", one property per field, by name): what HPDG keeps on its
// Sub808 lane and what a preset file holds. A missing property keeps the default, so older files load.
juce::ValueTree bassSettingsToTree(const BassSettings&);
BassSettings bassSettingsFromTree(const juce::ValueTree&);

// A preset file (.hpbass, shared by HPDG and the HPDG 808 plugin): the settings and, when the sample layer
// has one, the sample as a WAV inside.
struct BassPresetFile
{
    juce::String name;
    BassSettings settings;
    std::shared_ptr<const BassSample> sample;
};

juce::File bassUserPresetFolder(); // Documents/HamloProd/Bass808 Presets
juce::Array<juce::File> bassUserPresetFiles(); // sorted by name
bool writeBassPreset(const juce::File&, const BassPresetFile&);
std::optional<BassPresetFile> readBassPreset(const juce::File&);
} // namespace bbg
