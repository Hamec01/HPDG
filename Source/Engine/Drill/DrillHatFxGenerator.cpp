#include "DrillHatFxGenerator.h"

#include <algorithm>
#include <array>
#include <set>
#include <vector>

#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"

namespace bbg
{
namespace
{
struct FxWindow
{
    int bar = 0;
    int step = 0;
    int tick = 0;
    bool preSnare = false;
    bool kickReaction = false;
    bool phraseEdge = false;
    bool transitionLift = false;
    bool responseBar = false;
    bool tensionOrEndingBar = false;
};

DrillFxSize classifyBankSize(DrillHatFxBank bank)
{
    switch (bank)
    {
        case DrillHatFxBank::ShortTripletCluster:
        case DrillHatFxBank::SkippyBurst:
        case DrillHatFxBank::AngularJabBurst:
        case DrillHatFxBank::KickReactionSkips:
            return DrillFxSize::Tiny;
        case DrillHatFxBank::ExtendedTripletCluster:
        case DrillHatFxBank::PreSnareTriplet:
        case DrillHatFxBank::PhraseEdgeCluster:
        case DrillHatFxBank::Tight32Burst:
            return DrillFxSize::Medium;
        case DrillHatFxBank::TresilloEdgeBurst:
        case DrillHatFxBank::DarkFadeBurst:
            return DrillFxSize::Major;
    }

    return DrillFxSize::Tiny;
}

void applyBank(TrackState& track, DrillHatFxBank bank, int pitch, int tick, int bars, int baseVel)
{
    switch (bank)
    {
        case DrillHatFxBank::ShortTripletCluster:
            for (int i = 0; i < 3; ++i)
                HiResTiming::addNoteAtTick(track, pitch, tick + i * HiResTiming::kTicks1_24, std::max(1, baseVel - i * 2), true, bars);
            break;
        case DrillHatFxBank::ExtendedTripletCluster:
            for (int i = 0; i < 5; ++i)
                HiResTiming::addNoteAtTick(track, pitch, tick + i * HiResTiming::kTicks1_24, std::max(1, baseVel - i * 2), true, bars);
            break;
        case DrillHatFxBank::SkippyBurst:
        {
            const std::array<int, 4> offsets { 0, HiResTiming::kTicks1_64, HiResTiming::kTicks1_32, HiResTiming::kTicks1_32 + HiResTiming::kTicks1_64 };
            for (size_t i = 0; i < offsets.size(); ++i)
                HiResTiming::addNoteAtTick(track, pitch, tick + offsets[i], std::max(1, baseVel - static_cast<int>(i) * 2), true, bars);
            break;
        }
        case DrillHatFxBank::TresilloEdgeBurst:
        {
            const std::array<int, 3> offsets { 0, HiResTiming::kTicks1_24 * 3, HiResTiming::kTicks1_24 * 6 };
            for (size_t i = 0; i < offsets.size(); ++i)
                HiResTiming::addNoteAtTick(track, pitch, tick + offsets[i], std::max(1, baseVel - static_cast<int>(i) * 3), true, bars);
            break;
        }
        case DrillHatFxBank::PreSnareTriplet:
            for (int i = 0; i < 4; ++i)
                HiResTiming::addNoteAtTick(track, pitch, tick - HiResTiming::kTicks1_24 + i * HiResTiming::kTicks1_24, std::max(1, baseVel - i * 2), true, bars);
            break;
        case DrillHatFxBank::KickReactionSkips:
            HiResTiming::addNoteAtTick(track, pitch, tick - HiResTiming::kTicks1_32, std::max(1, baseVel - 4), true, bars);
            HiResTiming::addNoteAtTick(track, pitch, tick, std::max(1, baseVel - 2), true, bars);
            HiResTiming::addNoteAtTick(track, pitch, tick + HiResTiming::kTicks1_64, std::max(1, baseVel - 5), true, bars);
            break;
        case DrillHatFxBank::PhraseEdgeCluster:
            for (int i = 0; i < 4; ++i)
                HiResTiming::addNoteAtTick(track, pitch, tick + i * HiResTiming::kTicks1_32, std::max(1, baseVel - i * 3), true, bars);
            break;
        case DrillHatFxBank::DarkFadeBurst:
            for (int i = 0; i < 6; ++i)
                HiResTiming::addNoteAtTick(track, pitch, tick + i * HiResTiming::kTicks1_64, std::max(1, baseVel - 8 - i), true, bars);
            break;
        case DrillHatFxBank::AngularJabBurst:
            HiResTiming::addNoteAtTick(track, pitch, tick, std::max(1, baseVel - 2), true, bars);
            HiResTiming::addNoteAtTick(track, pitch, tick + HiResTiming::kTicks1_64, std::max(1, baseVel - 6), true, bars);
            break;
        case DrillHatFxBank::Tight32Burst:
            for (int i = 0; i < 4; ++i)
                HiResTiming::addNoteAtTick(track, pitch, tick + i * HiResTiming::kTicks1_32, std::max(1, baseVel - i * 2), true, bars);
            break;
    }
}

bool hasKickNear(const TrackState* kickTrack, int step)
{
    if (kickTrack == nullptr)
        return false;

    for (const auto& n : kickTrack->notes)
        if (std::abs(n.step - step) <= 1)
            return true;
    return false;
}

bool hasSnareAt(const TrackState* snareTrack, int step)
{
    if (snareTrack == nullptr)
        return false;

    for (const auto& n : snareTrack->notes)
        if (!n.isGhost && n.step == step)
            return true;
    return false;
}

bool hasOpenHatAt(const TrackState* openHatTrack, int step)
{
    if (openHatTrack == nullptr)
        return false;

    for (const auto& n : openHatTrack->notes)
        if (!n.isGhost && std::abs(n.step - step) <= 0)
            return true;
    return false;
}

bool hasSubRestartNear(const TrackState* subTrack, int step)
{
    if (subTrack == nullptr)
        return false;

    for (size_t i = 0; i < subTrack->notes.size(); ++i)
    {
        const auto& n = subTrack->notes[i];
        if (std::abs(n.step - step) > 1)
            continue;

        const bool shortHit = n.length <= 2;
        const bool denseNeighbor = (i > 0 && std::abs(n.step - subTrack->notes[i - 1].step) <= 2)
            || (i + 1 < subTrack->notes.size() && std::abs(subTrack->notes[i + 1].step - n.step) <= 2);
        if (shortHit || denseNeighbor)
            return true;
    }
    return false;
}

bool hasStrongSubEventNear(const TrackState* subTrack, int step)
{
    if (subTrack == nullptr)
        return false;

    for (const auto& n : subTrack->notes)
    {
        if (std::abs(n.step - step) > 1)
            continue;

        const bool strongVelocity = n.velocity >= 112;
        const bool phraseEdgeArea = (n.step % 16) >= 10;
        const bool dominantLength = n.length >= 3;
        if (phraseEdgeArea && (strongVelocity || dominantLength))
            return true;
    }

    return false;
}

bool hasRollLikeInWindow(const TrackState& hatTrack, int step)
{
    int count = 0;
    for (const auto& n : hatTrack.notes)
    {
        if (std::abs(n.step - step) <= 1)
            ++count;
    }
    return count >= 3;
}

std::vector<FxWindow> collectWindows(const TrackState& hatTrack,
                                     const TrackState* kickTrack,
                                     const TrackState* snareTrack,
                                     const std::vector<DrillPhraseRole>& phrase,
                                     const DrillGrooveBlueprint* blueprint,
                                     const DrillHatFxStyleProfile& profile)
{
    std::vector<FxWindow> windows;
    std::set<int> usedStep;
    windows.reserve(hatTrack.notes.size());

    for (const auto& h : hatTrack.notes)
    {
        const int step = h.step;
        if (!usedStep.insert(step).second)
            continue;

        const int bar = step / 16;
        const int stepInBar = step % 16;
        const bool preSnare = hasSnareAt(snareTrack, bar * 16 + 4) && (stepInBar == 3)
                           || hasSnareAt(snareTrack, bar * 16 + 12) && (stepInBar == 11);
        const auto* slot = blueprint != nullptr ? blueprint->slotAt(step) : nullptr;
        const bool phraseEdge = ((slot != nullptr && slot->phraseEdgeWeight >= 0.7f) || stepInBar >= 13) && profile.allowPhraseEdge;
        const bool transitionLift = stepInBar == 15;
        const bool kickReaction = profile.allowKickReaction && hasKickNear(kickTrack, step) && !preSnare;
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;

        const bool allowPreSnare = profile.allowPreSnare && preSnare;
        const bool allowEndOfBar = profile.allowEndOfBar && stepInBar >= 14;
        const bool allowHalfEdge = profile.allowEndOfHalfBar && (stepInBar == 7 || stepInBar == 15);
        const bool allowTransition = profile.allowTransitionLift && transitionLift;
        const bool tensionCarrier = role == DrillPhraseRole::Tension && stepInBar >= 10;

        const bool fxAllowed = slot == nullptr ? true : slot->fxAllowed;
        const bool protectedBySnare = slot != nullptr && slot->snareProtection;
        const bool majorReserved = slot != nullptr && slot->majorEventReserved;
        const bool reservedSilence = protectedBySnare || (majorReserved && (slot->phraseEdgeWeight >= 0.7f));

        if (!fxAllowed || reservedSilence)
            continue;

        if (!allowPreSnare && !phraseEdge && !kickReaction && !allowEndOfBar && !allowHalfEdge && !allowTransition && !tensionCarrier)
            continue;

        windows.push_back({
            bar,
            stepInBar,
            HiResTiming::noteTick(h),
            allowPreSnare,
            kickReaction,
            phraseEdge,
            allowTransition,
            role == DrillPhraseRole::Response,
            role == DrillPhraseRole::Tension || role == DrillPhraseRole::Ending
        });
    }

    return windows;
}

DrillHatFxBank chooseWeightedBank(const DrillHatFxStyleProfile& profile,
                                  const FxWindow& w,
                                  std::mt19937& rng)
{
    std::vector<DrillHatFxBankWeight> candidates;
    candidates.reserve(profile.banks.size());

    for (const auto& bw : profile.banks)
    {
        if (bw.weight <= 0)
            continue;

        int adjusted = bw.weight;
        if (w.preSnare && bw.bank == DrillHatFxBank::PreSnareTriplet)
            adjusted += 8;
        if (w.kickReaction && bw.bank == DrillHatFxBank::KickReactionSkips)
            adjusted += 8;
        if (w.phraseEdge && (bw.bank == DrillHatFxBank::PhraseEdgeCluster || bw.bank == DrillHatFxBank::DarkFadeBurst))
            adjusted += 8;
        if (w.transitionLift && bw.bank == DrillHatFxBank::Tight32Burst)
            adjusted += 5;

        if (adjusted > 0)
            candidates.push_back({ bw.bank, adjusted });
    }

    if (candidates.empty())
        return DrillHatFxBank::ShortTripletCluster;

    int total = 0;
    for (const auto& c : candidates)
        total += c.weight;

    std::uniform_int_distribution<int> pick(1, std::max(1, total));
    int r = pick(rng);
    for (const auto& c : candidates)
    {
        r -= c.weight;
        if (r <= 0)
            return c.bank;
    }

    return DrillHatFxBank::ShortTripletCluster;
}

int eventWindowIndex(int step)
{
    const int stepInBar = ((step % 16) + 16) % 16;
    return std::clamp(stepInBar / 4, 0, 3);
}
}

void DrillHatFxGenerator::generate(TrackState& hatFxTrack,
                                   const TrackState& hatTrack,
                                   const TrackState* kickTrack,
                                   const TrackState* snareTrack,
                                   const TrackState* openHatTrack,
                                   const TrackState* subTrack,
                                   const DrillStyleProfile& style,
                                   float fxIntensity,
                                   const std::vector<DrillPhraseRole>& phrase,
                                   const DrillGrooveBlueprint* blueprint,
                                   std::mt19937& rng) const
{
    hatFxTrack.notes.clear();
    const auto* info = TrackRegistry::find(hatFxTrack.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 44;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
    const int bars = std::max(1, static_cast<int>((hatTrack.notes.empty() ? 1 : (hatTrack.notes.back().step / 16) + 1)));

    const float intensity = std::clamp(fxIntensity * style.hatFxIntensity, 0.0f, 1.0f);
    const auto styleSpec = getDrillStyleSpec(style.substyle);
    const auto profile = getDrillHatFxStyleProfile(style.substyle);
    const auto windows = collectWindows(hatTrack, kickTrack, snareTrack, phrase, blueprint, profile);

    const bool forbidFxIfRollInWindow = true;
    const bool forbidFxIfOpenHatInWindow = true;
    const bool forbidFxIfSubRestartInWindow = true;
    const bool forbidFxIfStrongSubEvent = true;
    const bool decorationOnlyTinyBanks = true;
    const int maxAnyMajorEventPerBar = styleSpec.maxMajorEventsPerBar;
    const int maxAnyMajorEventPer2Bars = styleSpec.maxMajorEventsPer2Bars;

    std::vector<int> tinyPerBar(static_cast<size_t>(bars), 0);
    std::vector<int> mediumPerBar(static_cast<size_t>(bars), 0);
    std::vector<int> majorPerBar(static_cast<size_t>(bars), 0);
    std::vector<int> majorPerWindow(static_cast<size_t>(std::max(1, (bars + 1) / 2)), 0);
    std::vector<std::array<int, 4>> anyPerBarWindow(static_cast<size_t>(bars));
    for (auto& w : anyPerBarWindow)
        w = { 0, 0, 0, 0 };

    for (const auto& w : windows)
    {
        float gate = 0.08f + intensity * 0.36f;
        if (w.preSnare)
            gate += 0.14f;
        if (w.kickReaction)
            gate += 0.08f;
        if (w.phraseEdge)
            gate += 0.18f;
        if (w.transitionLift)
            gate += 0.08f;
        if (w.responseBar)
            gate *= 0.62f;
        if (!w.tensionOrEndingBar && !w.preSnare && !w.kickReaction)
            gate *= 0.72f;
        if (style.substyle == DrillSubstyle::DarkDrill)
            gate *= 0.8f;
        if (style.substyle == DrillSubstyle::BrooklynDrill)
            gate *= 0.82f;
        if (style.substyle == DrillSubstyle::UKDrill)
            gate *= 0.72f;
        if (style.cleanHatMode)
            gate *= 0.58f;

        const int absoluteStep = w.bar * 16 + w.step;
        const auto* slot = blueprint != nullptr ? blueprint->slotAt(absoluteStep) : nullptr;
        const bool fxAllowed = slot == nullptr ? true : slot->fxAllowed;
        const bool snareProtected = slot != nullptr && slot->snareProtection;
        const bool majorReserved = slot != nullptr && slot->majorEventReserved;
        const bool phraseEdgeReserved = slot != nullptr && slot->phraseEdgeWeight >= 0.7f;

        if (!fxAllowed || snareProtected || majorReserved || phraseEdgeReserved)
            continue;

        if (forbidFxIfRollInWindow && hasRollLikeInWindow(hatTrack, absoluteStep))
            continue;
        if (forbidFxIfOpenHatInWindow && hasOpenHatAt(openHatTrack, absoluteStep))
            continue;
        if (forbidFxIfSubRestartInWindow && hasSubRestartNear(subTrack, absoluteStep))
            continue;
        if (forbidFxIfStrongSubEvent && hasStrongSubEventNear(subTrack, absoluteStep))
            continue;

        if (slot != nullptr && slot->kickPlaced && slot->kickEventType != DrillKickEventType::AnchorKick)
            continue;

        if (w.phraseEdge && hasKickNear(kickTrack, absoluteStep))
            continue;

        if (chance(rng) > std::clamp(gate, 0.04f, 0.82f))
            continue;

        if (style.cleanHatMode && !(w.preSnare || w.phraseEdge))
            continue;

        const auto bank = chooseWeightedBank(profile, w, rng);
        const auto size = classifyBankSize(bank);
        const size_t barIndex = static_cast<size_t>(std::clamp(w.bar, 0, bars - 1));
        const size_t windowIndex = static_cast<size_t>(std::clamp(w.bar / 2, 0, static_cast<int>(majorPerWindow.size()) - 1));
        const int localWindow = eventWindowIndex(absoluteStep);

        if (style.cleanHatMode && size != DrillFxSize::Tiny)
            continue;
        if (decorationOnlyTinyBanks && size != DrillFxSize::Tiny)
            continue;

        if (anyPerBarWindow[barIndex][static_cast<size_t>(localWindow)] >= 1)
            continue;

        if (size == DrillFxSize::Tiny && tinyPerBar[barIndex] >= profile.maxTinyPerBar)
            continue;
        if (size == DrillFxSize::Medium && mediumPerBar[barIndex] >= profile.maxMediumPerBar)
            continue;
        if (size == DrillFxSize::Major)
        {
            if (majorPerBar[barIndex] >= profile.maxMajorPerBar)
                continue;
            if (majorPerWindow[windowIndex] >= profile.maxMajorPerTwoBars)
                continue;
        }

        const int anyMajorInBar = tinyPerBar[barIndex] + mediumPerBar[barIndex] + majorPerBar[barIndex];
        if (anyMajorInBar >= maxAnyMajorEventPerBar)
            continue;

        int anyMajorInWindow = majorPerWindow[windowIndex];
        const int pairBar = (w.bar % 2 == 0) ? (w.bar + 1) : (w.bar - 1);
        if (pairBar >= 0 && pairBar < bars)
        {
            const size_t pIdx = static_cast<size_t>(pairBar);
            anyMajorInWindow += tinyPerBar[pIdx] + mediumPerBar[pIdx] + majorPerBar[pIdx];
        }
        anyMajorInWindow += tinyPerBar[barIndex] + mediumPerBar[barIndex] + majorPerBar[barIndex];
        if (anyMajorInWindow >= maxAnyMajorEventPer2Bars)
            continue;

        int baseVel = vel(rng);
        if (w.preSnare)
            baseVel = std::min(style.hatVelocityMax, baseVel + 6);
        if (style.substyle == DrillSubstyle::DarkDrill)
            baseVel = std::max(style.hatVelocityMin, baseVel - 6);

        applyBank(hatFxTrack, bank, pitch, w.tick, bars, baseVel);
        ++anyPerBarWindow[barIndex][static_cast<size_t>(localWindow)];

        if (size == DrillFxSize::Tiny)
            ++tinyPerBar[barIndex];
        else if (size == DrillFxSize::Medium)
            ++mediumPerBar[barIndex];
        else
        {
            ++majorPerBar[barIndex];
            ++majorPerWindow[windowIndex];
        }
    }

    std::sort(hatFxTrack.notes.begin(), hatFxTrack.notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.velocity > b.velocity;
    });
}
} // namespace bbg
