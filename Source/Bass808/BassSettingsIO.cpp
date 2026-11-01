#include "BassSettingsIO.h"

namespace bbg
{
namespace
{
const juce::Identifier kSettingsNode { "BassSettings" };
const juce::Identifier kPresetNode { "HPDG808Preset" };
const juce::Identifier kSampleNode { "Sample" };

struct FloatField { const char* id; float BassSettings::*member; };
struct IntField { const char* id; int BassSettings::*member; };
struct BoolField { const char* id; bool BassSettings::*member; };

constexpr FloatField kFloats[] {
    { "shape", &BassSettings::shape }, { "sub", &BassSettings::sub }, { "synthLevelDb", &BassSettings::synthLevelDb },
    { "punchSemitones", &BassSettings::punchSemitones }, { "punchMs", &BassSettings::punchMs }, { "drive", &BassSettings::drive },
    { "bias", &BassSettings::bias }, { "saturation", &BassSettings::saturation }, { "toneHz", &BassSettings::toneHz },
    { "clickDb", &BassSettings::clickDb }, { "boostDb", &BassSettings::boostDb },
    { "attackMs", &BassSettings::attackMs }, { "holdSeconds", &BassSettings::holdSeconds }, { "decaySeconds", &BassSettings::decaySeconds },
    { "sustain", &BassSettings::sustain }, { "releaseMs", &BassSettings::releaseMs },
    { "sampleLevelDb", &BassSettings::sampleLevelDb }, { "sampleTune", &BassSettings::sampleTune }, { "sampleFine", &BassSettings::sampleFine },
    { "sampleStart", &BassSettings::sampleStart }, { "sampleEnd", &BassSettings::sampleEnd }, { "sampleLoopStart", &BassSettings::sampleLoopStart },
    { "sampleStretch", &BassSettings::sampleStretch }, { "handoffMs", &BassSettings::handoffMs },
    { "cutoffHz", &BassSettings::cutoffHz }, { "resonance", &BassSettings::resonance }, { "filterEnvOct", &BassSettings::filterEnvOct },
    { "filterDecayMs", &BassSettings::filterDecayMs }, { "filterKey", &BassSettings::filterKey }, { "vowel", &BassSettings::vowel },
    { "lfoRateHz", &BassSettings::lfoRateHz }, { "lfoDepth", &BassSettings::lfoDepth },
    { "satDriveDb", &BassSettings::satDriveDb }, { "satHarmonics", &BassSettings::satHarmonics }, { "satEvenOdd", &BassSettings::satEvenOdd },
    { "satMix", &BassSettings::satMix }, { "phantom", &BassSettings::phantom }, { "busFilter", &BassSettings::busFilter },
    { "glideMs", &BassSettings::glideMs }, { "tuneSemitones", &BassSettings::tuneSemitones }, { "gainDb", &BassSettings::gainDb },
    { "velocity", &BassSettings::velocity },
};
constexpr IntField kInts[] {
    { "wave", &BassSettings::wave }, { "subOctave", &BassSettings::subOctave }, { "filterType", &BassSettings::filterType },
    { "lfoSync", &BassSettings::lfoSync }, { "satModel", &BassSettings::satModel }, { "mode", &BassSettings::mode },
};
constexpr BoolField kBools[] {
    { "sampleLoop", &BassSettings::sampleLoop }, { "sampleLockTime", &BassSettings::sampleLockTime },
    { "sampleFollow", &BassSettings::sampleFollow }, { "sampleReverse", &BassSettings::sampleReverse },
    { "sampleInvert", &BassSettings::sampleInvert }, { "lfoRetrigger", &BassSettings::lfoRetrigger },
};
} // namespace

juce::ValueTree bassSettingsToTree(const BassSettings& settings)
{
    juce::ValueTree tree(kSettingsNode);
    for (const auto& f : kFloats)
        tree.setProperty(f.id, settings.*(f.member), nullptr);
    for (const auto& f : kInts)
        tree.setProperty(f.id, settings.*(f.member), nullptr);
    for (const auto& f : kBools)
        tree.setProperty(f.id, settings.*(f.member), nullptr);
    return tree;
}

BassSettings bassSettingsFromTree(const juce::ValueTree& tree)
{
    BassSettings settings;
    if (!tree.isValid())
        return settings;
    for (const auto& f : kFloats)
        if (tree.hasProperty(f.id))
            settings.*(f.member) = static_cast<float>(tree.getProperty(f.id));
    for (const auto& f : kInts)
        if (tree.hasProperty(f.id))
            settings.*(f.member) = static_cast<int>(tree.getProperty(f.id));
    for (const auto& f : kBools)
        if (tree.hasProperty(f.id))
            settings.*(f.member) = static_cast<bool>(tree.getProperty(f.id));
    settings.wave = juce::jlimit(0, BassSettings::NumWaves - 1, settings.wave);
    settings.filterType = juce::jlimit(0, BassSettings::NumFilters - 1, settings.filterType);
    settings.satModel = juce::jlimit(0, 6, settings.satModel);
    settings.mode = juce::jlimit(0, 2, settings.mode);
    settings.lfoSync = juce::jlimit(0, 8, settings.lfoSync);
    settings.subOctave = juce::jlimit(0, 1, settings.subOctave);
    settings.busFilter = juce::jlimit(-1.0f, 1.0f, settings.busFilter);
    return settings;
}

juce::File bassUserPresetFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HamloProd").getChildFile("Bass808 Presets");
}

juce::Array<juce::File> bassUserPresetFiles()
{
    auto files = bassUserPresetFolder().findChildFiles(juce::File::findFiles, false, "*.hpbass");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension()) < 0;
    });
    return files;
}

bool writeBassPreset(const juce::File& file, const BassPresetFile& preset)
{
    juce::ValueTree root(kPresetNode);
    root.setProperty("name", preset.name, nullptr);
    root.setProperty("version", 1, nullptr);
    root.appendChild(bassSettingsToTree(preset.settings), nullptr);
    if (preset.sample != nullptr)
    {
        juce::ValueTree sampleNode(kSampleNode);
        sampleNode.setProperty("name", preset.sample->name, nullptr);
        sampleNode.setProperty("wav", preset.sample->toWavMemory().toBase64Encoding(), nullptr);
        root.appendChild(sampleNode, nullptr);
    }
    const auto xml = root.createXml();
    if (xml == nullptr)
        return false;
    file.getParentDirectory().createDirectory();
    return xml->writeTo(file);
}

std::optional<BassPresetFile> readBassPreset(const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return std::nullopt;
    const auto root = juce::ValueTree::fromXml(*xml);
    if (!root.hasType(kPresetNode))
        return std::nullopt;
    BassPresetFile preset;
    preset.name = root.getProperty("name", file.getFileNameWithoutExtension()).toString();
    preset.settings = bassSettingsFromTree(root.getChildWithName(kSettingsNode));
    if (const auto sampleNode = root.getChildWithName(kSampleNode); sampleNode.isValid())
    {
        juce::MemoryBlock wav;
        if (wav.fromBase64Encoding(sampleNode.getProperty("wav").toString()))
            preset.sample = BassSample::fromWavMemory(wav, sampleNode.getProperty("name").toString(), {});
    }
    return preset;
}
} // namespace bbg
