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
        // The saturator's auto gain keeps small signals at unity, so more drive came out quieter: 13-18 dB down
        // at full DRIVE (held C2, the bank sample and ATL 1, Tests/Sub808EngineCheck.cpp). Its measured loss per
        // model at DRIVE 0 / 0.25 / 0.5 / 0.75 / 1 is put back, so DRIVE changes the tone, not the level.
        static constexpr float kLossDb[6][5] {
            { 0.0f, -2.70f, -6.25f, -10.55f, -13.85f }, // Classic
            { 0.0f, -2.90f, -6.35f, -10.45f, -13.60f }, // Tape
            { 0.0f, -2.10f, -4.90f, -8.95f, -12.80f },  // Tube
            { 0.0f, -6.55f, -10.20f, -14.70f, -18.05f }, // Iron
            { 0.0f, -2.35f, -6.40f, -10.75f, -13.95f }, // Germanium
            { 0.0f, -2.85f, -6.55f, -10.90f, -14.65f }, // Vinyl
        };
        const int model = juce::jlimit(1, 6, s.satModel == 0 ? strip.driveModel : s.satModel) - 1;
        // the loss at a saturator drive in dB (the table's steps are 6 dB apart; beyond 24 dB its last slope)
        const auto lossAt = [model](float driveDb)
        {
            const float position = juce::jmax(0.0f, driveDb) / 6.0f;
            const int step = juce::jmin(3, static_cast<int>(position));
            return kLossDb[model][step] + (kLossDb[model][step + 1] - kLossDb[model][step]) * (position - static_cast<float>(step));
        };
        // a sound with its own saturator already has its own drive's loss: only the added drive's is put back
        const float ownDb = s.satModel == 0 ? 0.0f : s.satDriveDb;
        const float drivenDb = s.satModel == 0 ? 24.0f * drive : juce::jmin(36.0f, s.satDriveDb + 18.0f * drive);
        s.gainDb -= lossAt(drivenDb) - lossAt(ownDb);

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
