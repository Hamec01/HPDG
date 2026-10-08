#include "SampleLabelReader.h"

#include <regex>
#include <string>

namespace bbg
{
namespace
{
constexpr double kMinLabelBpm = 50.0;
constexpr double kMaxLabelBpm = 220.0;
const char* const kNoteNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

std::string stemOf(const juce::String& fileName)
{
    auto stem = juce::File::createFileWithoutCheckingPath(fileName).getFileNameWithoutExtension();
    if (stem.isEmpty())
        stem = fileName;
    // Some packs type a note with a Cyrillic look-alike ("Сm").
    stem = stem.replaceCharacters(juce::CharPointer_UTF8("\xd0\xa1\xd0\x90\xd0\x92\xd0\x95"), "CABE");
    return stem.toStdString();
}

double bpmFromName(const std::string& stem)
{
    // 1. An explicit tempo: "140 BPM", "_118_BPM", "78Bpm", "BPM 92".
    static const std::regex explicitAfter(R"((\d{2,3}(?:\.\d+)?)[\s_\-]*bpm)", std::regex::icase);
    static const std::regex explicitBefore(R"(bpm[\s_\-]*(\d{2,3}(?:\.\d+)?))", std::regex::icase);
    for (const auto* pattern : { &explicitAfter, &explicitBefore })
    {
        std::smatch match;
        if (std::regex_search(stem, match, *pattern))
        {
            const double bpm = std::stod(match[1].str());
            if (bpm >= kMinLabelBpm && bpm <= kMaxLabelBpm)
                return bpm;
        }
    }

    // 2. A bare number in the tempo range ("_90_Dm", "(150)", "DNC_174_"), only when it is the
    //    single such number and not a counter ("Loop 100", "Vol 2", "#120").
    static const std::regex number(R"((\d+))");
    static const std::regex counter(R"((loop|vol|volume|v|no|nr|take|part|pattern|kit|#)[\s_\-\.]*$)", std::regex::icase);
    double found = 0.0;
    int candidates = 0;
    for (auto it = std::sregex_iterator(stem.begin(), stem.end(), number); it != std::sregex_iterator(); ++it)
    {
        const auto& match = *it;
        const auto digits = match[1].str();
        if (digits.size() < 2 || digits.size() > 3)
            continue;
        const double value = std::stod(digits);
        if (value < 60.0 || value > 200.0)
            continue;
        const auto before = stem.substr(0, static_cast<size_t>(match.position(1)));
        if (std::regex_search(before, counter))
            continue;
        found = value;
        ++candidates;
    }
    return candidates == 1 ? found : 0.0;
}

void keyFromName(const std::string& stem, int& root, int& mode)
{
    // The last key token: "G# Min", "_Am", "95Em", "Fmin", "Gsharp", "Dshrp", "Bb", "D#".
    // The letter must not continue a word ("Drum", "Bass") and must be upper case.
    static const std::regex key(R"((?:^|[^A-Za-z])([A-G])(#|b|sharp|shrp)?[\s_]?(minor|major|min|maj|m)?(?=$|[^A-Za-z]))",
                                std::regex::icase);
    static constexpr int letterPitch[] { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    root = -1;
    mode = -1;
    for (auto it = std::sregex_iterator(stem.begin(), stem.end(), key); it != std::sregex_iterator(); ++it)
    {
        const auto& match = *it;
        const char letter = match[1].str()[0];
        if (letter < 'A' || letter > 'G')
            continue; // icase also matched a lower-case letter
        int pc = letterPitch[letter - 'A'];
        const auto accidental = juce::String(match[2].str()).toLowerCase();
        if (accidental == "#" || accidental.startsWith("s"))
            ++pc;
        else if (accidental == "b")
            --pc;
        root = (pc % 12 + 12) % 12;
        const auto suffix = juce::String(match[3].str()).toLowerCase();
        mode = suffix.isEmpty() ? -1 : (suffix.startsWith("maj") ? 1 : 0);
    }
}
} // namespace

juce::String SampleLabels::describe() const
{
    juce::String text = "Sample labels:";
    text << (hasBpm() ? " bpm " + juce::String(bpm, 2) + " (" + bpmSource + ")" : juce::String(" bpm -"));
    if (hasKey())
        text << " | key " << kNoteNames[keyRoot] << (keyMode == 1 ? " major" : keyMode == 0 ? " minor" : "") << " (" << keySource << ")";
    else
        text << " | key -";
    return text;
}

SampleLabels SampleLabelReader::fromName(const juce::String& fileName)
{
    SampleLabels labels;
    const auto stem = stemOf(fileName);
    labels.bpm = bpmFromName(stem);
    if (labels.hasBpm())
        labels.bpmSource = "name";
    keyFromName(stem, labels.keyRoot, labels.keyMode);
    if (labels.hasKey())
        labels.keySource = "name";
    return labels;
}

SampleLabels SampleLabelReader::read(const juce::File& file, const juce::StringPairArray& metadata)
{
    auto labels = fromName(file.getFileName());
    const auto acidTempo = metadata.getValue(juce::WavAudioFormat::acidTempo, {}).getDoubleValue();
    if (acidTempo >= kMinLabelBpm && acidTempo <= kMaxLabelBpm)
    {
        labels.bpm = acidTempo;
        labels.bpmSource = "acid";
    }
    return labels;
}
} // namespace bbg
