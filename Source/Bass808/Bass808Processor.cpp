#include "Bass808Processor.h"

#include "Bass808Editor.h"
#include "Bass808Widgets.h"

#include <string_view>

namespace bbg
{
namespace
{
// Parameter ids are stable (saved in DAW projects). Version 1: the first HPDG 808; version 2: the
// layers, filter, LFO, HamloMag and phantom of the second pass.
struct FloatParam
{
    const char* id;
    const char* name;
    float BassSettings::*field;
    float low, high, centre, step; // centre 0: linear
    const char* unit;
    int version;
};
struct ChoiceParam
{
    const char* id;
    const char* name;
    int BassSettings::*field;
    const char* choices;
    int version;
};
struct BoolParam
{
    const char* id;
    const char* name;
    bool BassSettings::*field;
    int version;
};

const std::vector<FloatParam>& floatParams()
{
    static const std::vector<FloatParam> list {
        { "punch", "Punch", &BassSettings::punchSemitones, 0.0f, 24.0f, 0.0f, 0.1f, "st", 1 },
        { "punchTime", "Punch Time", &BassSettings::punchMs, 5.0f, 200.0f, 40.0f, 0.0f, "ms", 1 },
        { "drive", "Drive", &BassSettings::drive, 0.5f, 10.0f, 3.0f, 0.0f, "", 1 },
        { "even", "Even", &BassSettings::bias, 0.0f, 0.3f, 0.0f, 0.005f, "", 1 },
        { "saturation", "Saturation", &BassSettings::saturation, 0.0f, 1.0f, 0.0f, 0.01f, "", 1 },
        { "tone", "Tone", &BassSettings::toneHz, 100.0f, 16000.0f, 900.0f, 0.0f, "Hz", 1 },
        { "hold", "Hold", &BassSettings::holdSeconds, 0.0f, 3.0f, 0.4f, 0.0f, "s", 1 },
        { "decay", "Decay", &BassSettings::decaySeconds, 0.05f, 4.0f, 0.5f, 0.0f, "s", 1 },
        { "attack", "Attack", &BassSettings::attackMs, 0.5f, 20.0f, 3.0f, 0.0f, "ms", 1 },
        { "click", "Click", &BassSettings::clickDb, -60.0f, -10.0f, 0.0f, 0.5f, "dB", 1 },
        { "boost", "Boost", &BassSettings::boostDb, 0.0f, 12.0f, 0.0f, 0.1f, "dB", 1 },
        { "glide", "Glide", &BassSettings::glideMs, 0.0f, 500.0f, 100.0f, 0.0f, "ms", 1 },
        { "release", "Release", &BassSettings::releaseMs, 2.0f, 1000.0f, 60.0f, 0.0f, "ms", 1 },
        { "tune", "Tune", &BassSettings::tuneSemitones, -12.0f, 12.0f, 0.0f, 1.0f, "st", 1 },
        { "gain", "Gain", &BassSettings::gainDb, -24.0f, 6.0f, 0.0f, 0.1f, "dB", 1 },
        { "velocity", "Velocity", &BassSettings::velocity, 0.0f, 1.0f, 0.0f, 0.01f, "", 1 },
        { "shape", "Shape", &BassSettings::shape, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "sub", "Sub", &BassSettings::sub, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "synthLevel", "Synth", &BassSettings::synthLevelDb, -60.0f, 6.0f, 0.0f, 0.1f, "dB", 2 },
        { "sustain", "Sustain", &BassSettings::sustain, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "smpLevel", "Sample", &BassSettings::sampleLevelDb, -60.0f, 6.0f, 0.0f, 0.1f, "dB", 2 },
        { "smpTune", "Sample Tune", &BassSettings::sampleTune, -24.0f, 24.0f, 0.0f, 1.0f, "st", 2 },
        { "smpFine", "Sample Fine", &BassSettings::sampleFine, -100.0f, 100.0f, 0.0f, 1.0f, "ct", 2 },
        { "smpStart", "Sample Start", &BassSettings::sampleStart, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "smpEnd", "Sample End", &BassSettings::sampleEnd, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "smpLoopStart", "Loop Start", &BassSettings::sampleLoopStart, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "smpStretch", "Stretch", &BassSettings::sampleStretch, 0.25f, 4.0f, 1.0f, 0.0f, "x", 2 },
        { "handoff", "Handoff", &BassSettings::handoffMs, 0.0f, 500.0f, 60.0f, 0.0f, "ms", 2 },
        { "cutoff", "Cutoff", &BassSettings::cutoffHz, 20.0f, 20000.0f, 600.0f, 0.0f, "Hz", 2 },
        { "reso", "Resonance", &BassSettings::resonance, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "fltEnv", "Filter Env", &BassSettings::filterEnvOct, -4.0f, 4.0f, 0.0f, 0.0f, "oct", 2 },
        { "fltDecay", "Filter Decay", &BassSettings::filterDecayMs, 5.0f, 2000.0f, 200.0f, 0.0f, "ms", 2 },
        { "fltKey", "Key Track", &BassSettings::filterKey, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "vowel", "Vowel", &BassSettings::vowel, 0.0f, 4.0f, 0.0f, 0.0f, "", 2 },
        { "lfoRate", "LFO Rate", &BassSettings::lfoRateHz, 0.05f, 20.0f, 2.0f, 0.0f, "Hz", 2 },
        { "lfoDepth", "LFO Depth", &BassSettings::lfoDepth, 0.0f, 4.0f, 0.0f, 0.0f, "oct", 2 },
        { "satDrive", "Sat Drive", &BassSettings::satDriveDb, 0.0f, 36.0f, 0.0f, 0.1f, "dB", 2 },
        { "satHarm", "Harmonics", &BassSettings::satHarmonics, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "satEvenOdd", "Even/Odd", &BassSettings::satEvenOdd, -1.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "satMix", "Sat Mix", &BassSettings::satMix, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "phantom", "Phantom", &BassSettings::phantom, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
    };
    return list;
}

const std::vector<ChoiceParam>& choiceParams()
{
    static const std::vector<ChoiceParam> list {
        { "mode", "Mode", &BassSettings::mode, "Retrigger,Legato,Glide", 1 },
        { "wave", "Wave", &BassSettings::wave, "Sine,Triangle,Saw,Square,Reese", 2 },
        { "subOct", "Sub Octave", &BassSettings::subOctave, "-1 oct,0 oct", 2 },
        { "fltType", "Filter", &BassSettings::filterType, "LP12,LP24,HP12,BP,Vowel", 2 },
        { "lfoSync", "LFO Sync", &BassSettings::lfoSync, "Free,1/1,1/2,1/4,1/8,1/16,1/4T,1/8T,1/16T", 2 },
        { "satModel", "Saturator", &BassSettings::satModel, "Off,Classic,Tape,Tube,Iron,Germanium,Vinyl", 2 },
    };
    return list;
}

const std::vector<BoolParam>& boolParams()
{
    static const std::vector<BoolParam> list {
        { "smpLoop", "Loop", &BassSettings::sampleLoop, 2 },
        { "smpLock", "Lock Time", &BassSettings::sampleLockTime, 2 },
        { "smpFollow", "Follow Key", &BassSettings::sampleFollow, 2 },
        { "smpReverse", "Reverse", &BassSettings::sampleReverse, 2 },
        { "smpInvert", "Invert", &BassSettings::sampleInvert, 2 },
        { "lfoRetrig", "LFO Retrigger", &BassSettings::lfoRetrigger, 2 },
    };
    return list;
}

juce::String valueText(const FloatParam& p, float value)
{
    if (std::string_view(p.id) == "click" && value <= -59.5f)
        return "off";
    if ((std::string_view(p.id) == "synthLevel" || std::string_view(p.id) == "smpLevel") && value <= -59.5f)
        return "off";
    if (std::string_view(p.id) == "handoff" && value < 0.5f)
        return "layered";
    if (std::string_view(p.id) == "smpStart" || std::string_view(p.id) == "smpEnd" || std::string_view(p.id) == "smpLoopStart")
        return juce::String(juce::roundToInt(value * 100.0f)) + " %";
    if (std::string_view(p.id) == "vowel")
    {
        static const char* vowels[] { "A", "E", "I", "O", "U" };
        const int i = juce::jlimit(0, 3, static_cast<int>(value));
        const float f = value - static_cast<float>(i);
        return f < 0.05f ? juce::String(vowels[i]) : (f > 0.95f ? juce::String(vowels[i + 1]) : juce::String(vowels[i]) + "-" + vowels[i + 1]);
    }
    return b808::formatValue(value, p.unit);
}
} // namespace

Bass808Processor::Bass808Processor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "HPDG808", createLayout())
{
    applyPreset(0);
}

juce::AudioProcessorValueTreeState::ParameterLayout Bass808Processor::createLayout()
{
    const BassSettings defaults;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& p : floatParams())
    {
        juce::NormalisableRange<float> range(p.low, p.high, p.step);
        if (p.centre > 0.0f)
            range.setSkewForCentre(p.centre);
        const FloatParam copy = p;
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { p.id, p.version }, p.name, range, defaults.*(p.field),
            juce::AudioParameterFloatAttributes()
                .withLabel(p.unit)
                .withStringFromValueFunction([copy](float value, int) { return valueText(copy, value); })));
    }
    for (const auto& p : choiceParams())
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { p.id, p.version }, p.name,
                                                                juce::StringArray::fromTokens(p.choices, ",", ""), defaults.*(p.field)));
    for (const auto& p : boolParams())
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { p.id, p.version }, p.name, defaults.*(p.field)));
    return layout;
}

BassSettings Bass808Processor::currentSettings() const
{
    BassSettings s;
    for (const auto& p : floatParams())
        s.*(p.field) = apvts.getRawParameterValue(p.id)->load();
    for (const auto& p : choiceParams())
        s.*(p.field) = juce::roundToInt(apvts.getRawParameterValue(p.id)->load());
    for (const auto& p : boolParams())
        s.*(p.field) = apvts.getRawParameterValue(p.id)->load() >= 0.5f;
    return s;
}

void Bass808Processor::setParameter(const char* id, float value)
{
    if (auto* parameter = apvts.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void Bass808Processor::writeSettings(const BassSettings& s)
{
    for (const auto& p : floatParams())
        setParameter(p.id, s.*(p.field));
    for (const auto& p : choiceParams())
        setParameter(p.id, static_cast<float>(s.*(p.field)));
    for (const auto& p : boolParams())
        setParameter(p.id, s.*(p.field) ? 1.0f : 0.0f);
}

const juce::String Bass808Processor::getProgramName(int index)
{
    const auto& list = bassPresets();
    const auto& preset = list[static_cast<size_t>(juce::jlimit(0, static_cast<int>(list.size()) - 1, index))];
    return juce::String(preset.genre) + " - " + preset.name;
}

void Bass808Processor::applyPreset(int index)
{
    const auto& list = bassPresets();
    currentPreset = juce::jlimit(0, static_cast<int>(list.size()) - 1, index);
    auto settings = list[static_cast<size_t>(currentPreset)].settings;
    if (getSample() != nullptr && settings.sampleLevelDb <= -59.5f)
    {
        // a loaded sample stays where the user put it: the synth presets keep the sample layer's settings
        const auto now = currentSettings();
        settings.sampleLevelDb = now.sampleLevelDb;
        settings.sampleTune = now.sampleTune;
        settings.sampleFine = now.sampleFine;
        settings.sampleStart = now.sampleStart;
        settings.sampleEnd = now.sampleEnd;
        settings.sampleLoop = now.sampleLoop;
        settings.sampleLoopStart = now.sampleLoopStart;
        settings.sampleStretch = now.sampleStretch;
        settings.sampleLockTime = now.sampleLockTime;
        settings.sampleFollow = now.sampleFollow;
        settings.sampleReverse = now.sampleReverse;
        settings.sampleInvert = now.sampleInvert;
    }
    writeSettings(settings);
    apvts.state.setProperty("preset", currentPreset, nullptr);
}

void Bass808Processor::retire(std::shared_ptr<const BassSample> old)
{
    retired.erase(std::remove_if(retired.begin(), retired.end(), [](const auto& s) { return s.use_count() == 1; }), retired.end());
    if (old != nullptr)
        retired.push_back(std::move(old));
}

bool Bass808Processor::loadSample(const juce::File& file, juce::String& error)
{
    auto sample = BassSample::fromFile(file, error);
    if (sample == nullptr)
        return false;
    retire(engine.getSample());
    engine.setSample(std::move(sample));
    setParameter("smpStart", 0.0f);
    setParameter("smpEnd", 1.0f);
    setParameter("smpLoopStart", 0.5f);
    if (currentSettings().sampleLevelDb <= -59.5f)
        setParameter("smpLevel", 0.0f);
    return true;
}

void Bass808Processor::clearSample()
{
    retire(engine.getSample());
    engine.setSample(nullptr);
}

void Bass808Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate, samplesPerBlock);
    mono.assign(static_cast<size_t>(juce::jmax(1, samplesPerBlock)), 0.0f);
    setLatencySamples(engine.latencySamples());
}

bool Bass808Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void Bass808Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (static_cast<int>(mono.size()) < numSamples)
        mono.resize(static_cast<size_t>(numSamples)); // a host that sends more than it announced
    keyboardState.processNextMidiBuffer(midi, 0, numSamples, true);

    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto bpm = position->getBpm())
                engine.setTempo(*bpm);
    engine.setSettings(currentSettings());

    int position = 0;
    for (const auto metadata : midi)
    {
        const int at = juce::jlimit(position, numSamples, metadata.samplePosition);
        if (at > position)
            engine.render(mono.data() + position, at - position);
        position = at;
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            engine.noteOn(message.getNoteNumber(), message.getFloatVelocity());
            lastPlayedNote.store(message.getNoteNumber());
        }
        else if (message.isNoteOff())
            engine.noteOff(message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.allNotesOff();
    }
    if (numSamples > position)
        engine.render(mono.data() + position, numSamples - position);

    float peak = 0.0f;
    int write = scopeWrite.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        const float v = mono[static_cast<size_t>(i)];
        peak = std::max(peak, std::abs(v));
        scope[static_cast<size_t>(write)] = v;
        write = (write + 1) & (kScopeSize - 1);
    }
    scopeWrite.store(write, std::memory_order_release);
    outputPeak.store(std::max(peak, outputPeak.load(std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom(channel, 0, mono.data(), numSamples);
    midi.clear();
}

void Bass808Processor::copyScope(float* destination, int count) const
{
    count = juce::jlimit(0, kScopeSize, count);
    const int end = scopeWrite.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
        destination[i] = scope[static_cast<size_t>((end - count + i + kScopeSize) & (kScopeSize - 1))];
}

juce::AudioProcessorEditor* Bass808Processor::createEditor()
{
    return new Bass808Editor(*this);
}

void Bass808Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (const auto sample = getSample())
    {
        juce::ValueTree child("Sample");
        child.setProperty("name", sample->name, nullptr);
        child.setProperty("path", sample->path, nullptr);
        child.setProperty("wav", sample->toWavMemory().toBase64Encoding(), nullptr);
        state.appendChild(child, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void Bass808Processor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml(*xml);
    std::shared_ptr<const BassSample> sample;
    if (auto child = state.getChildWithName("Sample"); child.isValid())
    {
        juce::MemoryBlock wav;
        if (wav.fromBase64Encoding(child.getProperty("wav").toString()))
            sample = BassSample::fromWavMemory(wav, child.getProperty("name").toString(), child.getProperty("path").toString());
        state.removeChild(child, nullptr);
    }
    apvts.replaceState(state);
    currentPreset = juce::jlimit(0, getNumPrograms() - 1, static_cast<int>(apvts.state.getProperty("preset", 0)));
    retire(engine.getSample());
    engine.setSample(std::move(sample));
}
} // namespace bbg

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bbg::Bass808Processor();
}
