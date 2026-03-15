#include "DrillHatFxGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"

namespace bbg
{
namespace
{
void addTripletAccentFill(TrackState& track, int pitch, int startTick, int bars, int baseVel)
{
    for (int i = 0; i < 4; ++i)
        HiResTiming::addNoteAtTick(track, pitch, startTick + i * HiResTiming::kTicks1_24, std::max(1, baseVel - i * 3), true, bars);
}

void addSkippyBurst(TrackState& track, int pitch, int startTick, int bars, int baseVel)
{
    const int offsets[] = { 0, HiResTiming::kTicks1_64, HiResTiming::kTicks1_32, HiResTiming::kTicks1_32 + HiResTiming::kTicks1_64 };
    for (int i = 0; i < 4; ++i)
        HiResTiming::addNoteAtTick(track, pitch, startTick + offsets[i], std::max(1, baseVel - i * 2), true, bars);
}

void addTresilloFill(TrackState& track, int pitch, int startTick, int bars, int baseVel)
{
    const int tresilloOffsets[] = { 0, HiResTiming::kTicks1_24 * 3, HiResTiming::kTicks1_24 * 6 };
    for (int i = 0; i < 3; ++i)
        HiResTiming::addNoteAtTick(track, pitch, startTick + tresilloOffsets[i], std::max(1, baseVel - i * 4), true, bars);
}

void addDarkEdgeCluster(TrackState& track, int pitch, int startTick, int bars, int baseVel)
{
    for (int i = 0; i < 5; ++i)
        HiResTiming::addNoteAtTick(track, pitch, startTick + i * HiResTiming::kTicks1_64, std::max(1, baseVel - 8 - i), true, bars);
}
}

void DrillHatFxGenerator::generate(TrackState& hatFxTrack,
                                   const TrackState& hatTrack,
                                   const DrillStyleProfile& style,
                                   float fxIntensity,
                                   const std::vector<DrillPhraseRole>& phrase,
                                   std::mt19937& rng) const
{
    hatFxTrack.notes.clear();
    const auto* info = TrackRegistry::find(hatFxTrack.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 44;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
    std::uniform_int_distribution<int> fillPicker(0, 3);
    const int bars = std::max(1, static_cast<int>((hatTrack.notes.empty() ? 1 : (hatTrack.notes.back().step / 16) + 1)));

    const float intensity = std::clamp(fxIntensity * style.hatFxIntensity, 0.0f, 1.0f);

    for (const auto& h : hatTrack.notes)
    {
        const int bar = h.step / 16;
        const int stepInBar = h.step % 16;
        const int sourceTick = HiResTiming::noteTick(h);
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;

        float gate = 0.10f + intensity * 0.42f;
        if (role == DrillPhraseRole::Tension)
            gate += 0.12f;
        if (role == DrillPhraseRole::Ending && stepInBar >= 12)
            gate += 0.2f;

        if (chance(rng) > std::clamp(gate, 0.02f, 0.88f))
            continue;

        const int baseVel = vel(rng);
        const int fill = fillPicker(rng);
        if (fill == 0)
            addTripletAccentFill(hatFxTrack, pitch, sourceTick, bars, baseVel);
        else if (fill == 1)
            addSkippyBurst(hatFxTrack, pitch, sourceTick - HiResTiming::kTicks1_64, bars, baseVel);
        else if (fill == 2)
            addTresilloFill(hatFxTrack, pitch, sourceTick, bars, baseVel - 2);
        else
            addDarkEdgeCluster(hatFxTrack, pitch, sourceTick, bars, baseVel - 4);

        if (role == DrillPhraseRole::Ending && stepInBar >= 12 && chance(rng) < 0.66f)
        {
            addDarkEdgeCluster(hatFxTrack,
                               pitch,
                               sourceTick + HiResTiming::kTicks1_32,
                               bars,
                               std::max(style.hatVelocityMin, baseVel - 8));
        }
    }
}
} // namespace bbg
