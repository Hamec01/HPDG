#include "Vst3GridLiteComponent.h"

#include <algorithm>
#include <cmath>

#include "../Core/ProjectLaneAccess.h"
#include "../Core/TrackRegistry.h"
#include "SketchDrawing.h"
#include "SketchFonts.h"
#include "SketchTheme.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
constexpr int kRulerHeight = 28;
constexpr int kMinStepPixelWidth = 4;
constexpr int kOuterPadding = 6;
constexpr int kLaneLabelWidth = 96;

juce::Colour roleColour(const juce::String& role, bool selectedLane)
{
    const auto normalized = role.trim().toLowerCase();
    if (normalized == "support")
        return selectedLane ? sketch::Theme::ochre() : sketch::Theme::blue();
    if (normalized == "accent")
        return selectedLane ? sketch::Theme::ochre() : sketch::Theme::blue();
    if (normalized == "fill")
        return sketch::Theme::ochre();
    if (normalized == "anchor")
        return sketch::Theme::ochre();

    return selectedLane ? sketch::Theme::ochre() : sketch::Theme::graphite();
}

void drawHatchedNote(juce::Graphics& g, juce::Rectangle<float> rect, juce::Colour colour,
                     bool ghost, int seed)
{
    g.setColour(colour.withAlpha(ghost ? 0.22f : 0.72f));
    g.fillRect(rect);
    sketch::drawFrame(g, rect, sketch::Theme::graphite().withAlpha(ghost ? 0.58f : 0.86f),
                      1.05f, seed, 0.8f);

    const float spacing = ghost ? 5.0f : 3.5f;
    g.saveState();
    g.reduceClipRegion(rect.getSmallestIntegerContainer());
    g.setColour(sketch::Theme::graphite().withAlpha(ghost ? 0.34f : 0.56f));
    for (float x = rect.getX() - rect.getHeight(); x < rect.getRight(); x += spacing)
        g.drawLine(x, rect.getBottom(), x + rect.getHeight(), rect.getY(), ghost ? 0.65f : 0.9f);
    g.restoreState();
}
} // namespace

void Vst3GridLiteComponent::setProject(const PatternProject& value)
{
    project = value;
    repaint();
}

void Vst3GridLiteComponent::setLaneDisplayOrder(const std::vector<RuntimeLaneId>& order)
{
    laneDisplayOrder = order;
    repaint();
}

void Vst3GridLiteComponent::setSelectedTrack(const RuntimeLaneId& laneId)
{
    if (selectedTrack == laneId)
        return;

    selectedTrack = laneId;
    repaint();
}

void Vst3GridLiteComponent::setRowHeight(int value)
{
    const int next = juce::jlimit(22, 80, value);
    if (rowHeight == next)
        return;

    rowHeight = next;
    repaint();
}

void Vst3GridLiteComponent::setPlayheadStep(float step)
{
    if (std::abs(playheadStep - step) < 0.001f)
        return;

    playheadStep = step;
    repaint();
}

void Vst3GridLiteComponent::setPreviewStartStep(int step)
{
    const int next = juce::jmax(0, step);
    if (previewStartStep == next)
        return;

    previewStartStep = next;
    repaint();
}

void Vst3GridLiteComponent::setLoopRegion(const std::optional<juce::Range<int>>& tickRange)
{
    if (loopRegionTicks == tickRange)
        return;

    loopRegionTicks = tickRange;
    repaint();
}

int Vst3GridLiteComponent::getPreferredContentHeight() const
{
    return kRulerHeight + rowHeight * static_cast<int>(orderedVisibleLanes().size()) + kOuterPadding * 2;
}

std::vector<Vst3GridLiteComponent::VisibleLane> Vst3GridLiteComponent::orderedVisibleLanes() const
{
    std::vector<VisibleLane> lanes;
    lanes.reserve(project.runtimeLaneProfile.lanes.size());

    auto pushLane = [&lanes, this](const RuntimeLaneId& laneId)
    {
        const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, laneId);
        if (lane == nullptr || !lane->isVisibleInEditor)
            return;

        const auto exists = std::any_of(lanes.begin(), lanes.end(), [&laneId](const VisibleLane& entry)
        {
            return entry.laneId == laneId;
        });

        if (!exists)
            lanes.push_back({ lane->laneId, lane->laneName, lane->runtimeTrackType });
    };

    for (const auto& laneId : laneDisplayOrder)
        pushLane(laneId);

    for (const auto& lane : project.runtimeLaneProfile.lanes)
        pushLane(lane.laneId);

    return lanes;
}

juce::Colour Vst3GridLiteComponent::laneAccentColour(const VisibleLane& lane) const
{
    if (!lane.trackType.has_value())
        return juce::Colour::fromRGB(84, 112, 140);

    switch (*lane.trackType)
    {
        case TrackType::Kick:
        case TrackType::GhostKick:
            return juce::Colour::fromRGB(235, 142, 82);
        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            return juce::Colour::fromRGB(236, 172, 104);
        case TrackType::HiHat:
        case TrackType::HatFX:
        case TrackType::OpenHat:
            return juce::Colour::fromRGB(106, 176, 236);
        case TrackType::Perc:
            return juce::Colour::fromRGB(112, 190, 154);
        case TrackType::Ride:
        case TrackType::Cymbal:
            return juce::Colour::fromRGB(184, 170, 112);
        case TrackType::Sub808:
            return juce::Colour::fromRGB(132, 156, 226);
    }

    return juce::Colour::fromRGB(92, 126, 158);
}

juce::Colour Vst3GridLiteComponent::noteColour(const NoteEvent& note, bool selectedLane) const
{
    const float velocityAlpha = juce::jmap(static_cast<float>(juce::jlimit(1, 127, note.velocity)),
                                           1.0f,
                                           127.0f,
                                           0.42f,
                                           0.96f);
    return roleColour(note.semanticRole, selectedLane).withAlpha(velocityAlpha);
}

std::optional<int> Vst3GridLiteComponent::laneIndexAtY(int yInGrid, int effectiveRowHeight, int laneCount) const
{
    if (yInGrid < 0 || laneCount <= 0)
        return std::nullopt;

    const int row = yInGrid / juce::jmax(1, effectiveRowHeight);
    if (row < 0 || row >= laneCount)
        return std::nullopt;

    return row;
}

void Vst3GridLiteComponent::paint(juce::Graphics& g)
{
    g.fillAll(sketch::Theme::paper());

    const auto lanes = orderedVisibleLanes();
    const int laneCount = static_cast<int>(lanes.size());
    const int bars = juce::jmax(1, project.params.bars);
    const int totalSteps = juce::jmax(1, bars * 16);

    auto bounds = getLocalBounds().reduced(kOuterPadding);
    if (bounds.getWidth() <= kLaneLabelWidth + 24 || bounds.getHeight() <= kRulerHeight || laneCount <= 0)
        return;

    const auto frame = bounds.toFloat();
    sketch::dropShadow(g, frame, 4.0f, 3.0f, 0.9f);
    g.setGradientFill(sketch::raisedPaperGradient(frame, sketch::Theme::paperLight().brighter(0.05f), sketch::Theme::paper()));
    g.fillRoundedRectangle(frame, 3.0f);
    sketch::drawFrame(g, frame, sketch::Theme::graphiteSoft(), 1.2f, getWidth() + getHeight(), 3.0f);

    auto rulerArea = bounds.removeFromTop(kRulerHeight);
    auto labelArea = bounds.removeFromLeft(kLaneLabelWidth);
    auto gridArea = bounds;
    const int resolvedRowHeight = juce::jmax(22, rowHeight);
    const float stepWidth = juce::jmax(static_cast<float>(kMinStepPixelWidth),
                                       static_cast<float>(gridArea.getWidth()) / static_cast<float>(totalSteps));

    g.setGradientFill(sketch::raisedPaperGradient(rulerArea.toFloat(), sketch::Theme::ochreWash().withMultipliedAlpha(1.15f),
                                                  sketch::Theme::ochreWash()));
    g.fillRect(rulerArea);
    g.setColour(sketch::Theme::graphiteSoft().withAlpha(0.4f));
    g.drawLine(0.0f, static_cast<float>(rulerArea.getBottom()) - 0.5f, static_cast<float>(getWidth()), static_cast<float>(rulerArea.getBottom()) - 0.5f, 0.6f);
    g.setColour(sketch::Theme::paperLight());
    g.fillRect(rulerArea.withTrimmedLeft(kLaneLabelWidth));
    g.setColour(sketch::Theme::graphite());
    g.setFont(sketch::notebookFont(19.0f, true));
    g.drawText("Pattern", rulerArea.removeFromLeft(kLaneLabelWidth).reduced(9, 1), juce::Justification::centredLeft);
    sketch::drawLine(g, { frame.getX(), static_cast<float>(gridArea.getY()) },
                     { frame.getRight(), static_cast<float>(gridArea.getY()) }, sketch::Theme::graphiteSoft(), 1.0f, 41);
    sketch::drawLine(g, { static_cast<float>(gridArea.getX()), static_cast<float>(gridArea.getY()) },
                     { static_cast<float>(gridArea.getX()), static_cast<float>(gridArea.getBottom()) }, sketch::Theme::ochre(), 1.5f, 43);

    for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex)
    {
        const auto& lane = lanes[static_cast<size_t>(laneIndex)];
        const int y = gridArea.getY() + laneIndex * resolvedRowHeight;
        const auto row = juce::Rectangle<int>(gridArea.getX(), y, gridArea.getWidth(), resolvedRowHeight);
        const bool selectedLane = lane.laneId == selectedTrack;

        g.setColour(selectedLane ? sketch::Theme::blueWash().withAlpha(0.34f)
                                 : sketch::Theme::paperLight().withAlpha((laneIndex % 2) == 0 ? 0.32f : 0.12f));
        g.fillRect(row);
        g.setColour(sketch::Theme::graphite());
        g.setFont(sketch::notebookFont(selectedLane ? 14.0f : 13.5f, selectedLane));
        g.drawFittedText(lane.laneName, labelArea.withY(y).withHeight(resolvedRowHeight).reduced(9, 1),
                         juce::Justification::centredLeft, 1);
        sketch::drawLine(g, { static_cast<float>(labelArea.getX()), static_cast<float>(row.getBottom()) },
                         { static_cast<float>(row.getRight()), static_cast<float>(row.getBottom()) },
                         sketch::Theme::gridLine(), 0.75f, 100 + laneIndex);
    }

    for (int step = 0; step <= totalSteps; ++step)
    {
        const float x = static_cast<float>(gridArea.getX()) + static_cast<float>(step) * stepWidth;
        const bool barLine = step % 16 == 0;
        const bool beatLine = step % 4 == 0;
        g.setColour(barLine ? sketch::Theme::graphiteSoft().withAlpha(0.54f)
                            : (beatLine ? sketch::Theme::gridLine().withAlpha(0.62f)
                                        : sketch::Theme::gridLine().withAlpha(0.34f)));
        g.drawVerticalLine(static_cast<int>(std::round(x)),
                           static_cast<float>(rulerArea.getY()),
                           static_cast<float>(gridArea.getBottom()));
    }

    g.setFont(sketch::notebookFont(12.0f));
    g.setColour(sketch::Theme::graphiteSoft());
    for (int beat = 0; beat < bars * 4; ++beat)
    {
        const int step = beat * 4;
        const int x = static_cast<int>(std::round(static_cast<float>(gridArea.getX()) + static_cast<float>(step) * stepWidth));
        g.drawText(juce::String((beat % 4) + 1),
                   x + 3,
                   rulerArea.getY(),
                   22,
                   rulerArea.getHeight(),
                   juce::Justification::centredLeft,
                   true);
    }

    if (loopRegionTicks.has_value() && loopRegionTicks->getLength() > 0)
    {
        const float startStep = static_cast<float>(loopRegionTicks->getStart()) / static_cast<float>(ticksPerStep());
        const float endStep = static_cast<float>(loopRegionTicks->getEnd()) / static_cast<float>(ticksPerStep());
        const float x = static_cast<float>(gridArea.getX()) + startStep * stepWidth;
        const float width = juce::jmax(2.0f, (endStep - startStep) * stepWidth);
        const auto loopRect = juce::Rectangle<float>(x,
                                                     static_cast<float>(gridArea.getY()),
                                                     width,
                                                     static_cast<float>(gridArea.getHeight()));
        g.setColour(sketch::Theme::blueWash().withAlpha(0.22f));
        g.fillRect(loopRect);
        g.setColour(sketch::Theme::blue().withAlpha(0.68f));
        g.drawRect(loopRect, 1.0f);
    }

    for (const auto& track : project.tracks)
    {
        const auto laneIt = std::find_if(lanes.begin(), lanes.end(), [&track](const VisibleLane& lane)
        {
            return lane.laneId == track.laneId;
        });

        if (laneIt == lanes.end())
            continue;

        const int laneIndex = static_cast<int>(std::distance(lanes.begin(), laneIt));
        const bool selectedLane = laneIt->laneId == selectedTrack;
        const float y = static_cast<float>(gridArea.getY() + laneIndex * resolvedRowHeight);

        for (const auto& note : track.notes)
        {
            const float startStep = static_cast<float>(note.startTick()) / static_cast<float>(ticksPerStep());
            const float x = static_cast<float>(gridArea.getX()) + startStep * stepWidth;
            const float maxWidth = static_cast<float>(gridArea.getRight()) - x - 2.0f;
            if (maxWidth <= 0.0f)
                continue;

            const float lengthSteps = static_cast<float>(juce::jmax(1, note.lengthTicks)) / static_cast<float>(ticksPerStep());
            const float width = juce::jmin(maxWidth, juce::jmax(4.0f, lengthSteps * stepWidth - 2.0f));
            const auto rect = juce::Rectangle<float>(x + 1.0f,
                                                     y + 4.0f,
                                                     width,
                                                     static_cast<float>(resolvedRowHeight - 8));
            const auto colour = noteColour(note, selectedLane);

            drawHatchedNote(g, rect, colour, note.isGhost,
                            note.gridTick + laneIndex * 19 + note.velocity);
        }
    }

    const float previewX = static_cast<float>(gridArea.getX()) + static_cast<float>(previewStartStep) * stepWidth;
    sketch::drawLine(g, { previewX, static_cast<float>(gridArea.getY() - kRulerHeight) },
                     { previewX, static_cast<float>(gridArea.getBottom()) }, sketch::Theme::ochre().withAlpha(0.82f), 1.45f, previewStartStep + 211);

    if (playheadStep >= 0.0f)
    {
        const float playheadX = static_cast<float>(gridArea.getX()) + playheadStep * stepWidth;
        sketch::drawLine(g, { playheadX, static_cast<float>(gridArea.getY() - kRulerHeight) },
                         { playheadX, static_cast<float>(gridArea.getBottom()) }, sketch::Theme::blue(), 2.1f, 313);
        juce::Path nib;
        nib.addTriangle(playheadX - 4.0f, static_cast<float>(gridArea.getY() - kRulerHeight),
                        playheadX + 4.0f, static_cast<float>(gridArea.getY() - kRulerHeight),
                        playheadX, static_cast<float>(gridArea.getY() - kRulerHeight + 7));
        g.setColour(sketch::Theme::blue());
        g.fillPath(nib);
    }
}

void Vst3GridLiteComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto lanes = orderedVisibleLanes();
    const int laneCount = static_cast<int>(lanes.size());
    const int bars = juce::jmax(1, project.params.bars);
    const int totalSteps = juce::jmax(1, bars * 16);

    auto bounds = getLocalBounds().reduced(kOuterPadding);
    if (laneCount <= 0 || bounds.getWidth() <= kLaneLabelWidth + 24 || bounds.getHeight() <= kRulerHeight)
        return;

    bounds.removeFromTop(kRulerHeight);
    const int resolvedRowHeight = juce::jmax(22, rowHeight);
    const auto laneIndex = laneIndexAtY(event.y - bounds.getY(), resolvedRowHeight, laneCount);
    if (!laneIndex.has_value())
        return;

    const auto& lane = lanes[static_cast<size_t>(*laneIndex)];
    if (onLaneClicked)
        onLaneClicked(lane.laneId);

    auto gridBounds = bounds.withTrimmedLeft(kLaneLabelWidth);
    if (event.x < gridBounds.getX())
        return;

    const float stepWidth = juce::jmax(static_cast<float>(kMinStepPixelWidth),
                                       static_cast<float>(gridBounds.getWidth()) / static_cast<float>(totalSteps));
    const float stepFloat = (static_cast<float>(event.x) - static_cast<float>(gridBounds.getX())) / stepWidth;
    const int step = juce::jlimit(0, totalSteps - 1, static_cast<int>(std::floor(stepFloat)));

    if (onStepClicked)
        onStepClicked(step);
}
} // namespace bbg
