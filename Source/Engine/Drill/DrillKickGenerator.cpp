#include "DrillKickGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
struct KickRoleCaps
{
    int maxSupportPerBar = 1;
    int maxPhraseEdgePerBar = 1;
    int maxTotalPerBar = 3;
};

int minHitsPerBar(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::BrooklynDrill: return 2;
        case DrillSubstyle::NYDrill: return 2;
        case DrillSubstyle::DarkDrill: return 1;
        case DrillSubstyle::UKDrill: return 2;
    }

    return 1;
}

int maxHitsPerBar(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::BrooklynDrill: return 4;
        case DrillSubstyle::NYDrill: return 3;
        case DrillSubstyle::DarkDrill: return 2;
        case DrillSubstyle::UKDrill: return 3;
    }

    return 4;
}

KickRoleCaps kickRoleCaps(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::UKDrill: return { 1, 1, 3 };
        case DrillSubstyle::BrooklynDrill: return { 2, 1, 4 };
        case DrillSubstyle::NYDrill: return { 2, 1, 3 };
        case DrillSubstyle::DarkDrill: return { 1, 1, 2 };
    }

    return { 1, 1, 3 };
}

bool isPhraseEdgeStep(int stepInBar)
{
    return stepInBar >= 13;
}

int eventWindowIndex(int stepInBar)
{
    return std::clamp(stepInBar / 4, 0, 3);
}

DrillKickEventType classifyKickRole(int stepInBar, const DrillGrooveSlot* slot)
{
    if (stepInBar == 0 || stepInBar == 10)
        return DrillKickEventType::AnchorKick;
    if ((slot != nullptr && slot->phraseEdgeWeight >= 0.7f) || isPhraseEdgeStep(stepInBar))
        return DrillKickEventType::PhraseEdgeKick;
    return DrillKickEventType::SupportKick;
}

std::vector<int> candidateSteps(DrillSubstyle substyle, bool halfTimeAware)
{
    if (!halfTimeAware)
    {
        switch (substyle)
        {
            case DrillSubstyle::UKDrill: return { 0, 10, 6, 15 };
            case DrillSubstyle::BrooklynDrill: return { 0, 10, 6, 13, 15 };
            case DrillSubstyle::NYDrill: return { 0, 10, 6, 13 };
            case DrillSubstyle::DarkDrill: return { 0, 10, 15 };
        }
    }

    switch (substyle)
    {
        case DrillSubstyle::UKDrill: return { 0, 10, 6, 15 };
        case DrillSubstyle::BrooklynDrill: return { 0, 10, 6, 13, 15 };
        case DrillSubstyle::NYDrill: return { 0, 10, 6, 13 };
        case DrillSubstyle::DarkDrill: return { 0, 10, 15 };
    }

    return { 0, 6, 10, 15 };
}

float stepWeight(DrillSubstyle substyle, int stepInBar)
{
    switch (substyle)
    {
        case DrillSubstyle::UKDrill:
            if (stepInBar == 0) return 1.0f;
            if (stepInBar == 10) return 0.9f;
            if (stepInBar == 6 || stepInBar == 15) return 0.62f;
            return 0.4f;
        case DrillSubstyle::BrooklynDrill:
            if (stepInBar == 0) return 1.0f;
            if (stepInBar == 10 || stepInBar == 13) return 0.82f;
            if (stepInBar == 6 || stepInBar == 15) return 0.74f;
            return 0.5f;
        case DrillSubstyle::NYDrill:
            if (stepInBar == 0) return 1.0f;
            if (stepInBar == 10) return 0.82f;
            if (stepInBar == 6 || stepInBar == 13 || stepInBar == 15) return 0.64f;
            return 0.42f;
        case DrillSubstyle::DarkDrill:
            if (stepInBar == 0) return 1.0f;
            if (stepInBar == 10) return 0.72f;
            if (stepInBar == 15) return 0.46f;
            return 0.24f;
    }

    return 0.5f;
}

float anchorRigidityWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.anchorRigidityWeight, 0.7f, 1.5f);
}

const ReferenceKickBarPattern* referenceKickBarFor(const ReferenceKickPattern* pattern, int bar)
{
    if (pattern == nullptr || !pattern->available || pattern->barPatterns.empty())
        return nullptr;

    const int sourceBars = std::max(1, pattern->sourceBars > 0 ? pattern->sourceBars : static_cast<int>(pattern->barPatterns.size()));
    const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
    if (normalizedBar < 0 || normalizedBar >= static_cast<int>(pattern->barPatterns.size()))
        return nullptr;
    return &pattern->barPatterns[static_cast<size_t>(normalizedBar)];
}

bool hasReferenceKickCorpus(const StyleInfluenceState& styleInfluence)
{
    return styleInfluence.referenceKickCorpus.available
        && !styleInfluence.referenceKickCorpus.variants.empty();
}

struct ReferenceKickStepProfile
{
    bool available = false;
    float density = 0.0f;
    std::array<float, 16> presence {};
    std::array<float, 16> velocityBias {};
};

ReferenceKickStepProfile buildReferenceKickStepProfile(const ReferenceKickCorpus* corpus, int bar)
{
    ReferenceKickStepProfile profile;
    if (corpus == nullptr || !corpus->available || corpus->variants.empty())
        return profile;

    int contributingBars = 0;
    float totalNotes = 0.0f;
    for (const auto& variant : corpus->variants)
    {
        const auto* barPattern = referenceKickBarFor(&variant, bar);
        if (barPattern == nullptr)
            continue;

        ++contributingBars;
        totalNotes += static_cast<float>(barPattern->notes.size());
        for (const auto& note : barPattern->notes)
        {
            const int step = std::clamp(note.step16, 0, 15);
            profile.presence[static_cast<size_t>(step)] += 1.0f;
            profile.velocityBias[static_cast<size_t>(step)] += static_cast<float>(std::clamp(note.velocity, 1, 127)) / 127.0f;
        }
    }

    if (contributingBars <= 0)
        return profile;

    profile.available = true;
    const float invBars = 1.0f / static_cast<float>(contributingBars);
    for (size_t index = 0; index < profile.presence.size(); ++index)
    {
        profile.presence[index] *= invBars;
        profile.velocityBias[index] = profile.presence[index] > 0.0f
            ? std::clamp(profile.velocityBias[index] * invBars, 0.0f, 1.0f)
            : 0.0f;
    }
    profile.density = std::clamp((totalNotes * invBars) / 4.0f, 0.0f, 1.0f);
    return profile;
}

std::vector<int> referenceKickLeadSteps(const ReferenceKickStepProfile& profile)
{
    std::vector<std::pair<float, int>> ranked;
    for (int step = 0; step < 16; ++step)
    {
        if (profile.presence[static_cast<size_t>(step)] < 0.34f)
            continue;
        ranked.push_back({ profile.presence[static_cast<size_t>(step)] + profile.velocityBias[static_cast<size_t>(step)] * 0.12f, step });
    }

    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right)
    {
        if (left.first != right.first)
            return left.first > right.first;
        return left.second < right.second;
    });

    std::vector<int> steps;
    for (const auto& item : ranked)
        steps.push_back(item.second);
    return steps;
}

std::vector<int> buildUKKickStepsFromCorpus(const ReferenceKickCorpus* corpus,
                                            int bar,
                                            float rigidity,
                                            std::mt19937& rng)
{
    std::vector<const ReferenceKickBarPattern*> candidates;
    if (corpus == nullptr)
        return { 0, 10 };

    for (const auto& variant : corpus->variants)
    {
        if (const auto* barPattern = referenceKickBarFor(&variant, bar); barPattern != nullptr && !barPattern->notes.empty())
            candidates.push_back(barPattern);
    }

    if (candidates.empty())
        return { 0, 10 };

    std::array<int, 16> counts {};
    std::array<int, 16> velocitySums {};
    for (const auto* candidate : candidates)
    {
        for (const auto& note : candidate->notes)
        {
            const int step = std::clamp(note.step16, 0, 15);
            ++counts[static_cast<size_t>(step)];
            velocitySums[static_cast<size_t>(step)] += note.velocity;
        }
    }

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::vector<int> selected { 0 };
    if (counts[10] > 0 && chance(rng) < std::clamp(static_cast<float>(counts[10]) / static_cast<float>(candidates.size()) + 0.2f, 0.55f, 1.0f))
        selected.push_back(10);

    for (const int step : { 6, 13, 15, 4, 8, 12 })
    {
        const float freq = static_cast<float>(counts[step]) / static_cast<float>(candidates.size());
        if (freq <= 0.0f)
            continue;

        float gate = freq;
        if (step == 6 || step == 15 || step == 13)
            gate += 0.10f;
        gate *= std::clamp(1.28f - rigidity * 0.22f, 0.68f, 1.1f);
        gate = std::clamp(gate, 0.0f, 0.92f);
        if (chance(rng) < gate)
            selected.push_back(step);
        if (static_cast<int>(selected.size()) >= 4)
            break;
    }

    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

    if (selected.size() <= 2 && counts[6] > 0 && chance(rng) < 0.55f)
        selected.push_back(6);

    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

    if (selected.size() > 4)
        selected.resize(4);

    return selected;
}
}

void DrillKickGenerator::generate(TrackState& track,
                                  const GeneratorParams& params,
                                  const DrillStyleProfile& style,
                                  const StyleInfluenceState& styleInfluence,
                                  const std::vector<DrillPhraseRole>& phrase,
                                  const DrillGrooveBlueprint* blueprint,
                                  std::mt19937& rng) const
{
    track.notes.clear();
    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;

    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 100.0f, 130.0f);
    const bool halfTimeAware = tempoBand != TempoBand::Base;
    float density = std::clamp(params.densityAmount * style.kickDensityBias, 0.15f, 1.0f);
    if (halfTimeAware)
        density *= 0.92f;
    if (style.substyle == DrillSubstyle::DarkDrill)
        density *= 0.74f;
    density = std::clamp(density, 0.08f, 1.0f);
    const float rigidity = anchorRigidityWeight(styleInfluence);
    const ReferenceKickCorpus* referenceCorpus = hasReferenceKickCorpus(styleInfluence)
        ? &styleInfluence.referenceKickCorpus
        : nullptr;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.kickVelocityMin, style.kickVelocityMax);
    const auto candidates = candidateSteps(style.substyle, halfTimeAware);
    const int minHits = minHitsPerBar(style.substyle);
    const int maxHits = maxHitsPerBar(style.substyle);
    const auto caps = kickRoleCaps(style.substyle);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
        const auto referenceProfile = buildReferenceKickStepProfile(referenceCorpus, bar);
        const auto referenceLeadSteps = referenceKickLeadSteps(referenceProfile);
        std::vector<int> selected;
        selected.reserve(6);
        std::array<int, 4> majorEventsByWindow { 0, 0, 0, 0 };
        int supportCount = 0;
        int phraseEdgeCount = 0;
           const int supportCap = std::max(0,
                        static_cast<int>(std::round(static_cast<float>(caps.maxSupportPerBar)
                                    * std::clamp(1.55f - rigidity,
                                        0.0f,
                                        1.2f))));
           const int phraseEdgeCap = std::max(0,
                          static_cast<int>(std::round(static_cast<float>(caps.maxPhraseEdgePerBar)
                                       * std::clamp(1.55f - rigidity,
                                           0.0f,
                                           1.0f))));

        const float barBudget = (blueprint != nullptr && bar < static_cast<int>(blueprint->barPlans.size()))
            ? std::clamp(blueprint->barPlans[static_cast<size_t>(bar)].densityBudget, 0.0f, 1.0f)
            : std::clamp(density, 0.0f, 1.0f);
        const int dynamicMaxHits = std::max(1, static_cast<int>(std::round(static_cast<float>(maxHits) * std::clamp(0.55f + barBudget * 0.85f, 0.4f, 1.25f))));
        const int referenceBonusHits = referenceProfile.available && referenceProfile.density > 0.58f && role != DrillPhraseRole::Response ? 1 : 0;
        const int effectiveMaxHits = std::min({ maxHits, dynamicMaxHits + referenceBonusHits, caps.maxTotalPerBar });
        const int effectiveMinHits = std::min(minHits, effectiveMaxHits);

        auto tryAdd = [&](int step)
        {
            if (std::find(selected.begin(), selected.end(), step) != selected.end())
                return;
            if (static_cast<int>(selected.size()) >= effectiveMaxHits)
                return;

            const int absoluteStep = bar * 16 + step;
            const auto* slot = blueprint != nullptr ? blueprint->slotAt(absoluteStep) : nullptr;
            const auto eventType = classifyKickRole(step, slot);
            const int window = eventWindowIndex(step);

            if (majorEventsByWindow[static_cast<size_t>(window)] >= 1 && step != 0)
                return;

            if (eventType == DrillKickEventType::SupportKick && supportCount >= supportCap)
                return;
            if (eventType == DrillKickEventType::PhraseEdgeKick && phraseEdgeCount >= phraseEdgeCap)
                return;

            if (eventType == DrillKickEventType::PhraseEdgeKick)
            {
                const bool phraseEdgeOccupied = majorEventsByWindow[static_cast<size_t>(window)] > 0;
                if (phraseEdgeOccupied)
                    return;

                if (slot != nullptr && (slot->kickPlaced || slot->majorEventReserved))
                    return;
            }

            float gate = stepWeight(style.substyle, step);
            gate *= 0.42f + density * 0.64f;
            gate *= std::clamp(0.6f + barBudget * 0.8f, 0.35f, 1.2f);
            if (step == 0)
                gate = 1.0f;

            if (referenceProfile.available)
            {
                const float presence = referenceProfile.presence[static_cast<size_t>(step)];
                const float velocityBias = referenceProfile.velocityBias[static_cast<size_t>(step)];
                float referenceGate = 0.82f + presence * (step == 0 || step == 10 ? 0.34f : 0.62f) + velocityBias * 0.08f;
                if (eventType == DrillKickEventType::PhraseEdgeKick && presence >= 0.36f)
                    referenceGate += 0.08f;
                if (style.substyle == DrillSubstyle::UKDrill)
                    referenceGate += presence * 0.06f;
                if (referenceProfile.density < 0.28f && eventType == DrillKickEventType::SupportKick)
                    referenceGate *= 0.88f;
                gate *= std::clamp(referenceGate, 0.52f, 1.42f);
            }

            if (eventType == DrillKickEventType::AnchorKick)
                gate *= std::clamp(0.9f + rigidity * 0.14f, 0.8f, 1.15f);
            else
                gate *= std::clamp(1.3f - rigidity * 0.28f, 0.58f, 1.1f);

            if (slot != nullptr)
            {
                if (slot->kickForbidden)
                    return;

                gate *= std::clamp(slot->kickCandidateWeight * 1.15f, 0.0f, 1.35f);

                if (slot->snareProtection)
                {
                    if (step != 0 && step != 10)
                        gate *= 0.24f;
                    else
                        gate *= 0.66f;
                }

                const bool phraseEdgeStep = slot->phraseEdgeWeight >= 0.7f || step >= 13;
                if (phraseEdgeStep && slot->majorEventReserved)
                {
                    if (eventType == DrillKickEventType::PhraseEdgeKick)
                        return;
                    if (step != 0 && step != 10)
                        gate *= 0.36f;
                }

                if (slot->majorEventReserved && !phraseEdgeStep && step != 0)
                    gate *= 0.7f;
            }

            if (role == DrillPhraseRole::Ending && step >= 13)
                gate += 0.2f;
            if (role == DrillPhraseRole::Tension && (step == 6 || step == 10 || step == 13))
                gate += 0.14f;
            if (role == DrillPhraseRole::Response)
            {
                if (eventType == DrillKickEventType::SupportKick)
                    gate *= 0.68f;
                if (eventType == DrillKickEventType::PhraseEdgeKick)
                    gate *= 0.48f;
            }
            if (style.substyle == DrillSubstyle::DarkDrill && step != 0 && step != 10)
                gate *= 0.68f;

            const int extras = supportCount + phraseEdgeCount;
            if (role == DrillPhraseRole::Response && extras >= 1 && eventType != DrillKickEventType::AnchorKick)
                return;
            if (role == DrillPhraseRole::Tension && extras >= 2 && eventType != DrillKickEventType::AnchorKick)
                return;

            if (step != 0 && static_cast<int>(selected.size()) >= std::max(1, effectiveMaxHits - 1) && barBudget < 0.5f)
                gate *= 0.55f;

            if (chance(rng) < std::clamp(gate, 0.06f, 0.98f))
            {
                selected.push_back(step);
                ++majorEventsByWindow[static_cast<size_t>(window)];
                if (eventType == DrillKickEventType::SupportKick)
                    ++supportCount;
                else if (eventType == DrillKickEventType::PhraseEdgeKick)
                    ++phraseEdgeCount;
            }
        };

        // Start with deterministic anchor pulse and preferred support point.
        selected.push_back(0);
        ++majorEventsByWindow[0];
        if (std::find(candidates.begin(), candidates.end(), 10) != candidates.end())
            tryAdd(10);
        for (const int step : referenceLeadSteps)
        {
            if (step == 0 || step == 10)
                continue;
            if (referenceProfile.presence[static_cast<size_t>(step)] < 0.42f)
                continue;
            tryAdd(step);
            if (static_cast<int>(selected.size()) >= std::min(effectiveMaxHits, 3))
                break;
        }
        for (const int step : candidates)
            if (step != 0 && step != 10)
                tryAdd(step);

        // Ensure a minimum body for substyles that need more pressure.
        if (static_cast<int>(selected.size()) < effectiveMinHits)
        {
            for (const int step : candidates)
            {
                if (step == 0)
                    continue;
                if (std::find(selected.begin(), selected.end(), step) == selected.end())
                {
                    const int absoluteStep = bar * 16 + step;
                    const auto* slot = blueprint != nullptr ? blueprint->slotAt(absoluteStep) : nullptr;
                    const auto eventType = classifyKickRole(step, slot);
                    const int window = eventWindowIndex(step);
                    if (majorEventsByWindow[static_cast<size_t>(window)] >= 1)
                        continue;
                    if (eventType == DrillKickEventType::SupportKick && supportCount >= supportCap)
                        continue;
                    if (eventType == DrillKickEventType::PhraseEdgeKick && phraseEdgeCount >= phraseEdgeCap)
                        continue;
                    if (slot != nullptr && (slot->kickForbidden || slot->snareProtection || (slot->majorEventReserved && slot->phraseEdgeWeight >= 0.7f)))
                        continue;
                    selected.push_back(step);
                    ++majorEventsByWindow[static_cast<size_t>(window)];
                    if (eventType == DrillKickEventType::SupportKick)
                        ++supportCount;
                    else if (eventType == DrillKickEventType::PhraseEdgeKick)
                        ++phraseEdgeCount;
                }
                if (static_cast<int>(selected.size()) >= effectiveMinHits)
                    break;
            }
        }

        std::sort(selected.begin(), selected.end());
        selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

        // Prevent dense adjacent machine-like clusters.
        std::vector<int> filtered;
        filtered.reserve(selected.size());
        for (int step : selected)
        {
            if (!filtered.empty() && std::abs(step - filtered.back()) <= 1 && step != 15)
            {
                if (style.substyle == DrillSubstyle::BrooklynDrill && chance(rng) < 0.35f)
                    filtered.push_back(step);
                continue;
            }
            filtered.push_back(step);
        }

        for (int step : filtered)
        {
            const int absoluteStep = bar * 16 + step;
            const auto* slot = blueprint != nullptr ? blueprint->slotAt(absoluteStep) : nullptr;

            int velocity = vel(rng);
            if (step == 0 || step == 10)
                velocity = std::clamp(velocity + 4, style.kickVelocityMin, style.kickVelocityMax);
            else if (slot != nullptr && slot->phraseEdgeWeight >= 0.7f)
                velocity = std::clamp(velocity + 2, style.kickVelocityMin, style.kickVelocityMax);
            else
                velocity = std::clamp(velocity - 4, style.kickVelocityMin, style.kickVelocityMax);

            track.notes.push_back({ pitch, absoluteStep, 1, velocity, 0, false });
        }

        // Keep phrase-edge punctuation only when that phrase-edge window is free.
        if (role == DrillPhraseRole::Ending && std::find(filtered.begin(), filtered.end(), 15) == filtered.end())
        {
            const float edgeGate = style.substyle == DrillSubstyle::DarkDrill ? 0.22f : 0.5f;
            const int absoluteStep = bar * 16 + 15;
            const auto* slot = blueprint != nullptr ? blueprint->slotAt(absoluteStep) : nullptr;
            const int edgeWindow = eventWindowIndex(15);
            const bool phraseWindowFree = majorEventsByWindow[static_cast<size_t>(edgeWindow)] == 0;
            const bool allowedByBlueprint = slot == nullptr
                || (!slot->kickForbidden && !slot->snareProtection && !(slot->majorEventReserved && slot->phraseEdgeWeight >= 0.7f) && !slot->kickPlaced);
            if (phraseWindowFree && phraseEdgeCount < phraseEdgeCap && allowedByBlueprint && chance(rng) < edgeGate)
            {
                track.notes.push_back({ pitch, bar * 16 + 15, 1, vel(rng), 0, false });
            }
        }
    }

    std::sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.velocity > b.velocity;
    });
    track.notes.erase(std::unique(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        return a.step == b.step;
    }), track.notes.end());
}
} // namespace bbg
