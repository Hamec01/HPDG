#include "DrillPhrasePlanner.h"

#include <algorithm>

namespace bbg
{
namespace
{
template <size_t Size>
std::array<int, Size> makeSteps(std::initializer_list<int> steps)
{
    std::array<int, Size> out {};
    out.fill(-1);

    size_t index = 0;
    for (const auto step : steps)
    {
        if (index >= Size)
            break;

        out[index++] = step;
    }

    return out;
}

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float normalizedLaneActivity(const PatternProject& project, TrackType type)
{
    const float weight = std::clamp(laneBiasFor(project.styleInfluence, type).activityWeight, 0.55f, 1.6f);
    return clamp01((weight - 0.55f) / 1.05f);
}

DrillPhraseBarRole roleForBar(int barIndex, int totalBars)
{
    if (totalBars <= 1)
        return DrillPhraseBarRole::Statement;

    if (barIndex == totalBars - 1)
        return DrillPhraseBarRole::Release;

    if ((barIndex % 4) == 2)
        return DrillPhraseBarRole::Lift;

    if ((barIndex % 2) == 1)
        return DrillPhraseBarRole::Response;

    return DrillPhraseBarRole::Statement;
}

std::array<int, 6> hatCarrierStepsForRole(DrillPhraseBarRole role)
{
    switch (role)
    {
        case DrillPhraseBarRole::Lift: return makeSteps<6>({ 0, 3, 6, 9, 11, 14 });
        case DrillPhraseBarRole::Release: return makeSteps<6>({ 0, 3, 6, 8, 11, 15 });
        case DrillPhraseBarRole::Response: return makeSteps<6>({ 0, 3, 6, 8, 11, 14 });
        case DrillPhraseBarRole::Statement:
        default: return makeSteps<6>({ 0, 3, 6, 8, 11, 14 });
    }
}

DrillKickDensityIntent kickDensityFromScore(float score, DrillPhraseBarRole role)
{
    if (role == DrillPhraseBarRole::Release)
    {
        if (score < 0.34f)
            return DrillKickDensityIntent::Sparse;
        if (score < 0.72f)
            return DrillKickDensityIntent::Medium;
        return DrillKickDensityIntent::Push;
    }

    if (score < 0.30f)
        return DrillKickDensityIntent::Sparse;
    if (score < 0.66f)
        return DrillKickDensityIntent::Medium;
    return DrillKickDensityIntent::Push;
}

std::array<int, 4> kickTemplateStepsForBar(DrillPhraseBarRole role,
                                           DrillKickDensityIntent density,
                                           DrillLowEndIntent lowEnd)
{
    switch (role)
    {
        case DrillPhraseBarRole::Response:
            if (density == DrillKickDensityIntent::Sparse)
                return makeSteps<4>({ 0, 7 });
            if (density == DrillKickDensityIntent::Push || lowEnd == DrillLowEndIntent::Move)
                return makeSteps<4>({ 0, 15 });
            return makeSteps<4>({ 0, 10 });

        case DrillPhraseBarRole::Lift:
            if (density == DrillKickDensityIntent::Sparse)
                return makeSteps<4>({ 0, 5 });
            if (density == DrillKickDensityIntent::Push || lowEnd == DrillLowEndIntent::Move)
                return makeSteps<4>({ 0, 13 });
            return makeSteps<4>({ 0, 9 });

        case DrillPhraseBarRole::Release:
            if (density == DrillKickDensityIntent::Sparse)
                return makeSteps<4>({ 0, 14 });
            if (density == DrillKickDensityIntent::Push)
                return makeSteps<4>({ 0, 11, 15 });
            return makeSteps<4>({ 0, 15 });

        case DrillPhraseBarRole::Statement:
        default:
            if (density == DrillKickDensityIntent::Sparse)
                return makeSteps<4>({ 0 });
            if (density == DrillKickDensityIntent::Push || lowEnd == DrillLowEndIntent::Move)
                return makeSteps<4>({ 0, 14 });
            return makeSteps<4>({ 0, 10 });
    }
}

std::array<int, 2> snareAnchorStepsForBar(int barIndex, DrillPhraseBarRole role)
{
    if (role == DrillPhraseBarRole::Release)
        return makeSteps<2>({ 8, 12 });

    if (role == DrillPhraseBarRole::Lift)
        return makeSteps<2>({ 12 });

    return (barIndex % 2) == 0 ? makeSteps<2>({ 8 }) : makeSteps<2>({ 12 });
}

std::array<int, 4> supportAccentStepsForSnare(int primarySnareStep, DrillPhraseBarRole role)
{
    if (primarySnareStep >= 12)
        return makeSteps<4>({ 10, 11, 13, 14 });

    juce::ignoreUnused(role);
    return makeSteps<4>({ 6, 7, 9, 10 });
}

std::array<int, 4> lowEndAnchorStepsForRole(DrillPhraseBarRole role)
{
    switch (role)
    {
        case DrillPhraseBarRole::Lift: return makeSteps<4>({ 0, 5, 9, 12 });
        case DrillPhraseBarRole::Release: return makeSteps<4>({ 0, 8, 12, 14 });
        case DrillPhraseBarRole::Response: return makeSteps<4>({ 0, 7, 10, 12 });
        case DrillPhraseBarRole::Statement:
        default: return makeSteps<4>({ 0, 6, 10, 14 });
    }
}

DrillHatDensityIntent hatDensityFromScore(float score)
{
    if (score < 0.38f)
        return DrillHatDensityIntent::Sparse;
    if (score < 0.72f)
        return DrillHatDensityIntent::Medium;
    return DrillHatDensityIntent::Dense;
}

DrillSupportAccentIntent supportAccentFromScore(float score, DrillPhraseBarRole role)
{
    if (score < 0.26f)
        return DrillSupportAccentIntent::None;

    if (score < 0.52f)
        return DrillSupportAccentIntent::Light;

    if (role == DrillPhraseBarRole::Response && score >= 0.66f)
        return DrillSupportAccentIntent::Drag;

    if (role == DrillPhraseBarRole::Lift && score >= 0.62f)
        return DrillSupportAccentIntent::Push;

    if (role == DrillPhraseBarRole::Release && score >= 0.68f)
        return DrillSupportAccentIntent::Drag;

    return DrillSupportAccentIntent::Light;
}

DrillLowEndIntent lowEndFromScore(float score, DrillPhraseBarRole role)
{
    if (role == DrillPhraseBarRole::Release)
        return DrillLowEndIntent::Release;

    if (score < 0.30f)
        return DrillLowEndIntent::Hold;

    if (score < 0.62f)
        return DrillLowEndIntent::Anchor;

    return DrillLowEndIntent::Move;
}
} // namespace

juce::String toString(DrillPhraseBarRole role)
{
    switch (role)
    {
        case DrillPhraseBarRole::Statement: return "Statement";
        case DrillPhraseBarRole::Response: return "Response";
        case DrillPhraseBarRole::Lift: return "Lift";
        case DrillPhraseBarRole::Release: return "Release";
        default: return "Statement";
    }
}

juce::String toString(DrillHatDensityIntent intent)
{
    switch (intent)
    {
        case DrillHatDensityIntent::Sparse: return "Sparse";
        case DrillHatDensityIntent::Medium: return "Medium";
        case DrillHatDensityIntent::Dense: return "Dense";
        default: return "Medium";
    }
}

juce::String toString(DrillSupportAccentIntent intent)
{
    switch (intent)
    {
        case DrillSupportAccentIntent::None: return "None";
        case DrillSupportAccentIntent::Light: return "Light";
        case DrillSupportAccentIntent::Push: return "Push";
        case DrillSupportAccentIntent::Drag: return "Drag";
        default: return "Light";
    }
}

juce::String toString(DrillKickDensityIntent intent)
{
    switch (intent)
    {
        case DrillKickDensityIntent::Sparse: return "Sparse";
        case DrillKickDensityIntent::Medium: return "Medium";
        case DrillKickDensityIntent::Push: return "Push";
        default: return "Medium";
    }
}

juce::String toString(DrillLowEndIntent intent)
{
    switch (intent)
    {
        case DrillLowEndIntent::Hold: return "Hold";
        case DrillLowEndIntent::Anchor: return "Anchor";
        case DrillLowEndIntent::Move: return "Move";
        case DrillLowEndIntent::Release: return "Release";
        default: return "Anchor";
    }
}

DrillPhrasePlan DrillPhrasePlanner::buildPlan(const PatternProject& project)
{
    DrillPhrasePlan plan;
    plan.phraseSpanBars = std::max(1, project.params.bars);
    plan.substyleIndex = std::max(0, project.params.drillSubstyle);
    plan.bars.reserve(static_cast<size_t>(plan.phraseSpanBars));

    const float hatBase = clamp01(0.30f
                                  + 0.34f * clamp01(project.params.densityAmount)
                                  + 0.22f * normalizedLaneActivity(project, TrackType::HiHat)
                                  + 0.14f * clamp01(project.styleInfluence.drillHatDensityVariationWeight * 0.5f));
    const float supportBase = clamp01(0.12f
                                      + 0.34f * clamp01(project.styleInfluence.supportAccentWeight * 0.5f)
                                      + 0.24f * normalizedLaneActivity(project, TrackType::ClapGhostSnare));
    const float kickBase = clamp01(0.08f
                                   + 0.20f * clamp01(project.params.densityAmount)
                                   + 0.28f * normalizedLaneActivity(project, TrackType::Kick)
                                   + 0.24f * clamp01(project.styleInfluence.lowEndCouplingWeight * 0.5f)
                                   + 0.10f * normalizedLaneActivity(project, TrackType::Sub808));
    const float lowEndBase = clamp01(0.18f
                                     + 0.36f * clamp01(project.styleInfluence.lowEndCouplingWeight * 0.5f)
                                     + 0.24f * normalizedLaneActivity(project, TrackType::Sub808));

    juce::StringArray summaryParts;
    for (int barIndex = 0; barIndex < plan.phraseSpanBars; ++barIndex)
    {
        DrillPhraseBarPlan bar;
        bar.barIndex = barIndex;
        bar.role = roleForBar(barIndex, plan.phraseSpanBars);
        bar.isPhraseStart = barIndex == 0;
        bar.isPhraseEnd = barIndex == (plan.phraseSpanBars - 1);
        bar.isStrongBar = bar.isPhraseStart || (bar.role == DrillPhraseBarRole::Lift) || ((barIndex % 2) == 0);
        bar.isWeakBar = !bar.isStrongBar;

        bar.anchorMap.hatCarrierSteps = hatCarrierStepsForRole(bar.role);
        bar.anchorMap.snareAnchorSteps = snareAnchorStepsForBar(barIndex, bar.role);
        bar.anchorMap.lowEndAnchorSteps = lowEndAnchorStepsForRole(bar.role);
        bar.anchorMap.supportAccentSteps = supportAccentStepsForSnare(bar.anchorMap.snareAnchorSteps[0], bar.role);

        float hatScore = hatBase;
        if (bar.role == DrillPhraseBarRole::Lift)
            hatScore += 0.12f;
        else if (bar.role == DrillPhraseBarRole::Release)
            hatScore -= 0.08f;
        bar.hatDensity = hatDensityFromScore(clamp01(hatScore));

        float supportScore = supportBase;
        if (bar.role == DrillPhraseBarRole::Response)
            supportScore += 0.16f;
        else if (bar.role == DrillPhraseBarRole::Lift)
            supportScore += 0.10f;
        bar.supportAccent = supportAccentFromScore(clamp01(supportScore), bar.role);

        float lowEndScore = lowEndBase;
        if (bar.role == DrillPhraseBarRole::Lift)
            lowEndScore += 0.10f;
        else if (bar.role == DrillPhraseBarRole::Response)
            lowEndScore += 0.05f;
        else if (bar.role == DrillPhraseBarRole::Release)
            lowEndScore -= 0.06f;
        bar.lowEnd = lowEndFromScore(clamp01(lowEndScore), bar.role);

        float kickScore = kickBase;
        if (bar.role == DrillPhraseBarRole::Lift)
            kickScore += 0.12f;
        else if (bar.role == DrillPhraseBarRole::Release)
            kickScore += 0.18f;
        else if (bar.role == DrillPhraseBarRole::Response)
            kickScore += 0.06f;

        if (bar.lowEnd == DrillLowEndIntent::Move)
            kickScore += 0.08f;
        else if (bar.lowEnd == DrillLowEndIntent::Hold)
            kickScore -= 0.06f;

        bar.kickDensity = kickDensityFromScore(clamp01(kickScore), bar.role);
        bar.anchorMap.kickAnchorSteps = kickTemplateStepsForBar(bar.role, bar.kickDensity, bar.lowEnd);

        summaryParts.add("B" + juce::String(bar.barIndex + 1)
                         + ":" + toString(bar.role)
                         + "/" + toString(bar.hatDensity)
                         + "/" + toString(bar.supportAccent)
                         + "/" + toString(bar.kickDensity)
                         + "/" + toString(bar.lowEnd));
        plan.bars.push_back(bar);
    }

    plan.summary = summaryParts.joinIntoString(" | ");
    return plan;
}
} // namespace bbg