#include "DrillGrooveBlueprint.h"

#include <algorithm>

namespace bbg
{
namespace
{
float clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}
}

const DrillGrooveSlot* DrillGrooveBlueprint::slotAt(int absoluteStep) const
{
    if (absoluteStep < 0 || slots.empty())
        return nullptr;

    const int total = static_cast<int>(slots.size());
    if (total <= 0)
        return nullptr;

    const int idx = std::clamp(absoluteStep, 0, total - 1);
    return &slots[static_cast<size_t>(idx)];
}

DrillGrooveSlot* DrillGrooveBlueprint::slotAt(int absoluteStep)
{
    if (absoluteStep < 0 || slots.empty())
        return nullptr;

    const int total = static_cast<int>(slots.size());
    if (total <= 0)
        return nullptr;

    const int idx = std::clamp(absoluteStep, 0, total - 1);
    return &slots[static_cast<size_t>(idx)];
}

void DrillGrooveBlueprint::reserveMajorWindow(int absoluteStep, int radius)
{
    const int total = static_cast<int>(slots.size());
    if (total <= 0)
        return;

    const int begin = std::max(0, absoluteStep - std::max(0, radius));
    const int end = std::min(total - 1, absoluteStep + std::max(0, radius));
    for (int step = begin; step <= end; ++step)
        slots[static_cast<size_t>(step)].majorEventReserved = true;
}

bool DrillGrooveBlueprint::hasMajorReservedNear(int absoluteStep, int radius) const
{
    const int total = static_cast<int>(slots.size());
    if (total <= 0)
        return false;

    const int begin = std::max(0, absoluteStep - std::max(0, radius));
    const int end = std::min(total - 1, absoluteStep + std::max(0, radius));
    for (int step = begin; step <= end; ++step)
        if (slots[static_cast<size_t>(step)].majorEventReserved)
            return true;

    return false;
}

void DrillGrooveBlueprint::ingestSnareTrack(const TrackState& snareTrack, int protectionRadius)
{
    for (const auto& note : snareTrack.notes)
    {
        if (auto* slot = slotAt(note.step))
        {
            slot->snareAnchor = !note.isGhost;
            slot->snareGhostAllowed = !slot->snareAnchor;
            if (!note.isGhost)
                reserveMajorWindow(note.step, 1);
        }

        if (!note.isGhost)
        {
            const int r = std::max(1, protectionRadius);
            const int begin = std::max(0, note.step - r);
            const int end = std::min(static_cast<int>(slots.size()) - 1, note.step + r);
            for (int step = begin; step <= end; ++step)
            {
                auto& slot = slots[static_cast<size_t>(step)];
                slot.snareProtection = true;
                slot.fxAllowed = false;
                if (step != note.step)
                    slot.openHatAllowed = slot.openHatAllowed && (std::abs(step - note.step) > 1);
            }
        }
    }
}

void DrillGrooveBlueprint::ingestKickTrack(const TrackState& kickTrack)
{
    for (const auto& note : kickTrack.notes)
    {
        if (auto* slot = slotAt(note.step))
        {
            const int stepInBar = stepsPerBar > 0 ? (note.step % stepsPerBar) : 0;
            DrillKickEventType eventType = DrillKickEventType::SupportKick;
            if (stepInBar == 0 || stepInBar == 10)
                eventType = DrillKickEventType::AnchorKick;
            else if (slot->phraseEdgeWeight >= 0.7f || stepInBar >= 13)
                eventType = DrillKickEventType::PhraseEdgeKick;

            slot->kickPlaced = true;
            slot->kickEventType = eventType;
            slot->subStartWeight = clamp01(slot->subStartWeight + 0.24f);
            if ((note.step % stepsPerBar) >= 13)
                slot->phraseEdgeWeight = clamp01(slot->phraseEdgeWeight + 0.35f);

            if (eventType == DrillKickEventType::PhraseEdgeKick)
                reserveMajorWindow(note.step, 1);
            else if (eventType == DrillKickEventType::SupportKick)
                reserveMajorWindow(note.step, 0);
        }
    }
}

DrillGrooveBlueprint DrillGrooveBlueprintBuilder::build(const DrillPhrasePlan& phrasePlan,
                                                        int bars,
                                                        DrillSubstyle substyle,
                                                        float densityAmount)
{
    DrillGrooveBlueprint out;
    out.bars = std::max(1, bars);
    out.stepsPerBar = 16;
    out.barPlans.resize(static_cast<size_t>(out.bars));
    out.slots.resize(static_cast<size_t>(out.bars * out.stepsPerBar));

    for (int bar = 0; bar < out.bars; ++bar)
    {
        DrillGrooveBarPlan barPlan;
        if (bar < static_cast<int>(phrasePlan.bars.size()))
        {
            const auto& src = phrasePlan.bars[static_cast<size_t>(bar)];
            barPlan.phraseRole = src.role;
            barPlan.energyLevel = src.energyLevel;
            barPlan.densityBudget = src.densityBudget;
            barPlan.variationBudget = src.variationBudget;
            barPlan.isEndingBar = src.isEnding;
            barPlan.isTransitionBar = src.isTransition;
        }
        else
        {
            barPlan.phraseRole = (bar == out.bars - 1) ? DrillPhraseRole::Ending : DrillPhraseRole::Statement;
            barPlan.energyLevel = clamp01(0.45f + densityAmount * 0.35f);
            barPlan.densityBudget = clamp01(0.40f + densityAmount * 0.45f);
            barPlan.variationBudget = clamp01(0.30f + densityAmount * 0.40f);
            barPlan.isEndingBar = (bar == out.bars - 1);
            barPlan.isTransitionBar = ((bar % 2) == 1);
        }

        out.barPlans[static_cast<size_t>(bar)] = barPlan;

        for (int stepInBar = 0; stepInBar < out.stepsPerBar; ++stepInBar)
        {
            const int step = bar * out.stepsPerBar + stepInBar;
            auto& slot = out.slots[static_cast<size_t>(step)];

            slot.kickCandidateWeight = 0.30f;
            if (stepInBar == 0 || stepInBar == 10)
                slot.kickCandidateWeight = 1.0f;
            else if (stepInBar == 6 || stepInBar == 13 || stepInBar == 15)
                slot.kickCandidateWeight = 0.66f;

            slot.hatBackboneWeight = (stepInBar % 3) == 0 ? 0.84f : 0.42f;
            slot.hatFillAllowed = ((stepInBar % 2) == 1);
            slot.rollAllowed = (stepInBar >= 10);
            slot.accentAllowed = (stepInBar % 4) != 1;
            slot.fxAllowed = (stepInBar >= 9);
            slot.openHatAllowed = (stepInBar == 7 || stepInBar == 15 || stepInBar == 13);
            slot.percAllowed = (stepInBar == 5 || stepInBar == 11 || stepInBar == 15);

            slot.subStartWeight = (stepInBar == 0 || stepInBar == 10) ? 0.94f : 0.36f;
            slot.subRestartAllowed = (stepInBar < 14);
            slot.subHoldPreferred = (stepInBar >= 12);
            slot.slideAllowed = (stepInBar >= 12);

            slot.phraseEdgeWeight = (stepInBar >= 13) ? 0.9f : 0.0f;

            if (barPlan.phraseRole == DrillPhraseRole::Ending)
            {
                slot.rollAllowed = (stepInBar >= 12);
                slot.fxAllowed = (stepInBar >= 12);
                slot.subHoldPreferred = (stepInBar >= 11);
                slot.subRestartAllowed = (stepInBar <= 13);
            }
            else if (barPlan.phraseRole == DrillPhraseRole::Response)
            {
                slot.hatFillAllowed = slot.hatFillAllowed && (stepInBar != 15);
                slot.fxAllowed = false;
                slot.subHoldPreferred = true;
                slot.subRestartAllowed = (stepInBar <= 10);
            }
            else if (barPlan.phraseRole == DrillPhraseRole::Tension)
            {
                slot.rollAllowed = (stepInBar >= 9);
                slot.fxAllowed = (stepInBar >= 10);
                slot.subStartWeight = clamp01(slot.subStartWeight + 0.1f);
            }

            if (barPlan.densityBudget < 0.35f)
            {
                slot.hatFillAllowed = false;
                slot.fxAllowed = false;
                slot.subHoldPreferred = true;
                slot.subRestartAllowed = slot.subRestartAllowed && (stepInBar <= 8);
            }

            if (substyle == DrillSubstyle::DarkDrill)
            {
                slot.hatFillAllowed = slot.hatFillAllowed && (stepInBar >= 10);
                slot.fxAllowed = slot.fxAllowed && (stepInBar >= 12);
                slot.kickCandidateWeight = clamp01(slot.kickCandidateWeight * 0.82f);
                slot.subHoldPreferred = true;
            }
            else if (substyle == DrillSubstyle::BrooklynDrill)
            {
                if (stepInBar == 4 || stepInBar == 6 || stepInBar == 13)
                    slot.kickCandidateWeight = clamp01(slot.kickCandidateWeight + 0.16f);
            }
        }
    }

    return out;
}
} // namespace bbg
