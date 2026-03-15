#include "GridEditorComponent.h"

#include <algorithm>
#include <cmath>

#include "../Core/TrackRegistry.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
void GridEditorComponent::setProject(const PatternProject& value)
{
    project = value;
    invalidateStaticCache();
    repaint();
}

void GridEditorComponent::setStepWidth(float width)
{
    const float next = juce::jlimit(10.0f, 72.0f, width);
    if (std::abs(next - stepWidth) < 0.001f)
        return;

    stepWidth = next;
    invalidateStaticCache();
    repaint();
}

void GridEditorComponent::setLaneHeight(int height)
{
    const int next = juce::jlimit(22, 80, height);
    if (next == laneHeight)
        return;

    laneHeight = next;
    invalidateStaticCache();
    repaint();
}

void GridEditorComponent::setGridResolution(GridResolution resolution)
{
    if (gridResolution == resolution)
        return;

    gridResolution = resolution;
    invalidateStaticCache();
    repaint();
}

void GridEditorComponent::setSelectedTrack(TrackType type)
{
    if (selectedTrack == type)
        return;

    selectedTrack = type;
    invalidateStaticCache();
    repaint();
}

void GridEditorComponent::setPlayheadStep(float step)
{
    const int oldX = lastPlayheadPixel;
    playheadStep = step;

    if (playheadStep < 0.0f)
    {
        lastPlayheadPixel = -1;
        if (oldX >= 0)
            repaint(juce::Rectangle<int>(oldX - 2, rulerHeight, 5, getHeight() - rulerHeight));
        return;
    }

    const int newX = static_cast<int>(std::round(playheadStep * stepWidth));
    lastPlayheadPixel = newX;

    if (oldX >= 0)
        repaint(juce::Rectangle<int>(oldX - 2, rulerHeight, 5, getHeight() - rulerHeight));
    repaint(juce::Rectangle<int>(newX - 2, rulerHeight, 5, getHeight() - rulerHeight));
}

int GridEditorComponent::getGridWidth() const
{
    const int bars = std::max(1, project.params.bars);
    return static_cast<int>(std::ceil(stepWidth * static_cast<float>(bars * 16)));
}

int GridEditorComponent::getGridHeight() const
{
    int visibleRows = 0;
    for (const auto& track : project.tracks)
    {
        if (isVisibleLane(track.type))
            ++visibleRows;
    }

    return rulerHeight + visibleRows * laneHeight;
}

void GridEditorComponent::paint(juce::Graphics& g)
{
    ensureStaticCache();
    g.fillAll(juce::Colour::fromRGB(18, 20, 24));
    g.drawImageAt(staticGridCache, 0, 0);

    auto area = getLocalBounds();
    const int bars = std::max(1, project.params.bars);
    const int steps = bars * 16;
    const int tracksCount = (getGridHeight() - rulerHeight) / laneHeight;

    if (steps <= 0 || tracksCount <= 0)
        return;

    const float rowHeight = static_cast<float>(laneHeight);

    for (const auto& track : project.tracks)
    {
        const int row = visibleLaneIndex(track.type);
        if (row < 0)
            continue;

        for (const auto& note : track.notes)
        {
            const float microStep = static_cast<float>(note.microOffset) / static_cast<float>(ticksPerStep());
            const float microShiftPx = juce::jlimit(-0.42f * stepWidth, 0.42f * stepWidth, microStep * stepWidth);
            auto x = static_cast<float>(note.step) * stepWidth + microShiftPx;
            auto y = static_cast<float>(rulerHeight + row * laneHeight);
            auto w = stepWidth * static_cast<float>(std::max(1, note.length));

            const float normVel = juce::jlimit(0.0f, 1.0f, static_cast<float>(note.velocity) / 127.0f);
            const auto color = note.isGhost
                ? juce::Colour::fromRGB(120, 176, 255).withAlpha(0.45f + 0.45f * normVel)
                : juce::Colour::fromRGB(255, 164, 74).withAlpha(0.4f + 0.5f * normVel);

            g.setColour(color);
            g.fillRoundedRectangle(x + 1.0f, y + 2.0f, juce::jmax(2.0f, w - 2.0f), rowHeight - 4.0f, 2.0f);
        }
    }

    if (selectedNote.has_value())
    {
        const auto& ref = *selectedNote;
        for (const auto& track : project.tracks)
        {
            if (track.type != ref.track)
                continue;
            if (ref.index < 0 || ref.index >= static_cast<int>(track.notes.size()))
                continue;

            const int row = visibleLaneIndex(track.type);
            if (row < 0)
                continue;

            auto bounds = noteBounds(track.notes[static_cast<size_t>(ref.index)], row).expanded(1);
            g.setColour(juce::Colour::fromRGB(255, 236, 178).withAlpha(0.95f));
            g.drawRoundedRectangle(bounds.toFloat(), 2.5f, 1.5f);
            break;
        }
    }

    if (playheadStep >= 0.0f)
    {
        const float x = playheadStep * stepWidth;
        g.setColour(juce::Colour::fromRGB(255, 92, 92).withAlpha(0.88f));
        g.drawLine(x, static_cast<float>(rulerHeight), x, static_cast<float>(getHeight()), 1.8f);
    }
}

void GridEditorComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCtrlDown())
    {
        if (onHorizontalZoomGesture)
            onHorizontalZoomGesture(wheel.deltaY);
        return;
    }

    if (event.mods.isShiftDown())
    {
        if (onLaneHeightZoomGesture)
            onLaneHeightZoomGesture(wheel.deltaY);
        return;
    }

    juce::Component::mouseWheelMove(event, wheel);
}

void GridEditorComponent::mouseDown(const juce::MouseEvent& event)
{
    const int bars = std::max(1, project.params.bars);
    const int totalSteps = bars * 16;
    if (totalSteps <= 0)
        return;

    const auto p = event.getPosition();
    if (p.y < rulerHeight)
        return;

    if (const auto hit = findNoteAt(p); hit.has_value())
    {
        selectedNote = hit;
        draggingSelectedNote = true;
        dragStartX = p.x;

        for (const auto& track : project.tracks)
        {
            if (track.type != hit->track)
                continue;
            if (hit->index < 0 || hit->index >= static_cast<int>(track.notes.size()))
                break;

            const auto& note = track.notes[static_cast<size_t>(hit->index)];
            dragOriginalTicks = note.step * ticksPerStep() + note.microOffset;
            break;
        }

        repaint();
        return;
    }

    const int laneIndex = (p.y - rulerHeight) / laneHeight;
    const auto clickedTrack = trackForVisibleLaneIndex(laneIndex);
    if (!clickedTrack.has_value())
        return;

    const int step = juce::jlimit(0, totalSteps - 1, static_cast<int>(std::floor(static_cast<float>(p.x) / stepWidth)));
    selectedTrack = *clickedTrack;
    repaint();

    if (onCellClicked)
        onCellClicked(*clickedTrack, step);
}

void GridEditorComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!draggingSelectedNote || !selectedNote.has_value())
        return;

    auto& ref = *selectedNote;
    auto track = std::find_if(project.tracks.begin(), project.tracks.end(), [&ref](const TrackState& t)
    {
        return t.type == ref.track;
    });

    if (track == project.tracks.end() || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
        return;

    const int bars = juce::jmax(1, project.params.bars);
    const int maxTicks = bars * 16 * ticksPerStep();
    const float deltaSteps = static_cast<float>(event.getDistanceFromDragStartX()) / juce::jmax(1.0f, stepWidth);
    const int deltaTicks = static_cast<int>(std::round(deltaSteps * static_cast<float>(ticksPerStep())));

    int targetTicks = dragOriginalTicks + deltaTicks;
    const int snapTicks = snapTicksForCurrentResolution();
    targetTicks = static_cast<int>(std::round(static_cast<float>(targetTicks) / static_cast<float>(snapTicks))) * snapTicks;
    targetTicks = juce::jlimit(0, juce::jmax(0, maxTicks - 1), targetTicks);

    int newStep = targetTicks / ticksPerStep();
    int micro = targetTicks - newStep * ticksPerStep();
    if (micro > ticksPerStep() / 2)
    {
        micro -= ticksPerStep();
        ++newStep;
    }

    newStep = juce::jlimit(0, bars * 16 - 1, newStep);
    auto& note = track->notes[static_cast<size_t>(ref.index)];
    note.step = newStep;
    note.microOffset = juce::jlimit(-960, 960, micro);

    repaint();
}

void GridEditorComponent::mouseUp(const juce::MouseEvent&)
{
    if (!draggingSelectedNote || !selectedNote.has_value())
        return;

    draggingSelectedNote = false;
    const auto& ref = *selectedNote;
    for (const auto& track : project.tracks)
    {
        if (track.type != ref.track)
            continue;
        if (onTrackNotesEdited)
            onTrackNotesEdited(track.type, track.notes);
        break;
    }
}

bool GridEditorComponent::isVisibleLane(TrackType type) const
{
    const auto* info = TrackRegistry::find(type);
    return info != nullptr && info->visibleInUI;
}

int GridEditorComponent::visibleLaneIndex(TrackType type) const
{
    int index = 0;
    for (const auto& track : project.tracks)
    {
        if (!isVisibleLane(track.type))
            continue;

        if (track.type == type)
            return index;

        ++index;
    }

    return -1;
}

std::optional<TrackType> GridEditorComponent::trackForVisibleLaneIndex(int laneIndex) const
{
    if (laneIndex < 0)
        return std::nullopt;

    int index = 0;
    for (const auto& track : project.tracks)
    {
        if (!isVisibleLane(track.type))
            continue;

        if (index == laneIndex)
            return track.type;

        ++index;
    }

    return std::nullopt;
}

void GridEditorComponent::invalidateStaticCache()
{
    staticCacheDirty = true;
}

void GridEditorComponent::ensureStaticCache()
{
    if (!staticCacheDirty && staticGridCache.isValid()
        && staticGridCache.getWidth() == getWidth()
        && staticGridCache.getHeight() == getHeight())
        return;

    staticGridCache = juce::Image(juce::Image::RGB, juce::jmax(1, getWidth()), juce::jmax(1, getHeight()), true);
    juce::Graphics g(staticGridCache);

    g.fillAll(juce::Colour::fromRGB(18, 20, 24));

    auto area = getLocalBounds();
    const int bars = std::max(1, project.params.bars);
    const int steps = bars * 16;
    const int tracksCount = (getGridHeight() - rulerHeight) / laneHeight;

    g.setColour(juce::Colour::fromRGB(30, 34, 40));
    g.fillRect(0, 0, area.getWidth(), rulerHeight);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
    g.drawLine(0.0f, static_cast<float>(rulerHeight), static_cast<float>(area.getWidth()), static_cast<float>(rulerHeight));

    for (int row = 0; row < tracksCount; ++row)
    {
        const auto rowArea = area.withTop(rulerHeight + row * laneHeight).withHeight(laneHeight);
        g.setColour((row % 2 == 0) ? juce::Colour::fromRGB(26, 29, 34)
                                   : juce::Colour::fromRGB(22, 25, 29));
        g.fillRect(rowArea);
    }

    const int selectedRow = visibleLaneIndex(selectedTrack);
    if (selectedRow >= 0)
    {
        g.setColour(juce::Colour::fromRGBA(90, 180, 255, 30));
        g.fillRect(area.withTop(rulerHeight + selectedRow * laneHeight).withHeight(laneHeight));
    }

    for (int s = 0; s <= steps; ++s)
    {
        const float x = static_cast<float>(s) * stepWidth;
        if (s % 16 == 0)
            g.setColour(juce::Colour::fromRGB(106, 114, 130));
        else if (s % 4 == 0)
            g.setColour(juce::Colour::fromRGB(74, 80, 92));
        else if (s % 2 == 0)
            g.setColour(juce::Colour::fromRGB(58, 64, 74));
        else
            g.setColour(juce::Colour::fromRGB(44, 49, 56));

        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(rulerHeight), static_cast<float>(area.getBottom()));

        if (s % 16 == 0)
        {
            const int barNumber = s / 16 + 1;
            g.setColour(juce::Colour::fromRGB(182, 189, 201));
            g.setFont(juce::Font(11.0f));
            g.drawText(juce::String(barNumber), static_cast<int>(x) + 4, 2, 20, rulerHeight - 4, juce::Justification::centredLeft);
        }
    }

    const int totalTicks = bars * 16 * ticksPerStep();
    const int resolutionTicks = std::max(1, ticksForGridResolution());
    const bool drawSubGrid = resolutionTicks < ticksPerStep() * 2;

    if (drawSubGrid)
    {
        g.setColour(juce::Colour::fromRGBA(180, 190, 208, 26));
        for (int tick = resolutionTicks; tick < totalTicks; tick += resolutionTicks)
        {
            if (tick % ticksPerStep() == 0)
                continue;

            const float xSub = (static_cast<float>(tick) / static_cast<float>(ticksPerStep())) * stepWidth;
            g.drawVerticalLine(static_cast<int>(xSub), static_cast<float>(rulerHeight), static_cast<float>(area.getBottom()));
        }
    }

    g.setColour(juce::Colour::fromRGB(54, 60, 72));
    const float rowHeight = static_cast<float>(laneHeight);
    for (int r = 0; r <= tracksCount; ++r)
    {
        const float y = static_cast<float>(rulerHeight) + static_cast<float>(r) * rowHeight;
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(area.getRight()));
    }

    staticCacheDirty = false;
}

std::optional<GridEditorComponent::SelectedNoteRef> GridEditorComponent::findNoteAt(juce::Point<int> position) const
{
    for (const auto& track : project.tracks)
    {
        const int row = visibleLaneIndex(track.type);
        if (row < 0)
            continue;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
        {
            if (noteBounds(track.notes[static_cast<size_t>(i)], row).contains(position))
                return SelectedNoteRef { track.type, i };
        }
    }

    return std::nullopt;
}

juce::Rectangle<int> GridEditorComponent::noteBounds(const NoteEvent& note, int row) const
{
    const float microStep = static_cast<float>(note.microOffset) / static_cast<float>(ticksPerStep());
    const float microShiftPx = juce::jlimit(-0.42f * stepWidth, 0.42f * stepWidth, microStep * stepWidth);
    const float x = static_cast<float>(note.step) * stepWidth + microShiftPx;
    const float y = static_cast<float>(rulerHeight + row * laneHeight);
    const float w = stepWidth * static_cast<float>(std::max(1, note.length));
    return juce::Rectangle<int>(static_cast<int>(std::floor(x + 1.0f)),
                                static_cast<int>(std::floor(y + 2.0f)),
                                static_cast<int>(std::ceil(juce::jmax(2.0f, w - 2.0f))),
                                juce::jmax(4, laneHeight - 4));
}

int GridEditorComponent::snapTicksForCurrentResolution() const
{
    return ticksForGridResolution();
}

int GridEditorComponent::ticksForGridResolution() const
{
    switch (gridResolution)
    {
        case GridResolution::OneEighth: return ticksPerStep() * 2; // 1/8
        case GridResolution::OneSixteenth: return ticksPerStep(); // 1/16
        case GridResolution::OneThirtySecond: return ticksPerStep() / 2; // 1/32
        case GridResolution::OneSixtyFourth: return ticksPerStep() / 4; // 1/64
        case GridResolution::Triplet: return (ticksPerStep() * 2) / 3; // 1/24
        default: return ticksPerStep();
    }
}
} // namespace bbg
