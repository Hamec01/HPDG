#pragma once

#include <unordered_set>
#include <vector>

#include "DrillPhrasePlanner.h"
#include "DrillStyleProfile.h"
#include "../../Core/TrackState.h"

namespace bbg
{
enum class DrillKickEventType
{
    None = 0,
    AnchorKick,
    SupportKick,
    PhraseEdgeKick
};

struct DrillGrooveSlot
{
    bool snareAnchor = false;
    bool snareGhostAllowed = true;
    bool snareProtection = false;

    float kickCandidateWeight = 0.5f;
    bool kickForbidden = false;
    bool kickPlaced = false;
    DrillKickEventType kickEventType = DrillKickEventType::None;

    float hatBackboneWeight = 0.5f;
    bool hatFillAllowed = true;
    bool rollAllowed = true;
    bool accentAllowed = true;
    bool fxAllowed = true;
    bool openHatAllowed = true;
    bool percAllowed = true;

    float subStartWeight = 0.5f;
    bool subRestartAllowed = true;
    bool subHoldPreferred = false;
    bool slideAllowed = true;

    float phraseEdgeWeight = 0.0f;
    bool majorEventReserved = false;
};

struct DrillGrooveBarPlan
{
    DrillPhraseRole phraseRole = DrillPhraseRole::Statement;
    float energyLevel = 0.5f;
    float densityBudget = 0.5f;
    float variationBudget = 0.5f;
    bool isTransitionBar = false;
    bool isEndingBar = false;
};

struct DrillGrooveBlueprint
{
    int bars = 0;
    int stepsPerBar = 16;
    std::vector<DrillGrooveBarPlan> barPlans;
    std::vector<DrillGrooveSlot> slots;

    const DrillGrooveSlot* slotAt(int absoluteStep) const;
    DrillGrooveSlot* slotAt(int absoluteStep);

    void reserveMajorWindow(int absoluteStep, int radius);
    bool hasMajorReservedNear(int absoluteStep, int radius) const;

    void ingestSnareTrack(const TrackState& snareTrack, int protectionRadius);
    void ingestKickTrack(const TrackState& kickTrack);
};

class DrillGrooveBlueprintBuilder
{
public:
    static DrillGrooveBlueprint build(const DrillPhrasePlan& phrasePlan,
                                      int bars,
                                      DrillSubstyle substyle,
                                      float densityAmount);
};
} // namespace bbg
