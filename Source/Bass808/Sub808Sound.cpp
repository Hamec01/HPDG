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
        // HamloMag's auto gain keeps small signals at unity, so more drive came out quieter: 15-23 dB down at
        // full DRIVE (held C2, bank sample and the 808 presets, Tests/Sub808EngineCheck.cpp). Its measured
        // loss per model at DRIVE 0 / 0.25 / 0.5 / 0.75 / 1 is put back, so DRIVE changes the tone, not the level.
        static constexpr float kLossDb[6][5] {
            { 0.0f, -2.95f, -6.75f, -11.95f, -17.6f },  // Classic
            { 0.0f, -1.05f, -4.80f, -10.00f, -15.75f }, // Tape
            { 0.0f, -2.25f, -5.20f, -9.75f, -15.3f },   // Tube
            { 0.0f, -5.90f, -8.90f, -12.95f, -17.9f },  // Iron
            { 0.0f, -2.55f, -6.85f, -12.25f, -17.95f }, // Germanium
            { 0.0f, -3.05f, -7.05f, -12.45f, -20.65f }, // Vinyl
        };
        const int model = juce::jlimit(1, 6, s.satModel == 0 ? strip.driveModel : s.satModel) - 1;
        const float position = drive * 4.0f;
        const int step = juce::jmin(3, static_cast<int>(position));
        const float loss = kLossDb[model][step] + (kLossDb[model][step + 1] - kLossDb[model][step]) * (position - static_cast<float>(step));
        s.gainDb -= loss;

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
    sound->sidechain = juce::jlimit(0.0f, 1.0f, state.sidechain);
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
