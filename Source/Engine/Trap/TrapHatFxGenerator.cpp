#include "TrapHatFxGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"

namespace bbg
{
namespace
{
float fxDensityBias(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.66f;
        case TrapSubstyle::CloudTrap: return 0.8f;
        case TrapSubstyle::RageTrap: return 1.46f;
        case TrapSubstyle::MemphisTrap: return 1.14f;
        case TrapSubstyle::LuxuryTrap: return 0.72f;
        default: return 1.0f;
    }
}

void addTrapRoll32(TrackState& track, int pitch, int startTick, int bars, int baseVel)
{
    for (int i = 0; i < 4; ++i)
        HiResTiming::addNoteAtTick(track, pitch, startTick + i * HiResTiming::kTicks1_32, baseVel - i * 3, true, bars);
}

void addTrapBurst64(TrackState& track, int pitch, int startTick, int bars, int baseVel)
{
    for (int i = 0; i < 6; ++i)
        HiResTiming::addNoteAtTick(track, pitch, startTick + i * HiResTiming::kTicks1_64, std::max(1, baseVel - i * 2), true, bars);
}

void addTrapRatchet(TrackState& track, int pitch, int startTick, int bars, int baseVel)
{
    for (int i = 0; i < 3; ++i)
        HiResTiming::addNoteAtTick(track, pitch, startTick + i * (HiResTiming::kTicks1_24 / 2), std::max(1, baseVel - i * 4), true, bars);
}

void addTrapVelocityRamp(TrackState& track, int pitch, int startTick, int bars, int minVel, int maxVel)
{
    int v = std::max(1, minVel);
    const int step = std::max(1, (std::max(v + 1, maxVel) - v) / 5);
    for (int i = 0; i < 6; ++i)
    {
        v = std::min(maxVel, v + step);
        HiResTiming::addNoteAtTick(track, pitch, startTick + i * HiResTiming::kTicks1_32, v, true, bars);
    }
}
}

void TrapHatFxGenerator::generate(TrackState& hatFxTrack,
                                  const TrackState& hatTrack,
                                  const TrapStyleProfile& style,
                                  float fxIntensity,
                                  const std::vector<TrapPhraseRole>& phrase,
                                  std::mt19937& rng) const
{
    hatFxTrack.notes.clear();
    const auto* info = TrackRegistry::find(hatFxTrack.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 44;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
    std::uniform_int_distribution<int> fillPicker(0, 3);
    const int bars = std::max(1, static_cast<int>((hatTrack.notes.empty() ? 1 : (hatTrack.notes.back().step / 16) + 1)));

    const float baseFx = std::clamp(fxIntensity * style.hatFxIntensity * fxDensityBias(style.substyle), 0.0f, 1.0f);

    for (const auto& h : hatTrack.notes)
    {
        const int bar = h.step / 16;
        const int stepInBar = h.step % 16;
        const int sourceTick = HiResTiming::noteTick(h);
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;

        float opGate = 0.12f + baseFx * 0.48f;
        if (role == TrapPhraseRole::Lift)
            opGate += 0.1f;
        if (role == TrapPhraseRole::Ending && stepInBar >= 12)
            opGate += 0.24f;

        if (chance(rng) > std::clamp(opGate, 0.02f, 0.92f))
            continue;

        const int baseVel = vel(rng);
        int fill = fillPicker(rng);
        if (style.substyle == TrapSubstyle::DarkTrap && fill == 1)
            fill = 0;
        if (style.substyle == TrapSubstyle::CloudTrap && fill == 1)
            fill = 3;
        if (style.substyle == TrapSubstyle::LuxuryTrap && fill == 2)
            fill = 3;
        if (style.substyle == TrapSubstyle::RageTrap && fill == 3)
            fill = 1;

        if (fill == 0)
            addTrapRoll32(hatFxTrack, pitch, sourceTick, bars, baseVel);
        else if (fill == 1)
            addTrapBurst64(hatFxTrack, pitch, sourceTick - HiResTiming::kTicks1_64, bars, baseVel);
        else if (fill == 2)
            addTrapRatchet(hatFxTrack, pitch, sourceTick, bars, baseVel);
        else
            addTrapVelocityRamp(hatFxTrack, pitch, sourceTick - HiResTiming::kTicks1_32, bars, style.hatVelocityMin, baseVel + 14);

        // Additional triplet burst accent on phrase edges.
        float endingBurst = 0.7f;
        if (style.substyle == TrapSubstyle::DarkTrap)
            endingBurst = 0.42f;
        else if (style.substyle == TrapSubstyle::RageTrap)
            endingBurst = 0.84f;
        else if (style.substyle == TrapSubstyle::LuxuryTrap)
            endingBurst = 0.52f;
        if (role == TrapPhraseRole::Ending && stepInBar >= 12 && chance(rng) < endingBurst)
        {
            for (int i = 0; i < 3; ++i)
            {
                HiResTiming::addNoteAtTick(hatFxTrack,
                                           pitch,
                                           sourceTick + i * HiResTiming::kTicks1_24,
                                           std::max(style.hatVelocityMin, baseVel - i * 4),
                                           true,
                                           bars);
            }
        }
    }
}
} // namespace bbg
