#include "Vst3GridLiteComponent.h"

#include <algorithm>
#include <cmath>

#include "../Core/ProjectLaneAccess.h"
#include "../Core/TrackRegistry.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
constexpr int kRulerHeight = 24;
constexpr int kMinStepPixelWidth = 4;
constexpr int kOuterPadding = 6;

juce::Colour roleColour(const juce::String& role, bool selectedLane)
{
    const auto normalized = role.trim().toLowerCase();
    if (normalized == "support")
        return selectedLane ? juce::Colour::fromRGB(126, 194, 255) : juce::Colour::fromRGB(88, 158, 226);
    if (normalized == "accent")
        return selectedLane ? juce::Colour::fromRGB(205, 166, 255) : juce::Colour::fromRGB(160, 130, 220);
    if (normalized == "fill")
        return selectedLane ? juce::Colour::fromRGB(255, 184, 104) : juce::Colour::fromRGB(226, 145, 78);
    if (normalized == "anchor")
        return selectedLane ? juce::Colour::fromRGB(255, 212, 132) : juce::Colour::fromRGB(226, 164, 88);

    return selectedLane ? juce::Colour::fromRGB(242, 168, 96) : juce::Colour::fromRGB(204, 132, 72);
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
    g.fillAll(juce::Colour::fromRGB(12, 13, 16));

    const auto lanes = orderedVisibleLanes();
    const int laneCount = static_cast<int>(lanes.size());
    const int bars = juce::jmax(1, project.params.bars);
    const int totalSteps = juce::jmax(1, bars * 16);

    auto bounds = getLocalBounds().reduced(kOuterPadding);
    if (bounds.getWidth() <= 24 || bounds.getHeight() <= kRulerHeight || laneCount <= 0)
        return;

    const auto frame = bounds.toFloat();
    juce::ColourGradient fill(juce::Colour::fromRGB(27, 23, 20), frame.getTopLeft(),
                              juce::Colour::fromRGB(12, 13, 16), frame.getBottomLeft(), false);
    fill.addColour(0.32, juce::Colour::fromRGB(32, 27, 24));
    fill.addColour(0.70, juce::Colour::fromRGB(17, 18, 21));
    g.setGradientFill(fill);
    g.fillRoundedRectangle(frame, 7.0f);

    auto rulerArea = bounds.removeFromTop(kRulerHeight);
    auto gridArea = bounds;
    const int resolvedRowHeight = juce::jmax(22, rowHeight);
    const float stepWidth = juce::jmax(static_cast<float>(kMinStepPixelWidth),
                                       static_cast<float>(gridArea.getWidth()) / static_cast<float>(totalSteps));

    g.setColour(juce::Colour::fromRGB(44, 34, 25));
    g.fillRect(rulerArea);
    g.setColour(juce::Colour::fromRGBA(246, 190, 108, 66));
    g.drawLine(static_cast<float>(rulerArea.getX()),
               static_cast<float>(rulerArea.getBottom()),
               static_cast<float>(rulerArea.getRight()),
               static_cast<float>(rulerArea.getBottom()),
               1.0f);

    for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex)
    {
        const auto& lane = lanes[static_cast<size_t>(laneIndex)];
        const int y = gridArea.getY() + laneIndex * resolvedRowHeight;
        const auto row = juce::Rectangle<int>(gridArea.getX(), y, gridArea.getWidth(), resolvedRowHeight);
        const bool selectedLane = lane.laneId == selectedTrack;

        g.setColour(selectedLane ? juce::Colour::fromRGBA(52, 67, 92, 112)
                                 : juce::Colour::fromRGBA(26, 28, 32, 82));
        g.fillRect(row);

        g.setColour(laneAccentColour(lane).withAlpha(selectedLane ? 0.95f : 0.58f));
        g.fillRect(row.withWidth(4).reduced(0, 3));

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 12));
        g.drawHorizontalLine(row.getBottom() - 1, static_cast<float>(row.getX()), static_cast<float>(row.getRight()));
    }

    for (int step = 0; step <= totalSteps; ++step)
    {
        const float x = static_cast<float>(gridArea.getX()) + static_cast<float>(step) * stepWidth;
        const bool barLine = step % 16 == 0;
        const bool beatLine = step % 4 == 0;
        g.setColour(barLine ? juce::Colour::fromRGBA(255, 210, 142, 58)
                            : (beatLine ? juce::Colour::fromRGBA(255, 255, 255, 24)
                                        : juce::Colour::fromRGBA(255, 255, 255, 10)));
        g.drawVerticalLine(static_cast<int>(std::round(x)),
                           static_cast<float>(rulerArea.getY()),
                           static_cast<float>(gridArea.getBottom()));
    }

    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.setColour(juce::Colour::fromRGB(220, 190, 136));
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
        g.setColour(juce::Colour::fromRGBA(92, 198, 255, 20));
        g.fillRect(loopRect);
        g.setColour(juce::Colour::fromRGBA(110, 208, 255, 72));
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
            const float startStep = static_cast<float>(note.step)
                + static_cast<float>(note.microOffset) / static_cast<float>(ticksPerStep());
            const float x = static_cast<float>(gridArea.getX()) + startStep * stepWidth;
            const float maxWidth = static_cast<float>(gridArea.getRight()) - x - 2.0f;
            if (maxWidth <= 0.0f)
                continue;

            const float width = juce::jmin(maxWidth, juce::jmax(4.0f, static_cast<float>(juce::jmax(1, note.length)) * stepWidth - 2.0f));
            const auto rect = juce::Rectangle<float>(x + 1.0f,
                                                     y + 4.0f,
                                                     width,
                                                     static_cast<float>(resolvedRowHeight - 8));
            const auto colour = noteColour(note, selectedLane);

            g.setColour(colour.withAlpha(0.16f));
            g.fillRoundedRectangle(rect.expanded(2.0f, 1.0f), 4.0f);
            g.setColour(colour);
            g.fillRoundedRectangle(rect, 3.0f);
            g.setColour(juce::Colours::white.withAlpha(selectedLane ? 0.34f : 0.18f));
            g.drawRoundedRectangle(rect, 3.0f, 1.0f);
        }
    }

    const float previewX = static_cast<float>(gridArea.getX()) + static_cast<float>(previewStartStep) * stepWidth;
    g.setColour(juce::Colour::fromRGBA(255, 196, 116, 118));
    g.drawLine(previewX, static_cast<float>(rulerArea.getY()), previewX, static_cast<float>(gridArea.getBottom()), 1.5f);

    if (playheadStep >= 0.0f)
    {
        const float playheadX = static_cast<float>(gridArea.getX()) + playheadStep * stepWidth;
        g.setColour(juce::Colour::fromRGB(102, 210, 255));
        g.drawLine(playheadX, static_cast<float>(rulerArea.getY()), playheadX, static_cast<float>(gridArea.getBottom()), 2.0f);
    }

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 22));
    g.drawRoundedRectangle(frame, 7.0f, 1.0f);
}

void Vst3GridLiteComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto lanes = orderedVisibleLanes();
    const int laneCount = static_cast<int>(lanes.size());
    const int bars = juce::jmax(1, project.params.bars);
    const int totalSteps = juce::jmax(1, bars * 16);

    auto bounds = getLocalBounds().reduced(kOuterPadding);
    if (laneCount <= 0 || bounds.getWidth() <= 24 || bounds.getHeight() <= kRulerHeight)
        return;

    bounds.removeFromTop(kRulerHeight);
    const int resolvedRowHeight = juce::jmax(22, rowHeight);
    const auto laneIndex = laneIndexAtY(event.y - bounds.getY(), resolvedRowHeight, laneCount);
    if (!laneIndex.has_value())
        return;

    const auto& lane = lanes[static_cast<size_t>(*laneIndex)];
    if (onLaneClicked)
        onLaneClicked(lane.laneId);

    const float stepWidth = juce::jmax(static_cast<float>(kMinStepPixelWidth),
                                       static_cast<float>(bounds.getWidth()) / static_cast<float>(totalSteps));
    const float stepFloat = (static_cast<float>(event.x) - static_cast<float>(bounds.getX())) / stepWidth;
    const int step = juce::jlimit(0, totalSteps - 1, static_cast<int>(std::floor(stepFloat)));

    if (onStepClicked)
        onStepClicked(step);
}
} // namespace bbg
