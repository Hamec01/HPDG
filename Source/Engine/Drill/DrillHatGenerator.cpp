#include "DrillHatGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float substyleTripletBias(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::BrooklynDrill: return 1.1f;
        case DrillSubstyle::NYDrill: return 1.0f;
        case DrillSubstyle::DarkDrill: return 0.72f;
        case DrillSubstyle::UKDrill:
        default: return 0.9f;
    }
}

float substyleSupportBias(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::BrooklynDrill: return 1.22f;
        case DrillSubstyle::NYDrill: return 1.02f;
        case DrillSubstyle::DarkDrill: return 0.6f;
        case DrillSubstyle::UKDrill:
        default: return 0.82f;
    }
}
}

void DrillHatGenerator::generate(TrackState& track,
                                 const GeneratorParams& params,
                                 const DrillStyleProfile& style,
                                 const std::vector<DrillPhraseRole>& phrase,
                                 std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;

    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 100.0f, 130.0f);
    const bool halfTimeAware = tempoBand != TempoBand::Base;
    const float density = std::clamp(params.densityAmount * style.hatDensityBias, 0.15f, 1.0f);
    const float tripletBias = substyleTripletBias(style.substyle);
    const float supportBias = substyleSupportBias(style.substyle);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
    std::uniform_int_distribution<int> jitter(-14, 14);

    const int scaffoldDiv = halfTimeAware ? HiResTiming::kTicks1_8 : HiResTiming::kTicks1_16;
    const int tripletDiv = HiResTiming::kTicks1_24;
    const int move32 = HiResTiming::kTicks1_32;
    const int move64 = HiResTiming::kTicks1_64;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
        const float roleBoost = DrillPhrasePlanner::roleVariationStrength(role);
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;

        // Drill backbone uses skippy/tresillo-like anchors (3-3-2 feel blocks).
        for (int t = 0; t < HiResTiming::kTicksPerBar4_4; t += scaffoldDiv)
        {
            const int step = t / HiResTiming::kTicks1_16;
            const bool tresilloAnchor = (step % 8) == 0 || (step % 8) == 3 || (step % 8) == 6;
            float gate = tresilloAnchor ? 0.86f : (0.18f + density * (halfTimeAware ? 0.22f : 0.38f));
            if (halfTimeAware)
                gate *= supportBias;
            if (role == DrillPhraseRole::Tension)
                gate += roleBoost * 0.1f;
            if (role == DrillPhraseRole::Ending && step >= 12)
                gate += 0.12f;

            if (chance(rng) > std::clamp(gate, 0.06f, 0.96f))
                continue;

            int v = vel(rng);
            if (tresilloAnchor)
                v = std::min(style.hatVelocityMax, v + 7);
            HiResTiming::addNoteAtTick(track, pitch, barStart + t + jitter(rng), v, false, bars);

            // Structural skippy support.
            float supportGate = halfTimeAware ? (0.08f + density * 0.16f) : (0.12f + density * 0.22f);
            if (halfTimeAware)
                supportGate *= supportBias;
            if (tempoBand == TempoBand::Fast)
                supportGate += 0.04f;
            if (chance(rng) < std::clamp(supportGate, 0.01f, 0.84f))
            {
                const int tick = barStart + t + (chance(rng) < 0.52f ? move32 : -move32);
                HiResTiming::addNoteAtTick(track, pitch, tick, std::max(style.hatVelocityMin, v - 10), false, bars);
            }

            // Triplet-aware accents are integral to drill hat grammar.
            float triGate = (halfTimeAware ? 0.14f : 0.08f) + density * (halfTimeAware ? 0.2f : 0.18f);
            triGate *= tripletBias;
            if (tempoBand == TempoBand::Fast)
                triGate += 0.05f;
            if (role == DrillPhraseRole::Tension)
                triGate += 0.08f;
            if (chance(rng) < std::clamp(triGate, 0.02f, 0.64f))
            {
                const int triTick = barStart + HiResTiming::quantizeTicks(t + tripletDiv, tripletDiv);
                HiResTiming::addNoteAtTick(track, pitch, triTick, std::max(style.hatVelocityMin, v - 8), false, bars);
            }

            float dragGate = halfTimeAware ? (0.02f + density * 0.04f) : (0.04f + density * 0.06f);
            if (halfTimeAware && style.substyle == DrillSubstyle::DarkDrill)
                dragGate *= 0.65f;
            if (!tresilloAnchor && chance(rng) < std::clamp(dragGate, 0.0f, 0.32f))
            {
                const int drag = barStart + t - move64;
                HiResTiming::addNoteAtTick(track, pitch, drag, std::max(style.hatVelocityMin, v - 14), false, bars);
            }
        }

        // Phrase-edge structural fill.
        if (role == DrillPhraseRole::Ending)
        {
            const int fillStart = barStart + HiResTiming::kTicksPerBar4_4 - HiResTiming::kTicks1_8;
            int v = style.hatVelocityMax - 10;
            for (int i = 0; i < 6; ++i)
            {
                const int tick = fillStart + i * tripletDiv;
                v = std::max(style.hatVelocityMin, v - 2);
                HiResTiming::addNoteAtTick(track, pitch, tick, v, false, bars);
            }
        }
    }
}
} // namespace bbg
