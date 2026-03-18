#include "GridEditorComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include "../Core/TrackRegistry.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
enum class ToolCursorIcon
{
    Pencil,
    Brush,
    Blade,
    Eraser
};

juce::MouseCursor makeToolCursor(ToolCursorIcon icon)
{
    juce::Image image(juce::Image::ARGB, 32, 32, true);
    juce::Graphics g(image);

    auto transform = juce::AffineTransform::rotation(-0.72f, 14.0f, 18.0f);

    if (icon == ToolCursorIcon::Pencil)
    {
        juce::Path body;
        body.addRoundedRectangle(10.0f, 10.0f, 6.0f, 16.0f, 1.4f);
        body.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(238, 192, 74));
        g.fillPath(body);
        g.setColour(juce::Colour::fromRGB(92, 64, 24));
        g.strokePath(body, juce::PathStrokeType(1.0f));

        juce::Path ferrule;
        ferrule.addRectangle(10.0f, 8.0f, 6.0f, 3.2f);
        ferrule.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(208, 214, 222));
        g.fillPath(ferrule);

        juce::Path eraser;
        eraser.addRoundedRectangle(10.0f, 5.2f, 6.0f, 3.4f, 1.0f);
        eraser.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(236, 136, 156));
        g.fillPath(eraser);

        juce::Path tip;
        tip.addTriangle(10.0f, 26.0f, 13.0f, 31.0f, 16.0f, 26.0f);
        tip.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(232, 214, 184));
        g.fillPath(tip);

        juce::Path lead;
        lead.addTriangle(12.25f, 29.7f, 13.0f, 31.0f, 13.75f, 29.7f);
        lead.applyTransform(transform);
        g.setColour(juce::Colours::black.withAlpha(0.9f));
        g.fillPath(lead);

        return juce::MouseCursor(image, 20, 28);
    }

    if (icon == ToolCursorIcon::Brush)
    {
        juce::Path handle;
        handle.addRoundedRectangle(11.0f, 11.0f, 5.0f, 14.0f, 1.5f);
        handle.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(154, 98, 44));
        g.fillPath(handle);

        juce::Path ferrule;
        ferrule.addRectangle(11.0f, 8.0f, 5.0f, 4.0f);
        ferrule.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(196, 204, 214));
        g.fillPath(ferrule);

        juce::Path bristles;
        bristles.startNewSubPath(9.5f, 7.7f);
        bristles.lineTo(12.0f, 3.5f);
        bristles.lineTo(15.0f, 2.8f);
        bristles.lineTo(17.5f, 7.7f);
        bristles.closeSubPath();
        bristles.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(58, 78, 102));
        g.fillPath(bristles);

        return juce::MouseCursor(image, 5, 5);
    }

    if (icon == ToolCursorIcon::Blade)
    {
        juce::Path blade;
        blade.addRoundedRectangle(8.0f, 12.0f, 14.0f, 8.0f, 2.0f);
        blade.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(214, 220, 228));
        g.fillPath(blade);
        g.setColour(juce::Colour::fromRGB(92, 102, 116));
        g.strokePath(blade, juce::PathStrokeType(1.0f));

        juce::Path edge;
        edge.startNewSubPath(10.0f, 19.0f);
        edge.lineTo(20.0f, 19.0f);
        edge.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(250, 250, 252));
        g.strokePath(edge, juce::PathStrokeType(1.2f));

        juce::Path accent;
        accent.addRoundedRectangle(8.5f, 13.0f, 4.0f, 6.0f, 1.0f);
        accent.applyTransform(transform);
        g.setColour(juce::Colour::fromRGB(216, 84, 84));
        g.fillPath(accent);

        return juce::MouseCursor(image, 5, 5);
    }

    juce::Path eraser;
    eraser.addRoundedRectangle(8.0f, 10.0f, 13.0f, 10.0f, 2.0f);
    eraser.applyTransform(transform);
    g.setColour(juce::Colour::fromRGB(232, 126, 150));
    g.fillPath(eraser);
    g.setColour(juce::Colour::fromRGB(176, 84, 108));
    g.strokePath(eraser, juce::PathStrokeType(1.0f));

    juce::Path sleeve;
    sleeve.addRoundedRectangle(15.0f, 10.0f, 6.0f, 10.0f, 1.5f);
    sleeve.applyTransform(transform);
    g.setColour(juce::Colour::fromRGB(242, 228, 208));
    g.fillPath(sleeve);

    return juce::MouseCursor(image, 5, 5);
}

const juce::MouseCursor& toolCursorFor(GridEditorComponent::EditorTool tool)
{
    static const auto pencil = makeToolCursor(ToolCursorIcon::Pencil);
    static const auto brush = makeToolCursor(ToolCursorIcon::Brush);
    static const auto blade = makeToolCursor(ToolCursorIcon::Blade);
    static const auto eraser = makeToolCursor(ToolCursorIcon::Eraser);
    static const juce::MouseCursor crosshair(juce::MouseCursor::CrosshairCursor);

    switch (tool)
    {
        case GridEditorComponent::EditorTool::Pencil: return pencil;
        case GridEditorComponent::EditorTool::Brush: return brush;
        case GridEditorComponent::EditorTool::Cut: return blade;
        case GridEditorComponent::EditorTool::Erase: return eraser;
        case GridEditorComponent::EditorTool::Select:
        default: return crosshair;
    }
}
}

void GridEditorComponent::setProject(const PatternProject& value)
{
    project = value;
    previewStartStep = std::max(0, project.previewStartStep);
    previewStartTick = previewStartStep * ticksPerStep();
    normalizeSelection();
    invalidateStaticCache();
    repaint();
}

void GridEditorComponent::setStepWidth(float width)
{
    const float next = juce::jlimit(4.0f, 160.0f, width);
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

void GridEditorComponent::setLaneDisplayOrder(const std::vector<TrackType>& order)
{
    laneDisplayOrder = order;
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

void GridEditorComponent::setEditorTool(EditorTool tool)
{
    if (editorTool == tool)
        return;

    editorTool = tool;
    editMode = EditMode::None;
    drawVisitedTicks.clear();
    eraseVisitedKeys.clear();
    applyCursorForHover();
    repaint();
}

void GridEditorComponent::setInputBindings(const InputBindings& bindings)
{
    inputBindings = bindings;
}

void GridEditorComponent::setLoopRegion(const std::optional<juce::Range<int>>& tickRange)
{
    if (loopRegionTicks == tickRange)
        return;

    loopRegionTicks = tickRange;
    repaint();
}

std::optional<juce::Range<int>> GridEditorComponent::getLoopRegion() const
{
    return loopRegionTicks;
}

void GridEditorComponent::refreshTransientInputState()
{
    applyCursorForHover();
    repaint();
}

bool GridEditorComponent::deleteSelectedNote()
{
    return deleteSelectedNotes();
}

bool GridEditorComponent::deleteSelectedNotes()
{
    normalizeSelection();
    if (selectedNotes.empty())
        return false;

    std::set<TrackType> changedTracks;
    std::sort(selectedNotes.begin(), selectedNotes.end(), [](const SelectedNoteRef& a, const SelectedNoteRef& b)
    {
        if (a.track != b.track)
            return static_cast<int>(a.track) > static_cast<int>(b.track);
        return a.index > b.index;
    });

    for (const auto& ref : selectedNotes)
    {
        auto* track = findMutableTrack(ref.track);
        if (track == nullptr)
            continue;
        if (ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;
        track->notes.erase(track->notes.begin() + ref.index);
        changedTracks.insert(ref.track);
    }

    clearSelectionInternal();
    for (const auto t : changedTracks)
        emitTrackEdited(t);
    repaint();
    return !changedTracks.empty();
}

bool GridEditorComponent::copySelectionToClipboard()
{
    return copySelectionInternal(false);
}

bool GridEditorComponent::cutSelectionToClipboard()
{
    return copySelectionInternal(true);
}

bool GridEditorComponent::pasteClipboardAtPlayhead()
{
    if (clipboardNotes.empty())
        return false;

    const int bars = juce::jmax(1, project.params.bars);
    const int maxTicks = bars * 16 * ticksPerStep();
    const int anchorTick = snappedTickFromTick(previewStartTick);

    clearSelectionInternal();
    std::set<TrackType> changedTracks;
    std::vector<std::pair<TrackType, std::vector<NoteEvent>>> insertedNotesByTrack;

    auto insertedBucketFor = [&insertedNotesByTrack](TrackType trackType) -> std::vector<NoteEvent>&
    {
        auto it = std::find_if(insertedNotesByTrack.begin(), insertedNotesByTrack.end(), [trackType](const auto& entry)
        {
            return entry.first == trackType;
        });

        if (it == insertedNotesByTrack.end())
        {
            insertedNotesByTrack.push_back({ trackType, {} });
            return insertedNotesByTrack.back().second;
        }

        return it->second;
    };

    for (const auto& c : clipboardNotes)
    {
        auto* track = findMutableTrack(c.track);
        if (track == nullptr)
            continue;

        NoteEvent note = c.note;
        const int targetTick = juce::jlimit(0, juce::jmax(0, maxTicks - 1), anchorTick + c.relativeTick);
        setNoteStartTick(note, targetTick, bars);
        track->notes.push_back(note);
        insertedBucketFor(c.track).push_back(note);
        changedTracks.insert(c.track);
    }

    for (const auto trackType : changedTracks)
        if (auto* track = findMutableTrack(trackType))
            sortTrackNotes(*track);

    for (const auto& inserted : insertedNotesByTrack)
    {
        auto* track = findMutableTrack(inserted.first);
        if (track == nullptr)
            continue;

        std::vector<bool> consumed(track->notes.size(), false);
        for (const auto& note : inserted.second)
        {
            for (int i = static_cast<int>(track->notes.size()) - 1; i >= 0; --i)
            {
                if (consumed[static_cast<size_t>(i)])
                    continue;

                const auto& current = track->notes[static_cast<size_t>(i)];
                if (noteStartTick(current) == noteStartTick(note)
                    && current.length == note.length
                    && current.velocity == note.velocity
                    && current.pitch == note.pitch
                    && current.microOffset == note.microOffset)
                {
                    consumed[static_cast<size_t>(i)] = true;
                    selectedNotes.push_back({ inserted.first, i });
                    primarySelectedNote = SelectedNoteRef{ inserted.first, i };
                    break;
                }
            }
        }
    }

    for (const auto t : changedTracks)
        emitTrackEdited(t);

    repaint();
    return !changedTracks.empty();
}

bool GridEditorComponent::duplicateSelectionToRight()
{
    if (!copySelectionInternal(false))
        return false;

    const int insertTick = snappedTickFromTick(clipboardSourceStartTick + clipboardSpanTicks);
    previewStartTick = insertTick;
    previewStartStep = juce::jmax(0, insertTick / ticksPerStep());
    return pasteClipboardAtPlayhead();
}

void GridEditorComponent::clearNoteSelection()
{
    clearSelectionInternal();
    repaint();
}

bool GridEditorComponent::selectAllNotes()
{
    clearSelectionInternal();

    for (const auto& track : project.tracks)
    {
        if (!isVisibleLane(track.type))
            continue;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
            selectedNotes.push_back({ track.type, i });
    }

    if (!selectedNotes.empty())
        primarySelectedNote = selectedNotes.front();

    repaint();
    return !selectedNotes.empty();
}

bool GridEditorComponent::selectAllNotesInLane(TrackType lane)
{
    auto* track = findMutableTrack(lane);
    if (track == nullptr)
        return false;

    clearSelectionInternal();
    for (int i = 0; i < static_cast<int>(track->notes.size()); ++i)
        selectedNotes.push_back({ lane, i });

    if (!selectedNotes.empty())
        primarySelectedNote = selectedNotes.front();

    repaint();
    return !selectedNotes.empty();
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

    const bool velocityPreviewActive = isVelocityEditKeyDown() || editMode == EditMode::Velocity || velocityWaveModeActive;
    const auto velocityPreviewTrack = primarySelectedNote.has_value() ? primarySelectedNote->track : selectedTrack;

    if (loopRegionTicks.has_value() && loopRegionTicks->getLength() > 0)
    {
        const float loopStartX = (static_cast<float>(loopRegionTicks->getStart()) / static_cast<float>(ticksPerStep())) * stepWidth;
        const float loopEndX = (static_cast<float>(loopRegionTicks->getEnd()) / static_cast<float>(ticksPerStep())) * stepWidth;
        const auto loopRuler = juce::Rectangle<float>(loopStartX,
                                                      2.0f,
                                                      juce::jmax(2.0f, loopEndX - loopStartX),
                                                      static_cast<float>(rulerHeight - 4));

        g.setColour(juce::Colour::fromRGBA(84, 196, 255, 28));
        g.fillRect(juce::Rectangle<float>(loopStartX,
                                         static_cast<float>(rulerHeight),
                                         juce::jmax(2.0f, loopEndX - loopStartX),
                                         static_cast<float>(getHeight() - rulerHeight)));

        g.setColour(juce::Colour::fromRGBA(98, 214, 255, 110));
        g.fillRoundedRectangle(loopRuler, 4.0f);
        g.setColour(juce::Colour::fromRGBA(222, 246, 255, 186));
        g.drawRoundedRectangle(loopRuler, 4.0f, 1.2f);

        g.setColour(juce::Colour::fromRGBA(116, 224, 255, 160));
        g.drawLine(loopStartX, 1.0f, loopStartX, static_cast<float>(getHeight()), 1.4f);
        g.drawLine(loopEndX, 1.0f, loopEndX, static_cast<float>(getHeight()), 1.4f);

        g.setColour(juce::Colour::fromRGB(236, 248, 255));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("LOOP",
                   loopRuler.toNearestInt().reduced(8, 0),
                   juce::Justification::centredLeft,
                   false);
    }

    for (const auto& track : project.tracks)
    {
        const int row = visibleLaneIndex(track.type);
        if (row < 0)
            continue;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
        {
            const auto& note = track.notes[static_cast<size_t>(i)];
            const auto boundsInt = noteBounds(note, row);
            const auto noteRect = boundsInt.toFloat();
            const bool noteIsSelected = isSelected(SelectedNoteRef{ track.type, i });
            const bool renderVelocityWave = velocityPreviewActive && (track.type == velocityPreviewTrack || noteIsSelected);

            const float normVel = juce::jlimit(0.0f, 1.0f, static_cast<float>(note.velocity) / 127.0f);
            const auto color = note.isGhost
                ? juce::Colour::fromRGB(120, 176, 255).withAlpha(0.45f + 0.45f * normVel)
                : juce::Colour::fromRGB(255, 164, 74).withAlpha(0.4f + 0.5f * normVel);

            if (renderVelocityWave)
            {
                const float laneBottom = static_cast<float>(rulerHeight + row * laneHeight + laneHeight - 3);
                const float waveHeight = juce::jmax(4.0f, normVel * static_cast<float>(laneHeight - 6));
                const auto waveRect = juce::Rectangle<float>(noteRect.getX(), laneBottom - waveHeight, noteRect.getWidth(), waveHeight);
                g.setColour(color.withAlpha(noteIsSelected ? 0.92f : 0.78f));
                g.fillRoundedRectangle(waveRect, 2.0f);
                g.setColour(color.brighter(0.45f).withAlpha(0.95f));
                g.drawLine(waveRect.getX(), waveRect.getY(), waveRect.getRight(), waveRect.getY(), 1.6f);
                g.setColour(juce::Colour::fromRGBA(255, 255, 255, 22));
                g.drawRoundedRectangle(noteRect, 2.0f, 0.8f);
            }
            else
            {
                g.setColour(color);
                g.fillRoundedRectangle(noteRect, 2.0f);

                // Duration strip makes note length explicit even with dense patterns.
                g.setColour(color.brighter(0.35f).withAlpha(0.92f));
                const float stripY = noteRect.getBottom() - 2.5f;
                g.drawLine(noteRect.getX() + 1.0f, stripY, noteRect.getRight() - 1.0f, stripY, 1.6f);

                if (note.length > 1 && noteRect.getWidth() > 14.0f)
                {
                    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 188));
                    g.setFont(juce::Font(juce::FontOptions(10.0f)));
                    g.drawText(juce::String(note.length),
                               noteRect.withTrimmedTop(1.0f).withTrimmedBottom(4.0f).toNearestInt(),
                               juce::Justification::centredRight,
                               false);
                }

                if (track.type == TrackType::Sub808 && noteRect.getWidth() > 24.0f)
                {
                    g.setColour(juce::Colour::fromRGBA(206, 224, 255, 210));
                    g.setFont(juce::Font(juce::FontOptions(9.0f)));
                    g.drawText(juce::String(note.pitch),
                               noteRect.withTrimmedLeft(3.0f).withTrimmedBottom(4.0f).toNearestInt(),
                               juce::Justification::centredLeft,
                               false);
                }
            }

            const bool isHovered = hoverNote.has_value()
                && hoverNote->track == track.type
                && hoverNote->index == i;

            if (isHovered && !noteIsSelected)
            {
                g.setColour(juce::Colour::fromRGBA(255, 255, 255, 72));
                g.drawRoundedRectangle(noteRect.expanded(0.5f), 2.5f, 1.0f);
            }

        }
    }

    if (loopRegionTicks.has_value())
    {
        const auto loop = *loopRegionTicks;
        const float loopX = (static_cast<float>(loop.getStart()) / static_cast<float>(ticksPerStep())) * stepWidth;
        const float loopRight = (static_cast<float>(loop.getEnd()) / static_cast<float>(ticksPerStep())) * stepWidth;
        const auto loopRect = juce::Rectangle<float>(loopX,
                                                     0.0f,
                                                     juce::jmax(1.0f, loopRight - loopX),
                                                     static_cast<float>(getHeight()));
        g.setColour(juce::Colour::fromRGBA(84, 184, 255, 30));
        g.fillRect(loopRect);
        g.setColour(juce::Colour::fromRGBA(112, 208, 255, 180));
        g.drawLine(loopX, 0.0f, loopX, static_cast<float>(getHeight()), 1.2f);
        g.drawLine(loopRight, 0.0f, loopRight, static_cast<float>(getHeight()), 1.2f);
        g.fillRect(juce::Rectangle<float>(loopX, 0.0f, juce::jmax(1.0f, loopRight - loopX), static_cast<float>(rulerHeight)));
    }

    if (!selectedNotes.empty())
    {
        for (const auto& ref : selectedNotes)
        {
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
                g.setColour(juce::Colour::fromRGB(255, 236, 178).withAlpha(0.98f));
                g.drawRoundedRectangle(bounds.toFloat(), 2.5f, 2.0f);

                break;
            }
        }
    }

    if (playheadStep >= 0.0f)
    {
        const float x = playheadStep * stepWidth;
        g.setColour(juce::Colour::fromRGB(255, 92, 92).withAlpha(0.88f));
        g.drawLine(x, static_cast<float>(rulerHeight), x, static_cast<float>(getHeight()), 1.8f);
    }

    const float markerX = (static_cast<float>(previewStartTick) / static_cast<float>(ticksPerStep())) * stepWidth;
    if (markerX >= 0.0f && markerX <= static_cast<float>(getWidth()))
    {
        g.setColour(juce::Colour::fromRGB(108, 206, 255).withAlpha(0.9f));
        g.drawLine(markerX, 0.0f, markerX, static_cast<float>(rulerHeight), 1.3f);

        juce::Path marker;
        marker.startNewSubPath(markerX, 1.0f);
        marker.lineTo(markerX + 9.0f, 5.5f);
        marker.lineTo(markerX, 10.0f);
        marker.closeSubPath();
        g.fillPath(marker);
    }

    if (editMode == EditMode::Marquee && !marqueeRect.isEmpty())
    {
        g.setColour(juce::Colour::fromRGBA(95, 170, 255, 44));
        g.fillRect(marqueeRect);
        g.setColour(juce::Colour::fromRGB(122, 192, 255));
        g.drawRect(marqueeRect, 1);
    }

    if ((editMode == EditMode::Velocity || velocityWaveModeActive || (isVelocityEditKeyDown() && !selectedNotes.empty())) && !selectedNotes.empty())
    {
        int overlayPercent = velocityOverlayPercent;
        int overlayDelta = velocityOverlayDelta;
        if (editMode != EditMode::Velocity && !velocityWaveModeActive && isVelocityEditKeyDown())
        {
            int velocitySum = 0;
            int velocityCount = 0;
            for (const auto& ref : selectedNotes)
            {
                const auto trackIt = std::find_if(project.tracks.begin(), project.tracks.end(), [&ref](const TrackState& track)
                {
                    return track.type == ref.track;
                });
                if (trackIt == project.tracks.end() || ref.index < 0 || ref.index >= static_cast<int>(trackIt->notes.size()))
                    continue;

                velocitySum += trackIt->notes[static_cast<size_t>(ref.index)].velocity;
                ++velocityCount;
            }

            if (velocityCount > 0)
                overlayPercent = static_cast<int>(std::round((static_cast<float>(velocitySum) / static_cast<float>(velocityCount)) * 100.0f / 127.0f));
            overlayDelta = 0;
        }

        const auto overlay = juce::Rectangle<int>(juce::jmax(8, getWidth() - 168), rulerHeight + 8, 156, 24);
        g.setColour(juce::Colour::fromRGBA(18, 22, 28, 230));
        g.fillRoundedRectangle(overlay.toFloat(), 5.0f);
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 28));
        g.drawRoundedRectangle(overlay.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colour::fromRGB(210, 222, 238));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        juce::String label = velocityWaveModeActive ? "Velocity Wave: " : "Velocity: ";
        juce::String value = juce::String(overlayPercent) + "%";
        if (overlayDelta != 0)
            value += " (" + juce::String(overlayDelta > 0 ? "+" : "") + juce::String(overlayDelta) + ")";
        g.drawText(label + value,
                   overlay,
                   juce::Justification::centred,
                   false);
    }
}

void GridEditorComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const bool microtimingModifierDown = isModifierDown(event.mods, inputBindings.microtimingEditModifier);
    const bool horizontalZoomModifierDown = isModifierDown(event.mods, inputBindings.horizontalZoomModifier);
    const bool laneHeightZoomModifierDown = isModifierDown(event.mods, inputBindings.laneHeightZoomModifier);
    const bool velocityModifierDown = isVelocityEditKeyDown();

    if (!microtimingModifierDown
        && !horizontalZoomModifierDown
        && !laneHeightZoomModifierDown
        && !velocityModifierDown)
    {
        const int semitoneDelta = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
        if (semitoneDelta != 0 && updatePitchForSub808Selection(semitoneDelta))
        {
            emitTrackEdited(TrackType::Sub808);
            repaint();
            return;
        }
    }

    if (microtimingModifierDown)
    {
        const int delta = static_cast<int>(std::round((wheel.deltaY + wheel.deltaX) * 24.0f));
        if (delta != 0)
        {
            normalizeSelection();
            if (!selectedNotes.empty())
            {
                collectDragSnapshots();
                if (applySelectionMicroOffsetDelta(delta))
                {
                    std::set<TrackType> changed;
                    for (const auto& ref : selectedNotes)
                        changed.insert(ref.track);
                    for (const auto trackType : changed)
                        emitTrackEdited(trackType);
                    repaint();
                    return;
                }
            }

            if (auto hit = findSub808NoteForPitchEdit(event); hit.has_value())
            {
                setSingleSelection(*hit);
                collectDragSnapshots();
                if (applySelectionMicroOffsetDelta(delta))
                {
                    emitTrackEdited(hit->track);
                    repaint();
                    return;
                }
            }
        }
    }

    if (velocityModifierDown)
    {
        const int delta = static_cast<int>(std::round((wheel.deltaY + wheel.deltaX) * 22.0f));
        if (!selectedNotes.empty())
        {
            collectDragSnapshots();
            if (applySelectionVelocityDelta(delta))
            {
                std::set<TrackType> changed;
                for (const auto& ref : selectedNotes)
                    changed.insert(ref.track);
                for (const auto t : changed)
                    emitTrackEdited(t);
                repaint();
                return;
            }
        }

        const auto p = event.getPosition();
        const int laneIndex = (p.y - rulerHeight) / laneHeight;
        const auto lane = trackForVisibleLaneIndex(laneIndex);
        if (lane.has_value())
        {
            if (auto hit = findNoteAt(p, *lane); hit.has_value())
            {
                if (auto* track = findMutableTrack(hit->track))
                {
                    if (hit->index >= 0 && hit->index < static_cast<int>(track->notes.size()))
                    {
                        auto& note = track->notes[static_cast<size_t>(hit->index)];
                        note.velocity = juce::jlimit(1, 127, note.velocity + delta);
                        setSingleSelection(*hit);
                        emitTrackEdited(hit->track);
                        repaint();
                        return;
                    }
                }
            }
        }
    }

    if (horizontalZoomModifierDown)
    {
        if (onHorizontalZoomGesture)
            onHorizontalZoomGesture(wheel.deltaY);
        return;
    }

    if (laneHeightZoomModifierDown)
    {
        if (onLaneHeightZoomGesture)
            onLaneHeightZoomGesture(wheel.deltaY);
        return;
    }

    juce::Component::mouseWheelMove(event, wheel);
}

void GridEditorComponent::mouseDown(const juce::MouseEvent& event)
{
    editMode = EditMode::None;
    drawVisitedTicks.clear();
    eraseVisitedKeys.clear();
    lastDragPoint = event.getPosition();
    movedDuringDrag = false;

    const int bars = std::max(1, project.params.bars);
    const int totalSteps = bars * 16;
    if (totalSteps <= 0)
        return;

    const auto p = event.getPosition();

    if (event.mods.isRightButtonDown())
    {
        if (p.y < rulerHeight)
        {
            juce::PopupMenu menu;
            constexpr int kActionClearLoop = 401;
            menu.addItem(kActionClearLoop, "Clear Loop Region", loopRegionTicks.has_value());
            menu.showMenuAsync(juce::PopupMenu::Options{}.withParentComponent(this),
                               [safe = juce::Component::SafePointer<GridEditorComponent>(this), clearLoopAction = kActionClearLoop](int choice)
                               {
                                   if (safe == nullptr || choice != clearLoopAction)
                                       return;

                                   safe->loopRegionTicks.reset();
                                   if (safe->onLoopRegionChanged)
                                       safe->onLoopRegionChanged(std::nullopt);
                                   safe->repaint();
                               });
            return;
        }

        const int laneIndex = (p.y - rulerHeight) / laneHeight;
        showNoteContextMenu(p, trackForVisibleLaneIndex(laneIndex));
        return;
    }

    if (p.y < rulerHeight)
    {
        rulerDragStartTick = quantizedTickAtX(p.x, true);
        rulerDraggingLoop = true;
        rulerLoopDragMoved = false;
        return;
    }

    if (isModifierDown(event.mods, inputBindings.panViewModifier) && event.mods.isLeftButtonDown())
    {
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            editMode = EditMode::Pan;
            dragStartX = p.x;
            dragStartY = p.y;
            panStartViewPos = viewport->getViewPosition();
            return;
        }
    }

    const int laneIndex = (p.y - rulerHeight) / laneHeight;
    const auto clickedTrack = trackForVisibleLaneIndex(laneIndex);
    if (!clickedTrack.has_value())
        return;
    selectedTrack = *clickedTrack;

    const auto hit = findNoteAt(event.getPosition(), *clickedTrack);

    if (isModifierDown(event.mods, inputBindings.microtimingEditModifier) && hit.has_value())
    {
        if (isSelected(*hit))
        {
            primarySelectedNote = *hit;
            normalizeSelection();
        }
        else
        {
            setSingleSelection(*hit);
        }

        dragStartX = p.x;
        dragStartY = p.y;
        collectDragSnapshots();
        editMode = EditMode::MicroOffset;
        repaint();
        return;
    }

    if (editorTool == EditorTool::Erase)
    {
        editMode = EditMode::EraseDrag;
        eraseNoteAt(event.getPosition(), clickedTrack);
        repaint();
        return;
    }

    if (editorTool == EditorTool::Cut)
    {
        editMode = EditMode::Cut;
        if (hit.has_value())
        {
            const int cutTick = quantizedTickAtX(event.getPosition().x, true);
            if (splitNoteAtTick(hit->track, hit->index, cutTick))
            {
                emitTrackEdited(hit->track);
                repaint();
            }
        }
        return;
    }

    if (editorTool == EditorTool::Brush)
    {
        const int tick = quantizedTickAtX(p.x, true);
        editMode = EditMode::BrushDraw;
        drawTrack = *clickedTrack;
        placeDrawNoteAt(drawTrack, tick);
        drawVisitedTicks.insert(tick);
        repaint();
        return;
    }

    if (editorTool == EditorTool::Select)
    {
        if (hit.has_value())
        {
            const bool additive = event.mods.isShiftDown() || event.mods.isCommandDown() || event.mods.isCtrlDown();
            if (additive)
                toggleSelection(*hit);
            else if (!isSelected(*hit))
                setSingleSelection(*hit);
            repaint();
            return;
        }

        const bool additive = event.mods.isShiftDown() || event.mods.isCommandDown() || event.mods.isCtrlDown();
        marqueeBaseSelection = selectedNotes;
        if (!additive)
            clearSelectionInternal();

        marqueeStart = p;
        marqueeRect = juce::Rectangle<int>(p.x, p.y, 0, 0);
        marqueeAdditive = additive;
        editMode = EditMode::Marquee;
        repaint();
        return;
    }

    const bool forceDrawByModifier = isModifierDown(event.mods, inputBindings.drawModeModifier)
        && event.mods.isLeftButtonDown();

    if (forceDrawByModifier && !hit.has_value())
    {
        const int tick = quantizedTickAtX(event.getPosition().x, true);
        editMode = EditMode::Draw;
        drawTrack = *clickedTrack;
        placeDrawNoteAt(drawTrack, tick);
        drawVisitedTicks.insert(tick);
        repaint();
        return;
    }

    if (hit.has_value())
    {
        if (isSelected(*hit))
        {
            primarySelectedNote = *hit;
            normalizeSelection();
        }
        else
        {
            setSingleSelection(*hit);
        }

        dragStartX = p.x;
        dragStartY = p.y;

        for (const auto& track : project.tracks)
        {
            if (track.type != hit->track)
                continue;
            if (hit->index < 0 || hit->index >= static_cast<int>(track.notes.size()))
                break;

            const auto& note = track.notes[static_cast<size_t>(hit->index)];
            dragOriginalTicks = note.step * ticksPerStep() + note.microOffset;
            dragOriginalLength = std::max(1, note.length);
            dragOriginalEndTick = dragOriginalTicks + dragOriginalLength * ticksPerStep();
            dragOriginalVelocity = note.velocity;
            break;
        }

        collectDragSnapshots();

        if (isVelocityEditKeyDown())
        {
            editMode = EditMode::Velocity;
            velocityWaveModeActive = false;
            velocityOverlayPercent = dragOriginalVelocity * 100 / 127;
            velocityOverlayDelta = 0;
            repaint();
            return;
        }

        editMode = isStretchEditKeyDown() ? EditMode::StretchNote : EditMode::MoveNote;

        repaint();
        return;
    }

    const int tick = quantizedTickAtX(p.x, true);
    const bool added = placeDrawNoteAt(*clickedTrack, tick);
    const int step = juce::jlimit(0, totalSteps - 1, tick / ticksPerStep());
    if (added)
        emitTrackEdited(*clickedTrack);
    repaint();

    if (onCellClicked)
        onCellClicked(*clickedTrack, step);
}

void GridEditorComponent::mouseDrag(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();

    if (rulerDraggingLoop)
    {
        const int currentTick = quantizedTickAtX(p.x, true);
        const int startTick = juce::jmin(rulerDragStartTick, currentTick);
        const int endTick = juce::jmax(rulerDragStartTick, currentTick);
        rulerLoopDragMoved = rulerLoopDragMoved || (std::abs(currentTick - rulerDragStartTick) >= ticksForGridResolution());

        if (rulerLoopDragMoved && endTick > startTick)
        {
            loopRegionTicks = juce::Range<int>(startTick, endTick);
            if (onLoopRegionChanged)
                onLoopRegionChanged(loopRegionTicks);
            repaint();
        }
        return;
    }

    if (editMode == EditMode::Pan)
    {
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            const int dx = p.x - dragStartX;
            const int dy = p.y - dragStartY;
            viewport->setViewPosition(std::max(0, panStartViewPos.x - dx),
                                      std::max(0, panStartViewPos.y - dy));
        }
        return;
    }

    if (editMode == EditMode::Draw || editMode == EditMode::BrushDraw)
    {
        const int tick = quantizedTickAtX(p.x, true);
        if (drawVisitedTicks.insert(tick).second)
            placeDrawNoteAt(drawTrack, tick);
        movedDuringDrag = true;
        repaint();
        return;
    }

    if (editMode == EditMode::EraseDrag)
    {
        eraseNotesAlongSegment(lastDragPoint, p);
        lastDragPoint = p;
        movedDuringDrag = true;
        repaint();
        return;
    }

    if (editMode == EditMode::Marquee)
    {
        marqueeRect = juce::Rectangle<int>::leftTopRightBottom(juce::jmin(marqueeStart.x, p.x),
                                                                juce::jmin(marqueeStart.y, p.y),
                                                                juce::jmax(marqueeStart.x, p.x),
                                                                juce::jmax(marqueeStart.y, p.y));

        marqueeRect = marqueeRect.getIntersection(getLocalBounds().withTrimmedTop(rulerHeight));

        std::vector<SelectedNoteRef> picks;
        for (const auto& track : project.tracks)
        {
            const int row = visibleLaneIndex(track.type);
            if (row < 0)
                continue;
            for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
            {
                if (marqueeRect.intersects(noteBounds(track.notes[static_cast<size_t>(i)], row)))
                    picks.push_back({ track.type, i });
            }
        }

        selectedNotes = marqueeAdditive ? marqueeBaseSelection : std::vector<SelectedNoteRef>{};
        for (const auto& r : picks)
            if (!isSelected(r))
                selectedNotes.push_back(r);

        normalizeSelection();
        movedDuringDrag = true;
        repaint();
        return;
    }

    if (editMode == EditMode::MicroOffset)
    {
        const int deltaPx = p.x - dragStartX;
        const int deltaTicks = static_cast<int>(std::round(static_cast<float>(deltaPx) * 5.0f));
        if (applySelectionMicroOffsetDelta(deltaTicks))
            movedDuringDrag = true;
        repaint();
        return;
    }

    if (editMode == EditMode::Velocity)
    {
        const int dy = dragStartY - p.y;
        const int deltaVel = static_cast<int>(std::round(static_cast<float>(dy) * 0.6f));
        if (applySelectionVelocityWave(deltaVel, p.x))
            movedDuringDrag = true;
        repaint();
        return;
    }

    if (selectedNotes.empty())
        return;

    normalizeSelection();
    if (selectedNotes.empty())
        return;

    auto ref = primarySelectedNote.has_value() ? *primarySelectedNote : selectedNotes.front();
    auto* track = findMutableTrack(ref.track);
    if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()) || dragSnapshots.empty())
        return;

    const int bars = juce::jmax(1, project.params.bars);

    if (editMode == EditMode::StretchNote)
    {
        const int deltaPx = p.x - dragStartX;
        const int rawDeltaTicks = static_cast<int>(std::round(static_cast<float>(deltaPx) * static_cast<float>(ticksPerStep()) / juce::jmax(1.0f, stepWidth)));
        const int deltaTicks = juce::jmax(0, quantizeTick(rawDeltaTicks));
        applySelectionStretchTicks(deltaTicks, bars);
        movedDuringDrag = true;
        repaint();
        return;
    }

    if (editMode != EditMode::MoveNote)
        return;

    const int deltaPx = p.x - dragStartX;
    int deltaTicks = static_cast<int>(std::round(static_cast<float>(deltaPx) * static_cast<float>(ticksPerStep()) / juce::jmax(1.0f, stepWidth)));
    deltaTicks = quantizeTick(deltaTicks);
    applySelectionMoveTicks(deltaTicks, bars);
    movedDuringDrag = true;

    repaint();
}

void GridEditorComponent::mouseUp(const juce::MouseEvent& event)
{
    if (rulerDraggingLoop)
    {
        const int tick = quantizedTickAtX(event.getPosition().x, true);
        if (!rulerLoopDragMoved)
        {
            previewStartTick = tick;
            const int totalSteps = juce::jmax(1, project.params.bars * 16);
            const int step = juce::jlimit(0, juce::jmax(0, totalSteps - 1), tick / ticksPerStep());
            previewStartStep = step;
            if (onCellClicked)
                onCellClicked(selectedTrack, step);
        }
        else if (loopRegionTicks.has_value() && loopRegionTicks->getLength() <= 0)
        {
            loopRegionTicks.reset();
            if (onLoopRegionChanged)
                onLoopRegionChanged(std::nullopt);
        }

        rulerDraggingLoop = false;
        rulerLoopDragMoved = false;
        repaint();
        return;
    }

    if (editMode == EditMode::None)
        return;

    if (editMode == EditMode::MoveNote
        || editMode == EditMode::StretchNote
        || editMode == EditMode::Velocity
        || editMode == EditMode::MicroOffset)
    {
        std::set<TrackType> changed;
        for (const auto& ref : selectedNotes)
            changed.insert(ref.track);
        if (changed.empty() && primarySelectedNote.has_value())
            changed.insert(primarySelectedNote->track);
        for (const auto t : changed)
            emitTrackEdited(t);
    }
    else if (editMode == EditMode::Draw || editMode == EditMode::BrushDraw)
    {
        emitTrackEdited(drawTrack);
    }
    else if (editMode == EditMode::EraseDrag)
    {
        emitTrackEdited(selectedTrack);
    }

    if (editMode == EditMode::Marquee)
    {
        marqueeRect = {};
        marqueeBaseSelection.clear();
    }

    editMode = EditMode::None;
    velocityWaveModeActive = false;
    velocityOverlayDelta = 0;
    drawVisitedTicks.clear();
    eraseVisitedKeys.clear();
    dragSnapshots.clear();
}

void GridEditorComponent::mouseMove(const juce::MouseEvent& event)
{
    updateHoverState(event.getPosition());
}

void GridEditorComponent::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    hoverNote.reset();
    hoverZone = HoverZone::None;
    applyCursorForHover();
    repaint();
}

std::optional<GridEditorComponent::SelectedNoteRef> GridEditorComponent::findClosestNoteAt(juce::Point<int> position,
                                                                                            TrackType lane,
                                                                                            int maxDistancePx) const
{
    for (const auto& track : project.tracks)
    {
        if (track.type != lane)
            continue;

        int bestIndex = -1;
        int bestDistance = std::numeric_limits<int>::max();
        const int row = visibleLaneIndex(track.type);
        if (row < 0)
            return std::nullopt;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
        {
            const auto b = noteBounds(track.notes[static_cast<size_t>(i)], row).expanded(3, 3);
            if (!b.contains(position))
                continue;

            const int centerDistance = std::abs(b.getCentreX() - position.x);
            if (centerDistance < bestDistance)
            {
                bestDistance = centerDistance;
                bestIndex = i;
            }
        }

        if (bestIndex >= 0 && bestDistance <= maxDistancePx)
            return SelectedNoteRef { lane, bestIndex };

        break;
    }

    return std::nullopt;
}

std::optional<GridEditorComponent::SelectedNoteRef> GridEditorComponent::findSub808NoteForPitchEdit(const juce::MouseEvent& event) const
{
    if (primarySelectedNote.has_value())
        return primarySelectedNote;

    const auto p = event.getPosition();
    const int laneIndex = (p.y - rulerHeight) / laneHeight;
    const auto lane = trackForVisibleLaneIndex(laneIndex);
    if (!lane.has_value())
        return std::nullopt;

    return findNoteAt(p, *lane);
}

int GridEditorComponent::noteStartTick(const NoteEvent& note) const
{
    return note.step * ticksPerStep() + note.microOffset;
}

int GridEditorComponent::noteEndTick(const NoteEvent& note) const
{
    return noteStartTick(note) + std::max(1, note.length) * ticksPerStep();
}

bool GridEditorComponent::setNoteStartTick(NoteEvent& note, int targetTick, int bars)
{
    const int maxTicks = juce::jmax(1, bars * 16 * ticksPerStep());
    int clamped = juce::jlimit(0, maxTicks - 1, targetTick);
    int step = clamped / ticksPerStep();
    int micro = clamped - step * ticksPerStep();
    if (micro > ticksPerStep() / 2)
    {
        micro -= ticksPerStep();
        ++step;
    }

    step = juce::jlimit(0, bars * 16 - 1, step);
    note.step = step;
    note.microOffset = juce::jlimit(-240, 240, micro);
    return true;
}

bool GridEditorComponent::splitNoteAtTick(TrackType trackType, int noteIndex, int cutTick)
{
    auto* track = findMutableTrack(trackType);
    if (track == nullptr || noteIndex < 0 || noteIndex >= static_cast<int>(track->notes.size()))
        return false;

    auto& note = track->notes[static_cast<size_t>(noteIndex)];
    const int startTick = noteStartTick(note);
    const int endTick = noteEndTick(note);
    const int minLenTicks = ticksPerStep();

    if (cutTick <= startTick + minLenTicks || cutTick >= endTick - minLenTicks)
        return false;

    NoteEvent second = note;
    setNoteStartTick(second, cutTick, juce::jmax(1, project.params.bars));

    const int firstLen = juce::jmax(1,
        static_cast<int>(std::ceil(static_cast<double>(cutTick - startTick) / static_cast<double>(ticksPerStep()))));
    const int secondLen = juce::jmax(1,
        static_cast<int>(std::ceil(static_cast<double>(endTick - cutTick) / static_cast<double>(ticksPerStep()))));

    note.length = firstLen;
    second.length = secondLen;
    track->notes.push_back(second);
    sortTrackNotes(*track);
    return true;
}

bool GridEditorComponent::eraseNoteAt(juce::Point<int> position, std::optional<TrackType> laneHint)
{
    std::optional<SelectedNoteRef> hit;
    if (laneHint.has_value())
        hit = findNoteAt(position, *laneHint);
    else
        hit = findNoteAt(position);

    if (!hit.has_value())
        return false;

    auto* track = findMutableTrack(hit->track);
    if (track == nullptr || hit->index < 0 || hit->index >= static_cast<int>(track->notes.size()))
        return false;

    const auto& note = track->notes[static_cast<size_t>(hit->index)];
    const int key = (static_cast<int>(hit->track) << 24)
        ^ ((noteStartTick(note) & 0x0fffff) << 2)
        ^ (note.pitch & 0x3);
    if (!eraseVisitedKeys.insert(key).second)
        return false;

    track->notes.erase(track->notes.begin() + hit->index);
    normalizeSelection();
    return true;
}

bool GridEditorComponent::eraseNotesAlongSegment(juce::Point<int> from, juce::Point<int> to)
{
    bool changed = false;
    const int steps = juce::jmax(1, static_cast<int>(std::ceil(from.getDistanceFrom(to) / 4.0f)));
    for (int i = 0; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        juce::Point<int> p(static_cast<int>(std::round(juce::jmap(t, static_cast<float>(from.x), static_cast<float>(to.x)))),
                           static_cast<int>(std::round(juce::jmap(t, static_cast<float>(from.y), static_cast<float>(to.y)))));
        changed = eraseNoteAt(p) || changed;
    }
    return changed;
}

bool GridEditorComponent::updatePitchForSub808Selection(int semitoneDelta)
{
    if (selectedNotes.empty() && !primarySelectedNote.has_value())
        return false;

    bool changed = false;
    for (const auto& ref : selectedNotes)
    {
        if (ref.track != TrackType::Sub808)
            continue;
        auto* track = findMutableTrack(ref.track);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;
        auto& note = track->notes[static_cast<size_t>(ref.index)];
        const int next = juce::jlimit(24, 84, note.pitch + semitoneDelta);
        if (next != note.pitch)
        {
            note.pitch = next;
            changed = true;
        }
    }

    if (!changed && primarySelectedNote.has_value() && primarySelectedNote->track == TrackType::Sub808)
    {
        const auto& ref = *primarySelectedNote;
        auto* track = findMutableTrack(ref.track);
        if (track != nullptr && ref.index >= 0 && ref.index < static_cast<int>(track->notes.size()))
        {
            auto& note = track->notes[static_cast<size_t>(ref.index)];
            const int next = juce::jlimit(24, 84, note.pitch + semitoneDelta);
            if (next != note.pitch)
            {
                note.pitch = next;
                changed = true;
            }
        }
    }

    return changed;
}

void GridEditorComponent::sortTrackNotes(TrackState& track)
{
    std::stable_sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.microOffset < b.microOffset;
    });
}

int GridEditorComponent::resizeHandleWidthPx(const NoteEvent& note, int row) const
{
    const auto b = noteBounds(note, row);
    return juce::jlimit(9, 18, b.getWidth() / 4);
}

void GridEditorComponent::updateHoverState(juce::Point<int> position)
{
    hoverNote.reset();
    hoverZone = HoverZone::None;

    const int laneIndex = (position.y - rulerHeight) / laneHeight;
    const auto lane = trackForVisibleLaneIndex(laneIndex);
    if (!lane.has_value())
    {
        applyCursorForHover();
        return;
    }

    auto hit = findNoteAt(position, *lane);
    if (!hit.has_value())
    {
        applyCursorForHover();
        repaint();
        return;
    }

    hoverNote = hit;
    for (const auto& track : project.tracks)
    {
        if (track.type != hit->track)
            continue;
        if (hit->index < 0 || hit->index >= static_cast<int>(track.notes.size()))
            break;

        const int row = visibleLaneIndex(track.type);
        if (row < 0)
            break;

        hoverZone = HoverZone::NoteBody;
        break;
    }

    applyCursorForHover();
    repaint();
}

void GridEditorComponent::applyCursorForHover()
{
    if (hoverZone == HoverZone::NoteBody && isStretchEditKeyDown())
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(toolCursorFor(editorTool));
}

bool GridEditorComponent::isSelected(const SelectedNoteRef& ref) const
{
    return std::any_of(selectedNotes.begin(), selectedNotes.end(), [&ref](const SelectedNoteRef& s)
    {
        return s.track == ref.track && s.index == ref.index;
    });
}

void GridEditorComponent::clearSelectionInternal()
{
    selectedNotes.clear();
    primarySelectedNote.reset();
}

void GridEditorComponent::setSingleSelection(const SelectedNoteRef& ref)
{
    selectedNotes.clear();
    selectedNotes.push_back(ref);
    primarySelectedNote = ref;
}

void GridEditorComponent::toggleSelection(const SelectedNoteRef& ref)
{
    auto it = std::find_if(selectedNotes.begin(), selectedNotes.end(), [&ref](const SelectedNoteRef& s)
    {
        return s.track == ref.track && s.index == ref.index;
    });

    if (it != selectedNotes.end())
        selectedNotes.erase(it);
    else
        selectedNotes.push_back(ref);

    if (selectedNotes.empty())
        primarySelectedNote.reset();
    else
        primarySelectedNote = selectedNotes.back();
}

void GridEditorComponent::normalizeSelection()
{
    std::vector<SelectedNoteRef> valid;
    valid.reserve(selectedNotes.size());

    for (const auto& ref : selectedNotes)
    {
        const auto trackIt = std::find_if(project.tracks.begin(), project.tracks.end(), [&ref](const TrackState& t)
        {
            return t.type == ref.track;
        });
        if (trackIt == project.tracks.end())
            continue;
        if (ref.index < 0 || ref.index >= static_cast<int>(trackIt->notes.size()))
            continue;
        valid.push_back(ref);
    }

    selectedNotes.swap(valid);

    if (selectedNotes.empty())
    {
        primarySelectedNote.reset();
        return;
    }

    if (!primarySelectedNote.has_value() || !isSelected(*primarySelectedNote))
        primarySelectedNote = selectedNotes.front();
}

void GridEditorComponent::collectDragSnapshots()
{
    normalizeSelection();
    dragSnapshots.clear();
    dragSnapshots.reserve(selectedNotes.size());

    for (const auto& ref : selectedNotes)
    {
        auto* track = findMutableTrack(ref.track);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;

        const auto& n = track->notes[static_cast<size_t>(ref.index)];
        DragSnapshot snap;
        snap.track = ref.track;
        snap.index = ref.index;
        snap.startTick = noteStartTick(n);
        snap.startLength = std::max(1, n.length);
        snap.startEndTick = snap.startTick + snap.startLength * ticksPerStep();
        snap.startVelocity = n.velocity;
        snap.startMicroOffset = n.microOffset;
        snap.startPitch = n.pitch;
        dragSnapshots.push_back(snap);
    }
}

bool GridEditorComponent::applySelectionVelocityDelta(int deltaVel)
{
    bool changed = false;
    if (dragSnapshots.empty())
        collectDragSnapshots();

    int velocitySum = 0;
    int velocityCount = 0;
    int startVelocitySum = 0;

    for (const auto& snap : dragSnapshots)
    {
        auto* track = findMutableTrack(snap.track);
        if (track == nullptr || snap.index < 0 || snap.index >= static_cast<int>(track->notes.size()))
            continue;
        auto& n = track->notes[static_cast<size_t>(snap.index)];
        const int next = juce::jlimit(1, 127, snap.startVelocity + deltaVel);
        if (next != n.velocity)
        {
            n.velocity = next;
            changed = true;
        }

        velocitySum += n.velocity;
        startVelocitySum += snap.startVelocity;
        ++velocityCount;
    }

    if (velocityCount > 0)
    {
        velocityOverlayPercent = static_cast<int>(std::round((static_cast<float>(velocitySum) / static_cast<float>(velocityCount)) * 100.0f / 127.0f));
        velocityOverlayDelta = static_cast<int>(std::round(static_cast<float>(velocitySum - startVelocitySum) / static_cast<float>(velocityCount)));
    }

    return changed;
}

bool GridEditorComponent::applySelectionVelocityWave(int deltaVel, int currentMouseX)
{
    if (dragSnapshots.empty())
        collectDragSnapshots();

    if (dragSnapshots.empty())
        return false;

    const int minMouseX = juce::jmin(dragStartX, currentMouseX);
    const int maxMouseX = juce::jmax(dragStartX, currentMouseX);
    velocityWaveModeActive = std::abs(currentMouseX - dragStartX) >= 8 && dragSnapshots.size() > 1;

    if (!velocityWaveModeActive)
        return applySelectionVelocityDelta(deltaVel);

    bool changed = false;
    int velocitySum = 0;
    int velocityCount = 0;
    int startVelocitySum = 0;
    const int span = juce::jmax(1, maxMouseX - minMouseX);

    for (const auto& snap : dragSnapshots)
    {
        auto* track = findMutableTrack(snap.track);
        if (track == nullptr || snap.index < 0 || snap.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& n = track->notes[static_cast<size_t>(snap.index)];
        const int noteX = static_cast<int>(std::round((static_cast<float>(snap.startTick) / static_cast<float>(ticksPerStep())) * stepWidth));
        const float ratio = juce::jlimit(0.0f,
                                         1.0f,
                                         static_cast<float>(noteX - minMouseX) / static_cast<float>(span));
        const float weighted = currentMouseX >= dragStartX ? ratio : (1.0f - ratio);
        const int next = juce::jlimit(1, 127, snap.startVelocity + static_cast<int>(std::round(static_cast<float>(deltaVel) * weighted)));
        if (next != n.velocity)
        {
            n.velocity = next;
            changed = true;
        }

        velocitySum += n.velocity;
        startVelocitySum += snap.startVelocity;
        ++velocityCount;
    }

    if (velocityCount > 0)
    {
        velocityOverlayPercent = static_cast<int>(std::round((static_cast<float>(velocitySum) / static_cast<float>(velocityCount)) * 100.0f / 127.0f));
        velocityOverlayDelta = static_cast<int>(std::round(static_cast<float>(velocitySum - startVelocitySum) / static_cast<float>(velocityCount)));
    }

    return changed;
}

bool GridEditorComponent::applySelectionMicroOffsetDelta(int deltaTicks)
{
    if (dragSnapshots.empty())
        collectDragSnapshots();

    bool changed = false;
    const int microLimit = ticksPerStep();

    for (const auto& snap : dragSnapshots)
    {
        auto* track = findMutableTrack(snap.track);
        if (track == nullptr || snap.index < 0 || snap.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& note = track->notes[static_cast<size_t>(snap.index)];
        const int nextMicro = juce::jlimit(-microLimit, microLimit, snap.startMicroOffset + deltaTicks);
        if (note.microOffset != nextMicro)
        {
            note.microOffset = nextMicro;
            changed = true;
        }
    }

    return changed;
}

bool GridEditorComponent::applySelectionMoveTicks(int deltaTicks, int bars)
{
    if (dragSnapshots.empty())
        return false;

    bool changed = false;
    const int maxTick = bars * 16 * ticksPerStep() - 1;

    for (const auto& snap : dragSnapshots)
    {
        auto* track = findMutableTrack(snap.track);
        if (track == nullptr || snap.index < 0 || snap.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& note = track->notes[static_cast<size_t>(snap.index)];
        const int targetTick = juce::jlimit(0, juce::jmax(0, maxTick), snap.startTick + deltaTicks);
        const int previousStep = note.step;
        const int previousMicro = note.microOffset;
        setNoteStartTick(note, targetTick, bars);

        if (note.step != previousStep || note.microOffset != previousMicro)
        {
            changed = true;
        }
    }

    return changed;
}

bool GridEditorComponent::applySelectionStretchTicks(int deltaTicks, int bars)
{
    if (dragSnapshots.empty() || deltaTicks <= 0)
        return false;

    bool changed = false;
    const int maxTick = bars * 16 * ticksPerStep();

    for (const auto& snap : dragSnapshots)
    {
        auto* track = findMutableTrack(snap.track);
        if (track == nullptr || snap.index < 0 || snap.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& current = track->notes[static_cast<size_t>(snap.index)];
        const int clampedEndTick = juce::jlimit(snap.startTick + ticksPerStep(), maxTick, snap.startEndTick + deltaTicks);
        const int nextLength = juce::jlimit(1,
                                            bars * 16,
                                            static_cast<int>(std::ceil(static_cast<double>(clampedEndTick - snap.startTick)
                                                                       / static_cast<double>(ticksPerStep()))));
        if (current.length != nextLength)
        {
            current.length = nextLength;
            changed = true;
        }
    }

    return changed;
}

bool GridEditorComponent::copySelectionInternal(bool removeAfterCopy)
{
    normalizeSelection();
    if (selectedNotes.empty())
        return false;

    int minTick = std::numeric_limits<int>::max();
    int maxTick = 0;
    clipboardNotes.clear();

    for (const auto& ref : selectedNotes)
    {
        auto* track = findMutableTrack(ref.track);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;

        const auto& n = track->notes[static_cast<size_t>(ref.index)];
        const int start = noteStartTick(n);
        const int end = noteEndTick(n);
        minTick = juce::jmin(minTick, start);
        maxTick = juce::jmax(maxTick, end);

        clipboardNotes.push_back({ ref.track, n, 0 });
    }

    if (clipboardNotes.empty())
        return false;

    for (auto& c : clipboardNotes)
    {
        const int tick = noteStartTick(c.note);
        c.relativeTick = tick - minTick;
    }

    clipboardSourceStartTick = minTick;
    clipboardSpanTicks = juce::jmax(ticksPerStep(), maxTick - minTick);

    if (removeAfterCopy)
        return deleteSelectedNotes();

    return true;
}

GridEditorComponent::ContextMenuTarget GridEditorComponent::contextMenuTargetAt(juce::Point<int> p,
                                                                                 std::optional<TrackType> laneHint,
                                                                                 std::optional<SelectedNoteRef>* hitOut)
{
    std::optional<SelectedNoteRef> hit;
    if (laneHint.has_value())
        hit = findNoteAt(p, *laneHint);
    else
        hit = findNoteAt(p);

    if (hitOut != nullptr)
        *hitOut = hit;

    normalizeSelection();
    if (!hit.has_value())
        return ContextMenuTarget::Empty;

    if (selectedNotes.size() > 1 && isSelected(*hit))
        return ContextMenuTarget::Selection;

    return ContextMenuTarget::SingleNote;
}

void GridEditorComponent::showNoteContextMenu(juce::Point<int> p, std::optional<TrackType> laneHint)
{
    std::optional<SelectedNoteRef> hit;
    const auto target = contextMenuTargetAt(p, laneHint, &hit);

    if (hit.has_value())
    {
        if (target == ContextMenuTarget::SingleNote)
            setSingleSelection(*hit);
    }

    juce::PopupMenu menu;
    constexpr int kActionCut = 1;
    constexpr int kActionCopy = 2;
    constexpr int kActionDuplicate = 3;
    constexpr int kActionDelete = 4;
    constexpr int kActionSelectAll = 5;
    constexpr int kActionDeselect = 6;
    constexpr int kActionPaste = 7;
    constexpr int kActionSplit = 8;
    constexpr int kActionQuickRoll = 9;
    constexpr int kActionAdvancedRollBase = 100;
    constexpr int kActionFillBase = 200;

    juce::PopupMenu fillMenu;
    fillMenu.addItem(kActionFillBase + 2, "Fill every 2");
    fillMenu.addItem(kActionFillBase + 4, "Fill every 4");
    fillMenu.addItem(kActionFillBase + 8, "Fill every 8");
    fillMenu.addItem(kActionFillBase + 16, "Fill every 16");

    if (target == ContextMenuTarget::Empty)
    {
        menu.addItem(kActionPaste, "Paste", !clipboardNotes.empty());
        if (laneHint.has_value())
            menu.addSubMenu("Fill Notes", fillMenu, true);
        menu.addItem(kActionSelectAll, "Select All", std::any_of(project.tracks.begin(), project.tracks.end(), [](const TrackState& track)
        {
            return !track.notes.empty();
        }));
        menu.addItem(kActionDeselect, "Deselect", !selectedNotes.empty());
    }
    else if (target == ContextMenuTarget::SingleNote)
    {
        menu.addItem(kActionCut, "Cut");
        menu.addItem(kActionCopy, "Copy");
        menu.addItem(kActionDelete, "Delete");
        menu.addItem(kActionSplit, "Split", hit.has_value());

        const bool quickRollEnabled = canQuickRollSelection();
        const auto advancedDivisions = availableAdvancedRollDivisions();
        menu.addSeparator();
        menu.addSubMenu("Fill Notes", fillMenu, laneHint.has_value());
        menu.addItem(kActionQuickRoll, "Create Roll", quickRollEnabled);

        juce::PopupMenu advancedRollMenu;
        for (const int division : advancedDivisions)
            advancedRollMenu.addItem(kActionAdvancedRollBase + division, "Divide x" + juce::String(division));
        menu.addSubMenu("Advanced Roll", advancedRollMenu, !advancedDivisions.empty());
    }
    else
    {
        menu.addItem(kActionCut, "Cut");
        menu.addItem(kActionCopy, "Copy");
        menu.addItem(kActionDuplicate, "Duplicate");
        menu.addItem(kActionDelete, "Delete");

        const bool quickRollEnabled = canQuickRollSelection();
        const auto advancedDivisions = availableAdvancedRollDivisions();
        menu.addSeparator();
        menu.addSubMenu("Fill Notes", fillMenu, laneHint.has_value());
        menu.addItem(kActionQuickRoll, "Create Roll", quickRollEnabled);

        juce::PopupMenu advancedRollMenu;
        for (const int division : advancedDivisions)
            advancedRollMenu.addItem(kActionAdvancedRollBase + division, "Divide x" + juce::String(division));
        menu.addSubMenu("Advanced Roll", advancedRollMenu, !advancedDivisions.empty());
    }

    menu.showMenuAsync(juce::PopupMenu::Options{}.withParentComponent(this),
                       [safe = juce::Component::SafePointer<GridEditorComponent>(this), laneHint, p](int choice)
                       {
                           if (safe == nullptr || choice <= 0)
                               return;
                           safe->applyContextMenuAction(choice, laneHint, p);
                       });
}

bool GridEditorComponent::applyContextMenuAction(int actionId, std::optional<TrackType> laneHint, juce::Point<int> p)
{
    if (actionId >= 100)
    {
        if (actionId >= 200)
        {
            const auto targetLane = laneHint.value_or(selectedTrack);
            const int intervalSteps = actionId - 200;
            const int barTicks = 16 * ticksPerStep();
            const int anchorTick = quantizedTickAtX(p.x, true);
            const int blockStart = (anchorTick / barTicks) * barTicks;
            const int blockEnd = juce::jmin(juce::jmax(blockStart + barTicks, blockStart + ticksPerStep()), project.params.bars * barTicks);
            const auto fillRange = loopRegionTicks.value_or(juce::Range<int>(blockStart, blockEnd));
            return fillLaneRange(targetLane, fillRange, intervalSteps);
        }

        return rollSelectionWithDivisions(actionId - 100);
    }
    if (actionId == 1)
        return cutSelectionToClipboard();
    if (actionId == 2)
        return copySelectionToClipboard();
    if (actionId == 3)
        return duplicateSelectionToRight();
    if (actionId == 4)
        return deleteSelectedNotes();
    if (actionId == 5)
        return selectAllNotes();
    if (actionId == 6)
    {
        clearSelectionInternal();
        repaint();
        return true;
    }
    if (actionId == 7)
        return pasteClipboardAtPlayhead();
    if (actionId == 8 && laneHint.has_value())
    {
        if (auto hit = findNoteAt(p, *laneHint); hit.has_value())
        {
            const bool changed = splitNoteAtTick(hit->track, hit->index, quantizedTickAtX(p.x, true));
            if (changed)
            {
                emitTrackEdited(hit->track);
                repaint();
            }
            return changed;
        }
        return false;
    }
    if (actionId == 9)
        return rollSelectionQuick();

    juce::ignoreUnused(p);
    return false;
}

bool GridEditorComponent::canQuickRollSelection() const
{
    const auto available = availableAdvancedRollDivisions();
    return std::find(available.begin(), available.end(), 2) != available.end();
}

std::vector<int> GridEditorComponent::availableAdvancedRollDivisions() const
{
    std::vector<SelectedNoteRef> validSelection;
    validSelection.reserve(selectedNotes.size());

    for (const auto& ref : selectedNotes)
    {
        const auto trackIt = std::find_if(project.tracks.begin(), project.tracks.end(), [&ref](const TrackState& track)
        {
            return track.type == ref.track;
        });

        if (trackIt == project.tracks.end() || ref.index < 0 || ref.index >= static_cast<int>(trackIt->notes.size()))
            continue;

        validSelection.push_back(ref);
    }

    if (validSelection.empty())
        return {};

    static constexpr int candidates[] { 2, 3, 4, 6, 8, 12, 16 };
    std::vector<int> divisions;

    for (const int division : candidates)
    {
        bool validForAll = true;
        for (const auto& ref : validSelection)
        {
            const auto trackIt = std::find_if(project.tracks.begin(), project.tracks.end(), [&ref](const TrackState& track)
            {
                return track.type == ref.track;
            });
            if (trackIt == project.tracks.end() || ref.index < 0 || ref.index >= static_cast<int>(trackIt->notes.size()))
            {
                validForAll = false;
                break;
            }

            const auto& note = trackIt->notes[static_cast<size_t>(ref.index)];
            if (note.length < division || (note.length % division) != 0)
            {
                validForAll = false;
                break;
            }
        }

        if (validForAll)
            divisions.push_back(division);
    }

    return divisions;
}

bool GridEditorComponent::rollSelectionQuick()
{
    return rollSelectionWithDivisions(2);
}

bool GridEditorComponent::rollSelectionWithDivisions(int divisions)
{
    normalizeSelection();
    if (selectedNotes.empty() || divisions < 2)
        return false;

    const auto available = availableAdvancedRollDivisions();
    if (std::find(available.begin(), available.end(), divisions) == available.end())
        return false;

    std::set<TrackType> changedTracks;
    std::vector<std::pair<TrackType, std::vector<NoteEvent>>> insertedNotesByTrack;

    auto insertedBucketFor = [&insertedNotesByTrack](TrackType trackType) -> std::vector<NoteEvent>&
    {
        auto it = std::find_if(insertedNotesByTrack.begin(), insertedNotesByTrack.end(), [trackType](const auto& entry)
        {
            return entry.first == trackType;
        });

        if (it == insertedNotesByTrack.end())
        {
            insertedNotesByTrack.push_back({ trackType, {} });
            return insertedNotesByTrack.back().second;
        }

        return it->second;
    };

    auto refs = selectedNotes;
    std::sort(refs.begin(), refs.end(), [](const SelectedNoteRef& a, const SelectedNoteRef& b)
    {
        if (a.track != b.track)
            return static_cast<int>(a.track) > static_cast<int>(b.track);
        return a.index > b.index;
    });

    const int bars = juce::jmax(1, project.params.bars);
    for (const auto& ref : refs)
    {
        auto* track = findMutableTrack(ref.track);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;

        const auto source = track->notes[static_cast<size_t>(ref.index)];
        const int startTick = noteStartTick(source);
        const int segmentSteps = source.length / divisions;

        for (int i = 0; i < divisions; ++i)
        {
            NoteEvent rolled = source;
            rolled.length = segmentSteps;
            setNoteStartTick(rolled, startTick + i * segmentSteps * ticksPerStep(), bars);
            insertedBucketFor(ref.track).push_back(rolled);
        }

        track->notes.erase(track->notes.begin() + ref.index);
        changedTracks.insert(ref.track);
    }

    clearSelectionInternal();
    for (auto& inserted : insertedNotesByTrack)
    {
        auto* track = findMutableTrack(inserted.first);
        if (track == nullptr)
            continue;

        for (const auto& note : inserted.second)
            track->notes.push_back(note);
        sortTrackNotes(*track);

        std::vector<bool> consumed(track->notes.size(), false);
        for (const auto& note : inserted.second)
        {
            for (int i = static_cast<int>(track->notes.size()) - 1; i >= 0; --i)
            {
                if (consumed[static_cast<size_t>(i)])
                    continue;

                const auto& current = track->notes[static_cast<size_t>(i)];
                if (noteStartTick(current) == noteStartTick(note)
                    && current.length == note.length
                    && current.velocity == note.velocity
                    && current.pitch == note.pitch
                    && current.microOffset == note.microOffset)
                {
                    consumed[static_cast<size_t>(i)] = true;
                    selectedNotes.push_back({ inserted.first, i });
                    primarySelectedNote = SelectedNoteRef{ inserted.first, i };
                    break;
                }
            }
        }
    }

    for (const auto trackType : changedTracks)
        emitTrackEdited(trackType);
    repaint();
    return !changedTracks.empty();
}

int GridEditorComponent::clampTickToProject(int tick) const
{
    const int bars = juce::jmax(1, project.params.bars);
    const int maxTicks = bars * 16 * ticksPerStep();
    return juce::jlimit(0, juce::jmax(0, maxTicks - 1), tick);
}

int GridEditorComponent::quantizeTick(int tick) const
{
    const int snap = juce::jmax(1, snapTicksForCurrentResolution());
    return static_cast<int>(std::round(static_cast<float>(tick) / static_cast<float>(snap))) * snap;
}

int GridEditorComponent::tickAtX(int x) const
{
    const float stepFloat = static_cast<float>(x) / juce::jmax(1.0f, stepWidth);
    const int tick = static_cast<int>(std::round(stepFloat * static_cast<float>(ticksPerStep())));
    return clampTickToProject(tick);
}

int GridEditorComponent::snappedTickFromTick(int tick) const
{
    return clampTickToProject(quantizeTick(tick));
}

bool GridEditorComponent::isModifierDown(const juce::ModifierKeys& mods, juce::ModifierKeys::Flags modifier) const
{
    if (modifier == juce::ModifierKeys::noModifiers)
        return false;

    return (mods.getRawFlags() & modifier) != 0;
}

bool GridEditorComponent::isVelocityEditKeyDown() const
{
    const int code = inputBindings.velocityEditKeyCode;
    if (code <= 0)
        return false;

    if (juce::KeyPress::isKeyCurrentlyDown(code))
        return true;

    if (code >= 'A' && code <= 'Z')
        return juce::KeyPress::isKeyCurrentlyDown(code + 32);

    if (code >= 'a' && code <= 'z')
        return juce::KeyPress::isKeyCurrentlyDown(code - 32);

    return false;
}

bool GridEditorComponent::isStretchEditKeyDown() const
{
    const int code = inputBindings.stretchNoteKeyCode;
    if (code <= 0)
        return false;

    const auto modifiers = juce::ModifierKeys::getCurrentModifiersRealtime();
    if (!modifiers.isCtrlDown())
        return false;

    if (juce::KeyPress::isKeyCurrentlyDown(code))
        return true;

    if (code >= 'A' && code <= 'Z')
        return juce::KeyPress::isKeyCurrentlyDown(code + 32);

    if (code >= 'a' && code <= 'z')
        return juce::KeyPress::isKeyCurrentlyDown(code - 32);

    return false;
}

bool GridEditorComponent::isVisibleLane(TrackType type) const
{
    const auto* info = TrackRegistry::find(type);
    return info != nullptr && info->visibleInUI;
}

int GridEditorComponent::visibleLaneIndex(TrackType type) const
{
    std::vector<TrackType> ordered;
    ordered.reserve(project.tracks.size());

    for (const auto& lane : laneDisplayOrder)
        if (isVisibleLane(lane))
            ordered.push_back(lane);

    for (const auto& track : project.tracks)
        if (isVisibleLane(track.type)
            && std::find(ordered.begin(), ordered.end(), track.type) == ordered.end())
            ordered.push_back(track.type);

    for (int index = 0; index < static_cast<int>(ordered.size()); ++index)
    {
        if (ordered[static_cast<size_t>(index)] == type)
            return index;
    }

    return -1;
}

std::optional<TrackType> GridEditorComponent::trackForVisibleLaneIndex(int laneIndex) const
{
    if (laneIndex < 0)
        return std::nullopt;

    std::vector<TrackType> ordered;
    ordered.reserve(project.tracks.size());

    for (const auto& lane : laneDisplayOrder)
        if (isVisibleLane(lane))
            ordered.push_back(lane);

    for (const auto& track : project.tracks)
        if (isVisibleLane(track.type)
            && std::find(ordered.begin(), ordered.end(), track.type) == ordered.end())
            ordered.push_back(track.type);

    if (laneIndex >= static_cast<int>(ordered.size()))
        return std::nullopt;

    return ordered[static_cast<size_t>(laneIndex)];

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
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
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
            if (noteBounds(track.notes[static_cast<size_t>(i)], row).expanded(3, 3).contains(position))
                return SelectedNoteRef { track.type, i };
        }
    }

    return std::nullopt;
}

std::optional<GridEditorComponent::SelectedNoteRef> GridEditorComponent::findNoteAt(juce::Point<int> position, TrackType lane) const
{
    for (const auto& track : project.tracks)
    {
        if (track.type != lane)
            continue;

        const int row = visibleLaneIndex(track.type);
        if (row < 0)
            return std::nullopt;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
        {
            if (noteBounds(track.notes[static_cast<size_t>(i)], row).expanded(3, 3).contains(position))
                return SelectedNoteRef { track.type, i };
        }

        break;
    }

    return std::nullopt;
}

TrackState* GridEditorComponent::findMutableTrack(TrackType type)
{
    auto it = std::find_if(project.tracks.begin(), project.tracks.end(), [type](const TrackState& track)
    {
        return track.type == type;
    });

    return (it != project.tracks.end()) ? &(*it) : nullptr;
}

void GridEditorComponent::emitTrackEdited(TrackType type)
{
    if (!onTrackNotesEdited)
        return;

    for (const auto& track : project.tracks)
    {
        if (track.type == type)
        {
            onTrackNotesEdited(track.type, track.notes);
            return;
        }
    }
}

int GridEditorComponent::stepAtX(int x) const
{
    const int bars = juce::jmax(1, project.params.bars);
    const int totalSteps = bars * 16;
    const int step = static_cast<int>(std::floor(static_cast<float>(x) / juce::jmax(1.0f, stepWidth)));
    return juce::jlimit(0, juce::jmax(0, totalSteps - 1), step);
}

int GridEditorComponent::quantizedTickAtX(int x, bool snapToGrid) const
{
    const int tick = tickAtX(x);
    return snapToGrid ? snappedTickFromTick(tick) : tick;
}

bool GridEditorComponent::fillLaneRange(TrackType trackType, juce::Range<int> tickRange, int stepInterval)
{
    const int intervalTicks = juce::jmax(1, stepInterval) * ticksPerStep();
    const int totalTicks = juce::jmax(1, project.params.bars * 16 * ticksPerStep());
    const int startTick = juce::jlimit(0, totalTicks - 1, tickRange.getStart());
    const int endTick = juce::jlimit(startTick + 1, totalTicks, tickRange.getEnd());

    bool changed = false;
    for (int tick = startTick; tick < endTick; tick += intervalTicks)
        changed = placeDrawNoteAt(trackType, tick) || changed;

    if (changed)
    {
        emitTrackEdited(trackType);
        repaint();
    }

    return changed;
}

bool GridEditorComponent::placeDrawNoteAt(TrackType trackType, int tick)
{
    auto* track = findMutableTrack(trackType);
    if (track == nullptr)
        return false;

    const int bars = juce::jmax(1, project.params.bars);
    NoteEvent insertedNote;
    setNoteStartTick(insertedNote, tick, bars);

    for (const auto& existingNote : track->notes)
    {
        if (existingNote.step == insertedNote.step
            && std::abs(existingNote.microOffset - insertedNote.microOffset) <= snapTicksForCurrentResolution() / 2)
            return false;
    }

    insertedNote.length = juce::jmax(1, ticksForGridResolution() / juce::jmax(1, ticksPerStep()));
    insertedNote.velocity = 100;

    track->notes.push_back(insertedNote);
    std::sort(track->notes.begin(), track->notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.microOffset < b.microOffset;
    });

    clearSelectionInternal();
    return true;
}

juce::Rectangle<int> GridEditorComponent::noteBounds(const NoteEvent& note, int row) const
{
    const float microStep = static_cast<float>(note.microOffset) / static_cast<float>(ticksPerStep());
    const float microShiftPx = juce::jlimit(-0.98f * stepWidth, 0.98f * stepWidth, microStep * stepWidth);
    const float x = static_cast<float>(note.step) * stepWidth + microShiftPx;
    const float y = static_cast<float>(rulerHeight + row * laneHeight);

    // In sub-step grids (e.g. 1/6 step), draw and hit-test notes closer to a single cell width.
    const float subStepWidth = stepWidth * (static_cast<float>(ticksForGridResolution())
                                           / static_cast<float>(juce::jmax(1, ticksPerStep())));
    float w = stepWidth * static_cast<float>(std::max(1, note.length));
    if (note.length <= 1 && ticksForGridResolution() < ticksPerStep())
        w = juce::jmax(2.0f, subStepWidth - 1.0f);

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
        case GridResolution::OneSixthStep: return juce::jmax(1, ticksPerStep() / 6);
        case GridResolution::OneQuarterStep: return juce::jmax(1, ticksPerStep() / 4);
        case GridResolution::OneThirdStep: return juce::jmax(1, ticksPerStep() / 3);
        case GridResolution::OneHalfStep: return juce::jmax(1, ticksPerStep() / 2);
        case GridResolution::Step: return ticksPerStep();
        case GridResolution::OneSixthBeat: return juce::jmax(1, ticksPerStep() * 4 / 6);
        case GridResolution::OneQuarterBeat: return ticksPerStep();
        case GridResolution::OneThirdBeat: return juce::jmax(1, ticksPerStep() * 4 / 3);
        case GridResolution::OneHalfBeat: return ticksPerStep() * 2;
        case GridResolution::Beat: return ticksPerStep() * 4;
        case GridResolution::Bar: return ticksPerStep() * 16;
        case GridResolution::OneEighth: return ticksPerStep() * 2; // 1/8
        case GridResolution::OneSixteenth: return ticksPerStep(); // 1/16
        case GridResolution::OneThirtySecond: return ticksPerStep() / 2; // 1/32
        case GridResolution::OneSixtyFourth: return ticksPerStep() / 4; // 1/64
        case GridResolution::Triplet: return (ticksPerStep() * 2) / 3; // 1/24
        default: return ticksPerStep();
    }
}
} // namespace bbg
