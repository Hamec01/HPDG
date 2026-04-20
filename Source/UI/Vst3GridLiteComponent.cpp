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
constexpr int kLaneStripWidth = 18;
constexpr int kRulerHeight = 24;
constexpr int kMinStepPixelWidth = 3;

void drawEmberTexture(juce::Graphics& g, juce::Rectangle<float> area, float alphaScale)
{
    static const juce::File kTextureFile("C:/Users/ham/Documents/DRUMENGINE/Assets/hpdg_ember_texture.png");
    if (kTextureFile.existsAsFile())
    {
        if (auto texture = juce::ImageCache::getFromFile(kTextureFile); texture.isValid())
        {
            g.saveState();
            g.reduceClipRegion(area.getSmallestIntegerContainer());
            g.setOpacity(0.16f * alphaScale);
            g.drawImage(texture,
                        area.getX(),
                        area.getY(),
                        area.getWidth(),
                        area.getHeight(),
                        0.0f,
                        0.0f,
                        static_cast<float>(texture.getWidth()),
                        static_cast<float>(texture.getHeight()));
            g.restoreState();
        }
    }

    juce::Random rng(0x48474433);

    for (int i = 0; i < 24; ++i)
    {
        const float x = area.getX() + rng.nextFloat() * area.getWidth();
        const float y = area.getY() + rng.nextFloat() * area.getHeight();
        const float w = 46.0f + rng.nextFloat() * 180.0f;
        const float h = 8.0f + rng.nextFloat() * 34.0f;
        const float alpha = (0.018f + rng.nextFloat() * 0.040f) * alphaScale;
        g.setColour(juce::Colour::fromRGBA(255, 171, 74, static_cast<juce::uint8>(alpha * 255.0f)));
        g.fillEllipse(x, y, w, h);
    }

    for (int i = 0; i < 18; ++i)
    {
        juce::Path wisp;
        const float startX = area.getX() + rng.nextFloat() * area.getWidth();
        const float startY = area.getY() + rng.nextFloat() * area.getHeight();
        wisp.startNewSubPath(startX, startY);
        for (int segment = 0; segment < 3; ++segment)
        {
            const float cx = area.getX() + rng.nextFloat() * area.getWidth();
            const float cy = area.getY() + rng.nextFloat() * area.getHeight();
            const float ex = area.getX() + rng.nextFloat() * area.getWidth();
            const float ey = area.getY() + rng.nextFloat() * area.getHeight();
            wisp.quadraticTo(cx, cy, ex, ey);
        }

        g.setColour(juce::Colour::fromRGBA(255, 210, 158,
                                           static_cast<juce::uint8>((0.012f + rng.nextFloat() * 0.022f) * alphaScale * 255.0f)));
        g.strokePath(wisp, juce::PathStrokeType(1.2f + rng.nextFloat() * 2.0f,
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
}

juce::Colour roleColour(const juce::String& role, bool selectedLane)
{
    const auto normalized = role.trim().toLowerCase();
    if (normalized == "support")
        return selectedLane ? juce::Colour::fromRGB(124, 198, 255) : juce::Colour::fromRGB(98, 172, 242);
    if (normalized == "accent")
        return selectedLane ? juce::Colour::fromRGB(198, 164, 255) : juce::Colour::fromRGB(166, 138, 236);
    if (normalized == "fill")
        return selectedLane ? juce::Colour::fromRGB(255, 188, 110) : juce::Colour::fromRGB(244, 174, 96);
    if (normalized == "anchor")
        return selectedLane ? juce::Colour::fromRGB(255, 214, 128) : juce::Colour::fromRGB(252, 196, 112);

    return selectedLane ? juce::Colour::fromRGB(255, 190, 110) : juce::Colour::fromRGB(242, 176, 102);
}
}

void Vst3GridLiteComponent::drawNoteGlow(juce::Graphics& g, const juce::Rectangle<float>& rect, juce::Colour colour) const
{
    g.setColour(colour.withAlpha(0.12f));
    g.fillRoundedRectangle(rect.expanded(3.0f, 2.0f), 4.8f);
    g.setColour(colour.withAlpha(0.20f));
    g.fillRoundedRectangle(rect.expanded(1.4f, 1.0f), 4.0f);
}

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
    return kRulerHeight + rowHeight * static_cast<int>(orderedVisibleLanes().size());
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

        if (std::find_if(lanes.begin(), lanes.end(), [&laneId](const VisibleLane& entry)
            {
                return entry.laneId == laneId;
            }) != lanes.end())
        {
            return;
        }

        lanes.push_back({ lane->laneId, lane->laneName, lane->runtimeTrackType });
    };

    for (const auto& laneId : laneDisplayOrder)
        pushLane(laneId);

    for (const auto& lane : project.runtimeLaneProfile.lanes)
        pushLane(lane.laneId);

    return lanes;
}

juce::String Vst3GridLiteComponent::laneDisplayName(const VisibleLane& lane) const
{
    if (lane.laneName.isNotEmpty())
        return lane.laneName;

    if (lane.trackType.has_value())
    {
        if (const auto* info = TrackRegistry::find(*lane.trackType))
            return info->displayName;
    }

    return lane.laneId;
}

juce::Colour Vst3GridLiteComponent::laneAccentColour(const VisibleLane& lane) const
{
    if (!lane.trackType.has_value())
        return juce::Colour::fromRGB(86, 114, 142);

    switch (*lane.trackType)
    {
        case TrackType::Kick: return juce::Colour::fromRGB(242, 152, 92);
        case TrackType::Snare: return juce::Colour::fromRGB(238, 168, 102);
        case TrackType::HiHat: return juce::Colour::fromRGB(120, 178, 242);
        case TrackType::HatFX: return juce::Colour::fromRGB(144, 198, 255);
        case TrackType::OpenHat: return juce::Colour::fromRGB(116, 170, 232);
        case TrackType::Perc: return juce::Colour::fromRGB(120, 196, 164);
        case TrackType::Ride:
        case TrackType::Cymbal: return juce::Colour::fromRGB(188, 172, 112);
        default: return juce::Colour::fromRGB(92, 126, 158);
    }
}

juce::Colour Vst3GridLiteComponent::noteColour(const NoteEvent& note, bool selectedLane) const
{
    auto colour = roleColour(note.semanticRole, selectedLane);
    const float velocityAlpha = juce::jmap(static_cast<float>(juce::jlimit(1, 127, note.velocity)),
                                           1.0f,
                                           127.0f,
                                           0.52f,
                                           0.98f);
    return colour.withAlpha(velocityAlpha);
}

std::optional<int> Vst3GridLiteComponent::laneIndexAtY(int yInGrid, int effectiveRowHeight, int laneCount) const
{
    if (yInGrid < 0 || laneCount <= 0)
        return std::nullopt;

    const int row = yInGrid / effectiveRowHeight;
    if (row < 0 || row >= laneCount)
        return std::nullopt;

    return row;
}

void Vst3GridLiteComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(13, 14, 17));

    const auto lanes = orderedVisibleLanes();
    const int laneCount = static_cast<int>(lanes.size());
    const int bars = juce::jmax(1, project.params.bars);
    const int totalSteps = juce::jmax(1, bars * 16);

    auto bounds = getLocalBounds().reduced(8);
    if (bounds.getWidth() <= kLaneStripWidth + 20 || bounds.getHeight() <= kRulerHeight + 20 || laneCount <= 0)
        return;

    auto stripArea = bounds.removeFromLeft(kLaneStripWidth);
    auto gridArea = bounds;
    const auto rulerArea = gridArea.removeFromTop(kRulerHeight);
    const int resolvedRowHeight = juce::jmax(22, rowHeight);
    const float stepWidth = juce::jmax(static_cast<float>(kMinStepPixelWidth),
                                       static_cast<float>(gridArea.getWidth()) / static_cast<float>(totalSteps));

    const auto frame = getLocalBounds().toFloat().reduced(2.0f);
    juce::ColourGradient shellFill(juce::Colour::fromRGB(30, 24, 20), frame.getTopLeft(),
                                   juce::Colour::fromRGB(14, 15, 18), frame.getBottomLeft(), false);
    shellFill.addColour(0.24, juce::Colour::fromRGB(40, 30, 23));
    shellFill.addColour(0.56, juce::Colour::fromRGB(24, 22, 22));
    shellFill.addColour(1.0, juce::Colour::fromRGB(13, 14, 17));
    g.setGradientFill(shellFill);
    g.fillRoundedRectangle(frame, 10.0f);
    drawEmberTexture(g, frame.reduced(8.0f), 0.9f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
    g.drawRoundedRectangle(frame, 10.0f, 1.0f);
    g.setColour(juce::Colour::fromRGBA(238, 188, 112, 90));
    g.drawRoundedRectangle(frame.reduced(0.5f), 10.0f, 1.0f);

    juce::ColourGradient rulerFill(juce::Colour::fromRGB(56, 40, 24), rulerArea.getTopLeft().toFloat(),
                                   juce::Colour::fromRGB(30, 24, 22), rulerArea.getBottomLeft().toFloat(), false);
    g.setGradientFill(rulerFill);
    g.fillRect(rulerArea);
    g.setColour(juce::Colour::fromRGB(18, 19, 23));
    g.fillRect(stripArea.withTrimmedTop(kRulerHeight));
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 14));
    g.drawVerticalLine(stripArea.getRight(), static_cast<float>(rulerArea.getY()), static_cast<float>(gridArea.getBottom()));

    g.setColour(juce::Colour::fromRGB(233, 194, 123));
    g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    g.drawText("GRID", rulerArea.withTrimmedLeft(8).removeFromLeft(58), juce::Justification::centredLeft, true);

    for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex)
    {
        const auto& lane = lanes[static_cast<size_t>(laneIndex)];
        const int y = gridArea.getY() + laneIndex * resolvedRowHeight;
        const auto rowBounds = juce::Rectangle<int>(gridArea.getX(), y, gridArea.getWidth(), resolvedRowHeight);
        const bool selectedLane = lane.laneId == selectedTrack;
        const auto accent = laneAccentColour(lane);

        juce::ColourGradient rowFill(selectedLane ? juce::Colour::fromRGBA(56, 68, 92, 116)
                                                  : juce::Colour::fromRGBA(28, 30, 35, 78),
                                     rowBounds.getTopLeft().toFloat(),
                                     selectedLane ? juce::Colour::fromRGBA(24, 28, 36, 108)
                                                  : juce::Colour::fromRGBA(20, 22, 26, 70),
                                     rowBounds.getBottomLeft().toFloat(),
                                     false);
        g.setGradientFill(rowFill);
        g.fillRect(rowBounds);

        g.setColour(accent.withAlpha(selectedLane ? 0.90f : 0.62f));
        g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(stripArea.getX() + 5),
                                                      static_cast<float>(y + 3),
                                                      4.0f,
                                                      static_cast<float>(resolvedRowHeight - 6)),
                               2.0f);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 10));
        g.drawHorizontalLine(y + resolvedRowHeight - 1,
                             static_cast<float>(gridArea.getX()),
                             static_cast<float>(gridArea.getRight()));
    }

    for (int step = 0; step <= totalSteps; ++step)
    {
        const float x = gridArea.getX() + step * stepWidth;
        const bool majorBar = step % 16 == 0;
        const bool beat = step % 4 == 0;
        g.setColour(majorBar ? juce::Colour::fromRGBA(255, 214, 146, 54)
                             : (beat ? juce::Colour::fromRGBA(255, 255, 255, 22)
                                     : juce::Colour::fromRGBA(255, 255, 255, 10)));
        g.drawVerticalLine(static_cast<int>(std::round(x)),
                           static_cast<float>(rulerArea.getY()),
                           static_cast<float>(gridArea.getBottom()));
    }

    for (int beatIndex = 0; beatIndex < bars * 4; ++beatIndex)
    {
        const float beatX = gridArea.getX() + beatIndex * stepWidth * 4.0f;
        g.setColour(juce::Colour::fromRGB(146, 132, 112));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(juce::String((beatIndex % 4) + 1),
                   static_cast<int>(std::round(beatX)) + 3,
                   rulerArea.getY(),
                   24,
                   rulerArea.getHeight(),
                   juce::Justification::centredLeft,
                   true);
    }

    if (loopRegionTicks.has_value() && loopRegionTicks->getLength() > 0)
    {
        const float ticksPerStepValue = static_cast<float>(ticksPerStep());
        const float startStep = static_cast<float>(loopRegionTicks->getStart()) / ticksPerStepValue;
        const float endStep = static_cast<float>(loopRegionTicks->getEnd()) / ticksPerStepValue;
        const float x = gridArea.getX() + startStep * stepWidth;
        const float width = juce::jmax(2.0f, (endStep - startStep) * stepWidth);

        g.setColour(juce::Colour::fromRGBA(92, 198, 255, 18));
        g.fillRect(juce::Rectangle<float>(x, static_cast<float>(gridArea.getY()), width, static_cast<float>(gridArea.getHeight())));
        g.setColour(juce::Colour::fromRGBA(110, 208, 255, 64));
        g.drawRect(juce::Rectangle<float>(x, static_cast<float>(gridArea.getY()), width, static_cast<float>(gridArea.getHeight())), 1.0f);
    }

    for (const auto& track : project.tracks)
    {
        auto laneIt = std::find_if(lanes.begin(), lanes.end(), [&track](const VisibleLane& lane)
        {
            return lane.laneId == track.laneId;
        });

        if (laneIt == lanes.end())
            continue;

        const int laneIndex = static_cast<int>(std::distance(lanes.begin(), laneIt));
        const bool selectedLane = laneIt->laneId == selectedTrack;
        const float rowY = static_cast<float>(gridArea.getY() + laneIndex * resolvedRowHeight);

        for (const auto& note : track.notes)
        {
            const float startStep = static_cast<float>(note.step)
                + static_cast<float>(note.microOffset) / static_cast<float>(ticksPerStep());
            const float noteLength = juce::jmax(1.0f, static_cast<float>(note.length));
            const float x = gridArea.getX() + startStep * stepWidth;
            const float width = juce::jmax(4.0f, noteLength * stepWidth - 2.0f);
            const auto rect = juce::Rectangle<float>(x + 1.0f,
                                                     rowY + 4.0f,
                                                     juce::jmin(width, static_cast<float>(gridArea.getRight()) - x - 2.0f),
                                                     static_cast<float>(resolvedRowHeight - 8));
            const auto colour = noteColour(note, selectedLane);

            drawNoteGlow(g, rect, colour);
            g.setColour(colour);
            g.fillRoundedRectangle(rect, 3.0f);
            g.setColour(juce::Colours::white.withAlpha(selectedLane ? 0.34f : 0.20f));
            g.drawRoundedRectangle(rect, 3.0f, 1.0f);
        }
    }

    const float previewX = gridArea.getX() + previewStartStep * stepWidth;
    g.setColour(juce::Colour::fromRGBA(255, 196, 116, 108));
    g.drawLine(previewX, static_cast<float>(rulerArea.getY()), previewX, static_cast<float>(gridArea.getBottom()), 1.5f);

    if (playheadStep >= 0.0f)
    {
        const float playheadX = gridArea.getX() + playheadStep * stepWidth;
        g.setColour(juce::Colour::fromRGB(108, 214, 255));
        g.drawLine(playheadX, static_cast<float>(rulerArea.getY()), playheadX, static_cast<float>(gridArea.getBottom()), 2.0f);
    }
}

void Vst3GridLiteComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto lanes = orderedVisibleLanes();
    const int laneCount = static_cast<int>(lanes.size());
    const int bars = juce::jmax(1, project.params.bars);
    const int totalSteps = juce::jmax(1, bars * 16);

    auto bounds = getLocalBounds().reduced(8);
    if (laneCount <= 0 || bounds.getWidth() <= kLaneStripWidth + 20 || bounds.getHeight() <= kRulerHeight + 20)
        return;

    auto stripArea = bounds.removeFromLeft(kLaneStripWidth);
    auto gridArea = bounds;
    gridArea.removeFromTop(kRulerHeight);
    const int resolvedRowHeight = juce::jmax(22, rowHeight);
    const float stepWidth = juce::jmax(static_cast<float>(kMinStepPixelWidth),
                                       static_cast<float>(gridArea.getWidth()) / static_cast<float>(totalSteps));

    const auto laneIndex = laneIndexAtY(event.y - gridArea.getY(), resolvedRowHeight, laneCount);
    if (!laneIndex.has_value())
        return;

    const auto& lane = lanes[static_cast<size_t>(*laneIndex)];

    if (onLaneClicked)
        onLaneClicked(lane.laneId);

    if (event.x < stripArea.getRight())
        return;

    const float stepFloat = (static_cast<float>(event.x) - static_cast<float>(gridArea.getX())) / stepWidth;
    const int step = juce::jlimit(0, totalSteps - 1, static_cast<int>(std::floor(stepFloat)));

    if (onStepClicked)
        onStepClicked(step);
}
} // namespace bbg
