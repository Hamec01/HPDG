#include "TrapHatGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float tripletBias(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.74f;
        case TrapSubstyle::CloudTrap: return 0.96f;
        case TrapSubstyle::RageTrap: return 1.28f;
        case TrapSubstyle::MemphisTrap: return 1.1f;
        case TrapSubstyle::LuxuryTrap: return 0.88f;
        default: return 1.0f;
    }
}

float fastBurstBias(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.62f;
        case TrapSubstyle::CloudTrap: return 0.78f;
        case TrapSubstyle::RageTrap: return 1.42f;
        case TrapSubstyle::MemphisTrap: return 1.08f;
        case TrapSubstyle::LuxuryTrap: return 0.72f;
        default: return 1.0f;
    }
}
}

void TrapHatGenerator::generate(TrackState& track,
                                const GeneratorParams& params,
                                const TrapStyleProfile& style,
                                const std::vector<TrapPhraseRole>& phrase,
                                std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;

    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 145.0f, 100.0f, 132.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.82f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.68f;
    const float density = std::clamp(params.densityAmount * style.hatDensityBias * tempoScale, 0.12f, 1.0f);
    const float triBias = tripletBias(style.substyle);
    const float burstBias = fastBurstBias(style.substyle);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
    std::uniform_int_distribution<int> jitter(-18, 18);

    int carrierDivision = (density > 0.62f || style.substyle == TrapSubstyle::RageTrap || style.substyle == TrapSubstyle::MemphisTrap)
        ? HiResTiming::kTicks1_16
        : HiResTiming::kTicks1_8;
    if (tempoBand != TempoBand::Base && style.substyle != TrapSubstyle::RageTrap)
        carrierDivision = HiResTiming::kTicks1_8;
    const int move32Div = HiResTiming::kTicks1_32;
    const int move64Div = HiResTiming::kTicks1_64;
    const int tripletDiv = HiResTiming::kTicks1_24;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;
        const float roleBoost = TrapPhrasePlanner::roleVariationStrength(role);
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;

        for (int t = 0; t < HiResTiming::kTicksPerBar4_4; t += carrierDivision)
        {
            const int stepInBar = t / HiResTiming::kTicks1_16;

            float gate = 0.78f + density * 0.18f;
            if ((t % HiResTiming::kTicks1_8) == 0)
                gate += 0.08f;
            if (role == TrapPhraseRole::Lift)
                gate += 0.06f;
            if (role == TrapPhraseRole::Ending && stepInBar >= 12)
                gate += roleBoost * 0.12f;

            if (chance(rng) > std::clamp(gate, 0.2f, 0.99f))
                continue;

            int baseVel = vel(rng);
            if ((t % HiResTiming::kTicks1_8) == 0)
                baseVel = std::min(style.hatVelocityMax, baseVel + 8);

            HiResTiming::addNoteAtTick(track, pitch, barStart + t + jitter(rng), baseVel, false, bars);

            // 1/32 support motion
            float moveGate = (0.10f + density * 0.28f) * burstBias;
            if (role == TrapPhraseRole::Lift || role == TrapPhraseRole::Ending)
                moveGate += roleBoost * 0.08f;
            if (chance(rng) < std::clamp(moveGate, 0.03f, 0.84f))
            {
                const int dir = chance(rng) < 0.52f ? 1 : -1;
                const int tick = barStart + t + dir * move32Div;
                HiResTiming::addNoteAtTick(track,
                                           pitch,
                                           tick,
                                           std::max(style.hatVelocityMin, baseVel - 8),
                                           false,
                                           bars);
            }

            // Triplet-like 1/24 movement creates modern trap glide.
            float triGate = (0.06f + density * 0.18f) * triBias;
            if (role == TrapPhraseRole::Lift)
                triGate += 0.08f;
            if (chance(rng) < std::clamp(triGate, 0.01f, 0.58f))
            {
                const int tick = barStart + HiResTiming::quantizeTicks(t + tripletDiv, tripletDiv);
                HiResTiming::addNoteAtTick(track,
                                           pitch,
                                           tick,
                                           std::max(style.hatVelocityMin, baseVel - 10),
                                           false,
                                           bars);
            }

            // Short 1/64 drag before selected anchors to keep hats energetic.
            if ((t % HiResTiming::kTicks1_8) == 0 && chance(rng) < std::clamp((0.04f + density * 0.08f) * burstBias, 0.0f, 0.42f))
            {
                const int tick = barStart + t - move64Div;
                HiResTiming::addNoteAtTick(track,
                                           pitch,
                                           tick,
                                           std::max(style.hatVelocityMin, baseVel - 16),
                                           false,
                                           bars);
            }
        }

        // Phrase-ending flourish with a velocity ramp over fast 1/64 motion.
        if (role == TrapPhraseRole::Ending)
        {
            const int flourishStart = barStart + HiResTiming::kTicksPerBar4_4 - HiResTiming::kTicks1_8;
            int v = std::max(style.hatVelocityMin, style.hatVelocityMin + 4);
            for (int i = 0; i < 8; ++i)
            {
                const int tick = flourishStart + i * move64Div;
                v = std::min(style.hatVelocityMax, v + 5);
                HiResTiming::addNoteAtTick(track, pitch, tick, v, false, bars);
            }
        }
    }
}
} // namespace bbg
