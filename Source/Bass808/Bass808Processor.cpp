#include "Bass808Processor.h"

#include "Bass808Editor.h"

namespace bbg
{
namespace
{
// Parameter ids (stable: saved in DAW projects).
constexpr const char* kPunch = "punch";
constexpr const char* kPunchTime = "punchTime";
constexpr const char* kDrive = "drive";
constexpr const char* kEven = "even";
constexpr const char* kSaturation = "saturation";
constexpr const char* kTone = "tone";
constexpr const char* kHold = "hold";
constexpr const char* kDecay = "decay";
constexpr const char* kAttack = "attack";
constexpr const char* kClick = "click";
constexpr const char* kBoost = "boost";
constexpr const char* kGlide = "glide";
constexpr const char* kMode = "mode";
constexpr const char* kRelease = "release";
constexpr const char* kTune = "tune";
constexpr const char* kGain = "gain";
constexpr const char* kVelocity = "velocity";
constexpr float kClickOff = -60.0f;

juce::NormalisableRange<float> skewed(float lo, float hi, float centre)
{
    juce::NormalisableRange<float> range(lo, hi);
    range.setSkewForCentre(centre);
    return range;
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
    using Float = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    const auto add = [&p](const char* id, const char* name, juce::NormalisableRange<float> range, float def, const char* unit)
    {
        p.push_back(std::make_unique<Float>(juce::ParameterID { id, 1 }, name, range, def, juce::AudioParameterFloatAttributes().withLabel(unit)));
    };
    add(kPunch, "Punch", { 0.0f, 24.0f, 0.1f }, 18.0f, "st");
    add(kPunchTime, "Punch Time", skewed(5.0f, 200.0f, 40.0f), 40.0f, "ms");
    add(kDrive, "Drive", skewed(0.5f, 10.0f, 3.0f), 4.5f, "");
    add(kEven, "Even", { 0.0f, 0.3f, 0.005f }, 0.08f, "");
    add(kSaturation, "Saturation", { 0.0f, 1.0f, 0.01f }, 0.8f, "");
    add(kTone, "Tone", skewed(100.0f, 3000.0f, 600.0f), 800.0f, "Hz");
    add(kHold, "Hold", skewed(0.0f, 3.0f, 0.4f), 0.05f, "s");
    add(kDecay, "Decay", skewed(0.05f, 3.0f, 0.5f), 0.32f, "s");
    add(kAttack, "Attack", skewed(0.5f, 20.0f, 3.0f), 2.0f, "ms");
    add(kClick, "Click", { kClickOff, -10.0f, 0.5f }, -30.0f, "dB");
    add(kBoost, "Boost", { 0.0f, 12.0f, 0.1f }, 0.0f, "dB");
    add(kGlide, "Glide", skewed(0.0f, 500.0f, 100.0f), 120.0f, "ms");
    p.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { kMode, 1 }, "Mode",
                                                             juce::StringArray { "Retrigger", "Legato", "Glide" }, 2));
    add(kRelease, "Release", skewed(2.0f, 1000.0f, 60.0f), 30.0f, "ms");
    add(kTune, "Tune", { -12.0f, 12.0f, 1.0f }, 0.0f, "st");
    add(kGain, "Gain", { -24.0f, 6.0f, 0.1f }, 0.0f, "dB");
    add(kVelocity, "Velocity", { 0.0f, 1.0f, 0.01f }, 0.5f, "");
    return { p.begin(), p.end() };
}

float Bass808Processor::paramValue(const char* id) const
{
    return apvts.getRawParameterValue(id)->load();
}

const juce::String Bass808Processor::getProgramName(int index)
{
    return Synth808Voice::presets()[static_cast<size_t>(juce::jlimit(0, Synth808Voice::kPresetCount - 1, index))].name;
}

void Bass808Processor::applyPreset(int index)
{
    currentPreset = juce::jlimit(0, Synth808Voice::kPresetCount - 1, index);
    const auto& preset = Synth808Voice::presets()[static_cast<size_t>(currentPreset)];
    const auto set = [this](const char* id, float value)
    {
        if (auto* parameter = apvts.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    set(kPunch, preset.punchSemitones);
    set(kPunchTime, preset.punchMs);
    set(kDrive, preset.drive);
    set(kEven, preset.bias);
    set(kSaturation, preset.saturation);
    set(kTone, preset.cutoffHz);
    set(kHold, preset.holdSeconds);
    set(kDecay, preset.decaySeconds);
    set(kAttack, preset.attackMs);
    set(kClick, juce::jmax(kClickOff, preset.clickDb));
    set(kBoost, preset.boostDb);
    apvts.state.setProperty("preset", currentPreset, nullptr);
}

Synth808Params Bass808Processor::currentParams() const
{
    Synth808Params params;
    params.name = Synth808Voice::presets()[static_cast<size_t>(currentPreset)].name;
    params.punchSemitones = paramValue(kPunch);
    params.punchMs = paramValue(kPunchTime);
    params.drive = paramValue(kDrive);
    params.bias = paramValue(kEven);
    params.saturation = paramValue(kSaturation);
    params.cutoffHz = paramValue(kTone);
    params.holdSeconds = paramValue(kHold);
    params.decaySeconds = paramValue(kDecay);
    params.attackMs = paramValue(kAttack);
    const float click = paramValue(kClick);
    params.clickDb = click <= kClickOff ? -90.0f : click;
    params.boostDb = paramValue(kBoost);
    return params;
}

void Bass808Processor::prepareToPlay(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto& voice : voices)
        voice.prepare(sampleRate);
    heldCount = 0;
    sounding = -1;
}

bool Bass808Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

double Bass808Processor::noteHz(int note) const
{
    return 440.0 * std::pow(2.0, (note + paramValue(kTune) - 69.0) / 12.0);
}

int Bass808Processor::msToSamples(float ms) const
{
    return juce::jmax(1, static_cast<int>(ms * 0.001 * sampleRate));
}

void Bass808Processor::startNote(int note, float velocity, bool fromHeldStack)
{
    const auto mode = static_cast<Mode>(static_cast<int>(paramValue(kMode)));
    auto& voice = voices[static_cast<size_t>(currentVoice)];
    if (sounding >= 0 && voice.isActive() && mode != Mode::Retrigger)
    {
        // Legato / Glide: the note goes on, its pitch moves (an 808 slide)
        voice.glideTo(noteHz(note), mode == Mode::Glide ? msToSamples(paramValue(kGlide)) : 0);
    }
    else if (!fromHeldStack)
    {
        voice.noteOff(msToSamples(3.0f)); // mono: the previous note chokes, no click
        currentVoice = 1 - currentVoice;
        const float sensitivity = paramValue(kVelocity);
        voices[static_cast<size_t>(currentVoice)].noteOn(noteHz(note), 1.0f - sensitivity + sensitivity * velocity);
    }
    else
    {
        return;
    }
    sounding = note;
}

void Bass808Processor::releaseNote(int note)
{
    int write = 0;
    for (int i = 0; i < heldCount; ++i)
        if (held[static_cast<size_t>(i)] != note)
            held[static_cast<size_t>(write++)] = held[static_cast<size_t>(i)];
    heldCount = write;
    if (note != sounding)
        return;
    if (heldCount > 0 && static_cast<Mode>(static_cast<int>(paramValue(kMode))) != Mode::Retrigger)
    {
        startNote(held[static_cast<size_t>(heldCount - 1)], lastVelocity, true); // back to the key still held
        return;
    }
    voices[static_cast<size_t>(currentVoice)].noteOff(msToSamples(paramValue(kRelease)));
    sounding = -1;
}

void Bass808Processor::handleMidi(const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        releaseNote(note); // a repeated key moves to the top of the stack
        if (heldCount == static_cast<int>(held.size()))
        {
            std::move(held.begin() + 1, held.end(), held.begin());
            --heldCount;
        }
        held[static_cast<size_t>(heldCount++)] = note;
        lastVelocity = message.getFloatVelocity();
        startNote(note, lastVelocity, false);
    }
    else if (message.isNoteOff())
    {
        releaseNote(message.getNoteNumber());
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        heldCount = 0;
        sounding = -1;
        for (auto& voice : voices)
            voice.noteOff(msToSamples(3.0f));
    }
}

void Bass808Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer(midi, 0, numSamples, true);

    const auto params = currentParams();
    const float gain = Synth808Voice::autoGain(params) * juce::Decibels::decibelsToGain(paramValue(kGain));
    for (auto& voice : voices)
        voice.setParams(params, gain);

    auto* out = buffer.getWritePointer(0);
    int position = 0;
    const auto renderTo = [&](int end)
    {
        for (auto& voice : voices)
            voice.renderAdd(out + position, end - position);
        position = end;
    };
    for (const auto metadata : midi)
    {
        renderTo(juce::jlimit(position, numSamples, metadata.samplePosition));
        handleMidi(metadata.getMessage());
    }
    renderTo(numSamples);
    for (int channel = 1; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom(channel, 0, buffer, 0, 0, numSamples);
    midi.clear();
}

juce::AudioProcessorEditor* Bass808Processor::createEditor()
{
    return new Bass808Editor(*this);
}

void Bass808Processor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void Bass808Processor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes); xml != nullptr && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        currentPreset = juce::jlimit(0, Synth808Voice::kPresetCount - 1, static_cast<int>(apvts.state.getProperty("preset", 0)));
    }
}
} // namespace bbg

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bbg::Bass808Processor();
}
