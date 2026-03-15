#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace bbg
{
struct TransportSnapshot
{
    bool hasHostTempo = false;
    double bpm = 120.0;

    bool hasPpq = false;
    double ppqPosition = 0.0;

    bool isPlaying = false;

    int numerator = 4;
    int denominator = 4;

    static TransportSnapshot fromPlayHead(juce::AudioPlayHead* playHead)
    {
        TransportSnapshot s;

        if (playHead == nullptr)
            return s;

        auto position = playHead->getPosition();

        if (!position.hasValue())
            return s;

        const auto& pos = *position;

        if (auto hostBpm = pos.getBpm(); hostBpm.hasValue())
        {
            s.hasHostTempo = true;
            s.bpm = *hostBpm;
        }

        if (auto ppq = pos.getPpqPosition(); ppq.hasValue())
        {
            s.hasPpq = true;
            s.ppqPosition = *ppq;
        }

        s.isPlaying = pos.getIsPlaying();

        if (auto sig = pos.getTimeSignature(); sig.hasValue())
        {
            s.numerator = sig->numerator;
            s.denominator = sig->denominator;
        }

        return s;
    }
};
} // namespace bbg
