#include "TrapKickGenerator.h"

#include <algorithm>

#include "TrapLowEndRoles.h"
#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
struct TrapKickEvent
{
    NoteEvent note;
    TrapKickRole role = TrapKickRole::Support;
};

float densityScaleForSubstyle(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.78f;
        case TrapSubstyle::CloudTrap: return 0.86f;
        case TrapSubstyle::RageTrap: return 1.18f;
        case TrapSubstyle::MemphisTrap: return 0.94f;
        case TrapSubstyle::LuxuryTrap: return 0.9f;
        default: return 1.0f;
    }
}

int minAnchorsPerBar(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::RageTrap: return 2;
        default: return 1;
    }
}

float lowEndCouplingWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.lowEndCouplingWeight, 0.7f, 1.5f);
}

int maxKickEventsPerBar(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::DarkTrap: return 3;
        case TrapSubstyle::LuxuryTrap: return 3;
        case TrapSubstyle::ATLClassic: return 4;
        case TrapSubstyle::CloudTrap: return 4;
        case TrapSubstyle::MemphisTrap: return 4;
        case TrapSubstyle::RageTrap: return 5;
    }

    return 4;
}

int velocityForRole(TrapKickRole role, const TrapStyleProfile& style, std::mt19937& rng)
{
    const int minVel = style.kickVelocityMin;
    const int maxVel = style.kickVelocityMax;

    switch (role)
    {
        case TrapKickRole::Anchor:
        {
            std::uniform_int_distribution<int> dist(std::clamp(minVel + 10, 1, 127), std::clamp(maxVel, 1, 127));
            return dist(rng);
        }
        case TrapKickRole::Tail:
        {
            std::uniform_int_distribution<int> dist(std::clamp(minVel + 4, 1, 127), std::clamp(maxVel - 4, 1, 127));
            return dist(rng);
        }
        case TrapKickRole::Pickup:
        {
            std::uniform_int_distribution<int> dist(std::clamp(minVel + 2, 1, 127), std::clamp(maxVel - 8, 1, 127));
            return dist(rng);
        }
        case TrapKickRole::GhostLike:
        {
            std::uniform_int_distribution<int> dist(std::clamp(minVel - 20, 1, 127), std::clamp(minVel - 6, 1, 127));
            return dist(rng);
        }
        case TrapKickRole::Support:
        default:
        {
            std::uniform_int_distribution<int> dist(std::clamp(minVel + 4, 1, 127), std::clamp(maxVel - 6, 1, 127));
            return dist(rng);
        }
    }
}

void tryAddEvent(std::vector<TrapKickEvent>& plan,
                 int step,
                 TrapKickRole role,
                 int bars,
                 const TrapStyleProfile& style,
                 std::mt19937& rng)
{
    if (step < 0 || step >= bars * 16)
        return;

    auto duplicate = std::find_if(plan.begin(), plan.end(), [step](const TrapKickEvent& e)
    {
        return e.note.step == step;
    });
    if (duplicate != plan.end())
    {
        if (static_cast<int>(role) < static_cast<int>(duplicate->role))
            duplicate->role = role;
        return;
    }

    plan.push_back({ NoteEvent { 36, step, 1, velocityForRole(role, style, rng), 0, role == TrapKickRole::GhostLike, {} }, role });
}

std::vector<TrapKickEvent> buildKickPhrasePlan(int bars,
                                               float density,
                                               const TrapStyleProfile& style,
                                               const std::vector<TrapPhraseRole>& phrase,
                                               bool halfTimeAware,
                                               std::mt19937& rng)
{
    std::vector<TrapKickEvent> plan;
    plan.reserve(static_cast<size_t>(bars * 5));
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    const float sparseBias = (style.substyle == TrapSubstyle::DarkTrap || style.substyle == TrapSubstyle::LuxuryTrap) ? 0.84f : 1.0f;
    const float rageBias = style.substyle == TrapSubstyle::RageTrap ? 1.16f : 1.0f;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;
        const int barStart = bar * 16;
        const int midStep = halfTimeAware ? 10 : 8;
        const int lateTailStep = 14;

        tryAddEvent(plan, barStart, TrapKickRole::Anchor, bars, style, rng);

        float midGate = 0.46f + density * 0.28f;
        if (phraseRole == TrapPhraseRole::Lift)
            midGate += 0.1f;
        if (phraseRole == TrapPhraseRole::Ending)
            midGate += 0.06f;
        if (style.substyle == TrapSubstyle::RageTrap)
            midGate += 0.14f;
        if (style.substyle == TrapSubstyle::DarkTrap || style.substyle == TrapSubstyle::LuxuryTrap)
            midGate -= 0.08f;
        if (chance(rng) < std::clamp(midGate * sparseBias * rageBias, 0.1f, 0.96f))
            tryAddEvent(plan, barStart + midStep, TrapKickRole::Anchor, bars, style, rng);

        float pickupGate = 0.18f + density * 0.34f;
        if (phraseRole == TrapPhraseRole::Lift)
            pickupGate += 0.12f;
        if (phraseRole == TrapPhraseRole::Ending)
            pickupGate += 0.08f;
        if (style.substyle == TrapSubstyle::RageTrap)
            pickupGate += 0.14f;
        if (style.substyle == TrapSubstyle::DarkTrap)
            pickupGate -= 0.06f;

        if (chance(rng) < std::clamp(pickupGate * sparseBias * rageBias, 0.05f, 0.92f))
        {
            const int pickupStep = (chance(rng) < 0.55f) ? 6 : 11;
            tryAddEvent(plan, barStart + pickupStep, TrapKickRole::Pickup, bars, style, rng);
        }

        float supportGate = 0.16f + density * 0.26f;
        if (style.substyle == TrapSubstyle::RageTrap)
            supportGate += 0.12f;
        if (style.substyle == TrapSubstyle::CloudTrap || style.substyle == TrapSubstyle::MemphisTrap)
            supportGate += 0.06f;
        if (style.substyle == TrapSubstyle::DarkTrap || style.substyle == TrapSubstyle::LuxuryTrap)
            supportGate -= 0.06f;
        if (chance(rng) < std::clamp(supportGate * sparseBias * rageBias, 0.04f, 0.82f))
            tryAddEvent(plan, barStart + 10, TrapKickRole::Support, bars, style, rng);

        float tailGate = 0.18f + density * 0.22f;
        if (phraseRole == TrapPhraseRole::Ending)
            tailGate += 0.22f;
        if (style.substyle == TrapSubstyle::MemphisTrap)
            tailGate += 0.1f;
        if (style.substyle == TrapSubstyle::RageTrap)
            tailGate += 0.08f;
        if (chance(rng) < std::clamp(tailGate, 0.06f, 0.9f))
            tryAddEvent(plan, barStart + lateTailStep, TrapKickRole::Tail, bars, style, rng);

        if (style.substyle == TrapSubstyle::RageTrap && chance(rng) < std::clamp(0.2f + density * 0.2f, 0.06f, 0.72f))
            tryAddEvent(plan, barStart + 12, TrapKickRole::Pickup, bars, style, rng);

        if (chance(rng) < std::clamp(0.06f + density * 0.08f, 0.0f, 0.24f))
        {
            const int ghostStep = barStart + ((chance(rng) < 0.5f) ? 7 : 13);
            tryAddEvent(plan, ghostStep, TrapKickRole::GhostLike, bars, style, rng);
        }
    }

    std::sort(plan.begin(), plan.end(), [](const TrapKickEvent& a, const TrapKickEvent& b)
    {
        return a.note.step < b.note.step;
    });

    std::vector<int> perBarAnchors(static_cast<size_t>(bars), 0);
    std::vector<int> perBarCount(static_cast<size_t>(bars), 0);
    for (const auto& e : plan)
    {
        const int bar = std::clamp(e.note.step / 16, 0, bars - 1);
        perBarCount[static_cast<size_t>(bar)] += 1;
        if (e.role == TrapKickRole::Anchor)
            perBarAnchors[static_cast<size_t>(bar)] += 1;
    }

    for (int bar = 0; bar < bars; ++bar)
    {
        if (perBarAnchors[static_cast<size_t>(bar)] >= minAnchorsPerBar(style.substyle))
            continue;
        tryAddEvent(plan, bar * 16 + (halfTimeAware ? 10 : 8), TrapKickRole::Anchor, bars, style, rng);
    }

    std::sort(plan.begin(), plan.end(), [](const TrapKickEvent& a, const TrapKickEvent& b)
    {
        if (a.note.step != b.note.step)
            return a.note.step < b.note.step;
        return static_cast<int>(a.role) < static_cast<int>(b.role);
    });

    const int maxEvents = maxKickEventsPerBar(style.substyle);
    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<size_t> indices;
        for (size_t i = 0; i < plan.size(); ++i)
            if ((plan[i].note.step / 16) == bar)
                indices.push_back(i);

        if (static_cast<int>(indices.size()) <= maxEvents)
            continue;

        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b)
        {
            const auto rank = [](TrapKickRole role)
            {
                switch (role)
                {
                    case TrapKickRole::Anchor: return 5;
                    case TrapKickRole::Tail: return 4;
                    case TrapKickRole::Support: return 3;
                    case TrapKickRole::Pickup: return 2;
                    case TrapKickRole::GhostLike: return 1;
                }
                return 0;
            };
            return rank(plan[a].role) > rank(plan[b].role);
        });

        for (size_t i = static_cast<size_t>(maxEvents); i < indices.size(); ++i)
            plan[indices[i]].note.step = -1;
    }

    plan.erase(std::remove_if(plan.begin(), plan.end(), [](const TrapKickEvent& e)
    {
        return e.note.step < 0;
    }), plan.end());

    return plan;
}
}

void TrapKickGenerator::generate(TrackState& track,
                                 const GeneratorParams& params,
                                 const TrapStyleProfile& style,
                                 const StyleInfluenceState& styleInfluence,
                                 const std::vector<TrapPhraseRole>& phrase,
                                 std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 145.0f, 100.0f, 132.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.86f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.74f;
    const float density = std::clamp(params.densityAmount * style.kickDensityBias * densityScaleForSubstyle(style.substyle) * tempoScale * lowEndCouplingWeight(styleInfluence), 0.12f, 1.0f);
    const bool halfTimeAware = tempoBand != TempoBand::Base;

    auto plan = buildKickPhrasePlan(bars, density, style, phrase, halfTimeAware, rng);
    for (auto& event : plan)
    {
        event.note.pitch = pitch;
        if (event.role == TrapKickRole::Anchor)
            event.note.velocity = std::clamp(event.note.velocity + 3, 1, 127);
        else if (event.role == TrapKickRole::Tail)
            event.note.velocity = std::clamp(event.note.velocity + 1, 1, 127);
        track.notes.push_back(event.note);
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
