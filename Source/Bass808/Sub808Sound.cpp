#include "Sub808Sound.h"

#include <cmath>

#include "Bass808/BassSettingsIO.h"

namespace bbg
{
BassSettings sub808SampleOnlySettings()
{
    BassSettings s;
    s.synthLevelDb = -60.0f;
    s.clickDb = -60.0f;
    s.sampleLevelDb = 0.0f;
    s.sampleFollow = true;
    s.attackMs = 0.0f;
    s.holdSeconds = 0.0f;
    s.decaySeconds = 60.0f;
    s.sustain = 1.0f;
    s.releaseMs = 6.0f;
    s.filterType = BassSettings::LP12;
    s.cutoffHz = 20000.0f;
    s.satModel = 0;
    s.phantom = 0.0f;
    s.mode = BassSettings::Retrigger;
    s.velocity = 1.0f;
    return s;
}

BassSettings applySub808Strip(BassSettings s, const Sub808SoundState& strip)
{
    s.velocity = 1.0f;
    s.busFilter = juce::jlimit(-1.0f, 1.0f, strip.filter);

    const float drive = juce::jlimit(0.0f, 1.0f, strip.drive);
    if (drive > 0.001f)
    {
        if (s.satModel == 0)
        {
            s.satModel = juce::jlimit(1, 6, strip.driveModel);
            s.satDriveDb = 24.0f * drive;
            s.satHarmonics = 0.5f;
            s.satEvenOdd = 0.0f;
            s.satMix = 1.0f;
        }
        else
        {
            s.satDriveDb = juce::jmin(36.0f, s.satDriveDb + 18.0f * drive);
        }
    }

    const float length = juce::jlimit(0.0f, 1.0f, strip.length);
    if (length < 0.999f)
    {
        // 40 ms .. 4 s per -6 dB; the sustain goes with it, so a held sample shortens too
        const float seconds = 0.04f * std::pow(100.0f, length);
        s.decaySeconds = juce::jmin(s.decaySeconds, seconds);
        s.holdSeconds = juce::jmin(s.holdSeconds, 0.5f * seconds);
        s.sustain *= std::pow(length, 4.0f);
    }

    s.gainDb += juce::jlimit(-24.0f, 12.0f, strip.gainDb);
    return s;
}

std::shared_ptr<const Sub808Sound> buildSub808Sound(const Sub808SoundState& state,
                                                    std::shared_ptr<const juce::AudioBuffer<float>> bankBuffer,
                                                    double bankRate,
                                                    double bankRootHz,
                                                    const juce::String& bankName)
{
    auto sound = std::make_shared<Sub808Sound>();
    if (state.custom)
    {
        sound->settings = applySub808Strip(bassSettingsFromTree(state.settings), state);
        if (state.sampleWav.getSize() > 0)
            sound->sample = BassSample::fromWavMemory(state.sampleWav, state.name, {});
        return sound;
    }

    if (bankBuffer == nullptr || bankBuffer->getNumSamples() <= 0 || bankRate < 1000.0)
        return nullptr;

    // the bank sample as it is: no trim, no normalising (its level and timing stay what they were)
    auto sample = std::make_shared<BassSample>();
    const int channels = bankBuffer->getNumChannels();
    const int length = bankBuffer->getNumSamples();
    sample->data.resize(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
        sample->data[static_cast<size_t>(i)] = channels <= 1
            ? bankBuffer->getSample(0, i)
            : 0.5f * (bankBuffer->getSample(0, i) + bankBuffer->getSample(1, i));
    sample->sampleRate = bankRate;
    sample->rootHz = bankRootHz;
    sample->name = bankName;
    sound->sample = std::move(sample);
    sound->settings = applySub808Strip(sub808SampleOnlySettings(), state);
    sound->bankSource = std::move(bankBuffer);
    return sound;
}
} // namespace bbg
