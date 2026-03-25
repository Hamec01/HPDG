#include "TrapHatGenerator.h"

#include <algorithm>
#include <array>

#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
struct TrapHatMotifTemplate
{
    std::array<float, 8> slotBias {};
    std::array<float, 8> ghostBias {};
    std::array<float, 8> tripletBias {};
};

TrapHatMotifTemplate buildMotifTemplate(TrapSubstyle substyle, std::mt19937& rng)
{
    TrapHatMotifTemplate motif;
    std::uniform_real_distribution<float> spread(0.0f, 1.0f);

    for (int i = 0; i < 8; ++i)
    {
        float slot = 0.88f + spread(rng) * 0.32f;
        if ((i % 2) == 0)
            slot += 0.08f;

        float ghost = 0.88f + spread(rng) * 0.26f;
        float trip = 0.84f + spread(rng) * 0.32f;

        if (substyle == TrapSubstyle::DarkTrap || substyle == TrapSubstyle::LuxuryTrap)
        {
            if ((i % 4) == 1)
                slot *= 0.86f;
            trip *= 0.88f;
        }
        else if (substyle == TrapSubstyle::RageTrap)
        {
            if ((i % 4) == 3)
                slot *= 1.1f;
            ghost *= 1.08f;
            trip *= 1.1f;
        }

        motif.slotBias[static_cast<size_t>(i)] = std::clamp(slot, 0.72f, 1.34f);
        motif.ghostBias[static_cast<size_t>(i)] = std::clamp(ghost, 0.74f, 1.3f);
        motif.tripletBias[static_cast<size_t>(i)] = std::clamp(trip, 0.72f, 1.34f);
    }

    return motif;
}

float motifLookup(const std::array<float, 8>& values,
                  int slot,
                  int bar,
                  TrapPhraseRole role)
{
    int index = slot;
    if (role == TrapPhraseRole::Lift)
        index = (slot + 1) % 8;
    else if (role == TrapPhraseRole::Ending)
        index = (slot + 2) % 8;
    else if ((bar & 1) == 1)
        index = (slot + 7) % 8;

    return values[static_cast<size_t>(index)];
}

float tripletBias(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.72f;
        case TrapSubstyle::CloudTrap: return 1.08f;
        case TrapSubstyle::RageTrap: return 1.06f;
        case TrapSubstyle::MemphisTrap: return 1.14f;
        case TrapSubstyle::LuxuryTrap: return 0.86f;
        default: return 1.0f;
    }
}

float baseCarrierBias(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.84f;
        case TrapSubstyle::CloudTrap: return 0.9f;
        case TrapSubstyle::RageTrap: return 1.14f;
        case TrapSubstyle::MemphisTrap: return 0.94f;
        case TrapSubstyle::LuxuryTrap: return 0.88f;
        default: return 1.0f;
    }
}

int carrierDivisionForSpec(const TrapStyleSpec& spec, float density)
{
    switch (spec.hatDensityMode)
    {
        case TrapHatDensityMode::Sparse:
            return density > 0.56f ? HiResTiming::kTicks1_16 : HiResTiming::kTicks1_8;
        case TrapHatDensityMode::Dense:
            return HiResTiming::kTicks1_16;
        case TrapHatDensityMode::Medium:
        default:
            return density > 0.66f ? HiResTiming::kTicks1_16 : HiResTiming::kTicks1_8;
    }
}

int maxBaseNotesPerBar(const TrapStyleSpec& spec)
{
    switch (spec.hatDensityMode)
    {
        case TrapHatDensityMode::Sparse: return 14;
        case TrapHatDensityMode::Dense: return 32;
        case TrapHatDensityMode::Medium:
        default:
            return 22;
    }
}

int accentVelocity(std::mt19937& rng)
{
    std::uniform_int_distribution<int> accentVel(85, 96);
    return accentVel(rng);
}

int baseVelocity(std::mt19937& rng)
{
    std::uniform_int_distribution<int> baseVel(58, 84);
    return baseVel(rng);
}

int ghostVelocity(std::mt19937& rng)
{
    std::uniform_int_distribution<int> ghostVel(35, 55);
    return ghostVel(rng);
}

int carrierJitter(TrapSubstyle substyle,
                  TrapPhraseRole role,
                  bool isBeatAnchor,
                  std::mt19937& rng)
{
    int minJ = -3;
    int maxJ = 4;

    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: minJ = -3; maxJ = 4; break;
        case TrapSubstyle::DarkTrap: minJ = -2; maxJ = 4; break;
        case TrapSubstyle::CloudTrap: minJ = -4; maxJ = 5; break;
        case TrapSubstyle::RageTrap: minJ = -2; maxJ = 3; break;
        case TrapSubstyle::MemphisTrap: minJ = -4; maxJ = 6; break;
        case TrapSubstyle::LuxuryTrap: minJ = -2; maxJ = 2; break;
        default: break;
    }

    if (isBeatAnchor)
    {
        minJ = std::max(minJ, -2);
        maxJ = std::min(maxJ, 2);
    }

    if (role == TrapPhraseRole::Ending)
    {
        minJ = std::max(minJ, -2);
        maxJ = std::min(maxJ + 1, 5);
    }

    std::uniform_int_distribution<int> dist(minJ, maxJ);
    return dist(rng);
}

int smoothCarrierVelocity(int desired,
                         int previous,
                         bool isBeatAnchor,
                         TrapSubstyle substyle)
{
    if (previous < 0)
        return desired;

    int downLimit = previous - 18;
    int upLimit = previous + 16;
    if (substyle == TrapSubstyle::LuxuryTrap)
    {
        downLimit = previous - 12;
        upLimit = previous + 10;
    }
    else if (substyle == TrapSubstyle::MemphisTrap)
    {
        downLimit = previous - 22;
        upLimit = previous + 20;
    }

    int smoothed = std::clamp(desired, downLimit, upLimit);
    if (isBeatAnchor)
        smoothed = std::max(smoothed, previous - 2);

    return smoothed;
}

float hiHatActivityWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(laneBiasFor(styleInfluence, TrackType::HiHat).activityWeight, 0.55f, 1.55f);
}

float bounceWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.bounceWeight, 0.65f, 1.5f);
}
}

void TrapHatGenerator::generate(TrackState& track,
                                const GeneratorParams& params,
                                const TrapStyleProfile& style,
                                const StyleInfluenceState& styleInfluence,
                                const std::vector<TrapPhraseRole>& phrase,
                                std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const auto spec = getTrapStyleSpec(style.substyle);

    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 145.0f, 100.0f, 132.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.82f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.68f;
    const float unclampedDensity = params.densityAmount * style.hatDensityBias * tempoScale;
    const float hatActivity = hiHatActivityWeight(styleInfluence);
    const float density = std::clamp(unclampedDensity * hatActivity, spec.hatDensityMin, spec.hatDensityMax);
    const float triBias = tripletBias(style.substyle);
    const float bounce = bounceWeight(styleInfluence);
    const float carrierBias = baseCarrierBias(style.substyle) * (0.9f + bounce * 0.1f);
    const auto motif = buildMotifTemplate(style.substyle, rng);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    int carrierDivision = carrierDivisionForSpec(spec, density);
    if (hatActivity <= 0.85f)
        carrierDivision = HiResTiming::kTicks1_8;
    else if (hatActivity >= 1.18f && spec.hatDensityMode != TrapHatDensityMode::Sparse)
        carrierDivision = HiResTiming::kTicks1_16;
    if (tempoBand != TempoBand::Base && style.substyle != TrapSubstyle::RageTrap)
        carrierDivision = HiResTiming::kTicks1_8;

    const int move32 = HiResTiming::kTicks1_32;
    const int tripletDiv = HiResTiming::kTicks1_24;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;
        const float roleBoost = TrapPhrasePlanner::roleVariationStrength(role);
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        int notesInBar = 0;
        const int maxNotes = std::max(6,
                          static_cast<int>(std::round(static_cast<float>(maxBaseNotesPerBar(spec))
                                      * std::clamp(0.55f + density * 0.55f + (bounce - 1.0f) * 0.25f,
                                               0.42f,
                                               1.45f))));
        int lastCarrierVel = -1;

        for (int t = 0; t < HiResTiming::kTicksPerBar4_4; t += carrierDivision)
        {
            if (notesInBar >= maxNotes)
                break;

            const int stepInBar = t / HiResTiming::kTicks1_16;
            const bool isBeatAnchor = (t % HiResTiming::kTicks1_8) == 0;
            const int motifSlot = std::clamp(stepInBar / 2, 0, 7);
            float gate = 0.62f + density * 0.3f;
            if (isBeatAnchor)
                gate += 0.18f;
            if (role == TrapPhraseRole::Lift)
                gate += 0.06f;
            if (role == TrapPhraseRole::Ending && stepInBar >= 12)
                gate += roleBoost * 0.08f;
            gate *= carrierBias;
            gate *= motifLookup(motif.slotBias, motifSlot, bar, role);

            // Sparse styles intentionally leave breathing pockets.
            if (spec.hatDensityMode == TrapHatDensityMode::Sparse && !isBeatAnchor && (stepInBar % 4) == 2)
                gate *= 0.64f;

            // Keep tiny omissions in the carrier so HatFX can become the energy lane.
            if (!isBeatAnchor && chance(rng) < 0.07f)
                gate *= 0.4f;

            if (chance(rng) > std::clamp(gate, 0.2f, 0.99f))
                continue;

            int v = baseVelocity(rng);
            if (isBeatAnchor)
                v = std::max(v, accentVelocity(rng));

            if (!isBeatAnchor && carrierDivision == HiResTiming::kTicks1_16)
            {
                // Light hand-alternation feel to avoid robotic same-level runs.
                v += ((stepInBar & 1) == 0) ? 2 : -4;
            }

            v = smoothCarrierVelocity(v, lastCarrierVel, isBeatAnchor, style.substyle);
            v = std::clamp(v, style.hatVelocityMin, style.hatVelocityMax);
            lastCarrierVel = v;

            HiResTiming::addNoteAtTick(track,
                                       pitch,
                                       barStart + t + carrierJitter(style.substyle, role, isBeatAnchor, rng),
                                       v,
                                       false,
                                       bars);
            ++notesInBar;

            // Small doubles keep the base moving without turning it into a burst lane.
            float tinyDoubleGate = (0.05f + density * 0.1f) * (0.86f + bounce * 0.14f);
            if (spec.hatDensityMode == TrapHatDensityMode::Dense)
                tinyDoubleGate += 0.04f;
            tinyDoubleGate *= motifLookup(motif.ghostBias, motifSlot, bar, role);
            if (chance(rng) < std::clamp(tinyDoubleGate, 0.01f, 0.26f))
            {
                const int tick = barStart + t + move32;
                HiResTiming::addNoteAtTick(track,
                                           pitch,
                                           tick,
                                           std::clamp(ghostVelocity(rng), 28, std::min(72, style.hatVelocityMax)),
                                           true,
                                           bars);
                ++notesInBar;
            }

            // Light triplet glides belong to base for Cloud/Memphis flavor, not full rolls.
            float triGate = (0.03f + density * 0.07f) * triBias;
            if (role == TrapPhraseRole::Lift)
                triGate += 0.05f;
            triGate *= motifLookup(motif.tripletBias, motifSlot, bar, role);
            if (chance(rng) < std::clamp(triGate, 0.0f, 0.24f))
            {
                const int tick = barStart + HiResTiming::quantizeTicks(t + tripletDiv, tripletDiv);
                HiResTiming::addNoteAtTick(track,
                                           pitch,
                                           tick,
                                           std::clamp(ghostVelocity(rng), 28, std::min(72, style.hatVelocityMax)),
                                           true,
                                           bars);
                ++notesInBar;
            }
        }

        // Phrase punctuation in base lane stays short and readable.
        if (role == TrapPhraseRole::Ending && notesInBar < maxNotes)
        {
            const int punctTick = barStart + HiResTiming::kTicksPerBar4_4 - HiResTiming::kTicks1_16;
            HiResTiming::addNoteAtTick(track,
                                       pitch,
                                       punctTick,
                                       std::clamp(accentVelocity(rng), style.hatVelocityMin, style.hatVelocityMax),
                                       false,
                                       bars);
        }
    }
}
} // namespace bbg
