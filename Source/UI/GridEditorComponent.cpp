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
int makeStrokeCellKey(const RuntimeLaneId& laneId, int tick)
{
    return static_cast<int>(std::hash<juce::String>{}(laneId) & 0x0fffffff)
        ^ ((tick & 0x0fffff) << 2);
}

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

juce::String normalizedSemanticRole(const juce::String& role)
{
    return role.trim().toLowerCase();
}

juce::Colour semanticRoleColour(const juce::String& role)
{
    const auto normalized = normalizedSemanticRole(role);
    if (normalized == "anchor")
        return juce::Colour::fromRGB(255, 214, 102);
    if (normalized == "support")
        return juce::Colour::fromRGB(124, 196, 255);
    if (normalized == "fill")
        return juce::Colour::fromRGB(130, 222, 172);
    if (normalized == "accent")
        return juce::Colour::fromRGB(236, 142, 255);
    return juce::Colour::fromRGB(210, 218, 232);
}

juce::String semanticRoleBadge(const juce::String& role)
{
    const auto normalized = normalizedSemanticRole(role);
    if (normalized == "anchor")
        return "A";
    if (normalized == "support")
        return "S";
    if (normalized == "fill")
        return "F";
    if (normalized == "accent")
        return "X";
    if (normalized.isNotEmpty())
        return normalized.substring(0, 1).toUpperCase();
    return {};
}
}

void GridEditorComponent::setProject(const PatternProject& value)
{
    project = value;
    if (selectedTrack.isEmpty() && !project.tracks.empty())
        selectedTrack = project.tracks.front().laneId;
    previewStartStep = std::max(0, project.previewStartStep);
    previewStartTick = previewStartStep * ticksPerStep();
    normalizeSelection();
    refreshEditorRegionState();
    invalidateStaticCache();
    notifyGridModeDisplayChanged();
    repaint();
}

void GridEditorComponent::setStepWidth(float width)
{
    const float next = juce::jlimit(4.0f, 160.0f, width);
    if (std::abs(next - stepWidth) < 0.001f)
        return;

    stepWidth = next;
    invalidateStaticCache();
    notifyGridModeDisplayChanged();
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
    notifyGridModeDisplayChanged();
    repaint();
}

void GridEditorComponent::setLaneDisplayOrder(const std::vector<RuntimeLaneId>& order)
{
    laneDisplayOrder = order;
    invalidateStaticCache();
    repaint();
}

void GridEditorComponent::setSelectedTrack(const RuntimeLaneId& laneId)
{
    if (selectedTrack == laneId)
        return;

    selectedTrack = laneId;
    refreshEditorRegionState();
    invalidateStaticCache();
    repaint();
}

void GridEditorComponent::notifyTrackFocused(const RuntimeLaneId& laneId)
{
    if (onTrackFocused)
        onTrackFocused(laneId);
}

int GridEditorComponent::velocityFromY(int y, const RuntimeLaneId& laneId) const
{
    const int row = visibleLaneIndex(laneId);
    if (row < 0)
        return 100;

    const int laneTop = rulerHeight + row * laneHeight;
    const int laneBottom = laneTop + laneHeight - 1;
    const float normalized = 1.0f - (static_cast<float>(juce::jlimit(laneTop, laneBottom, y) - laneTop)
        / static_cast<float>(juce::jmax(1, laneHeight - 1)));
    return juce::jlimit(1, 127, static_cast<int>(std::round(1.0f + normalized * 126.0f)));
}

bool GridEditorComponent::beginVelocityEditGesture(const SelectedNoteRef& ref, juce::Point<int> position)
{
    const auto trackIt = std::find_if(project.tracks.begin(), project.tracks.end(), [&ref](const TrackState& track)
    {
        return track.laneId == ref.laneId;
    });

    if (trackIt == project.tracks.end() || ref.index < 0 || ref.index >= static_cast<int>(trackIt->notes.size()))
        return false;

    if (!isSelected(ref))
        setSingleSelection(ref);
    else
        primarySelectedNote = ref;

    dragStartX = position.x;
    dragStartY = position.y;
    dragOriginalVelocity = trackIt->notes[static_cast<size_t>(ref.index)].velocity;
    collectDragSnapshots();
    editMode = EditMode::Velocity;
    velocityWaveModeActive = false;
    velocityOverlayDelta = 0;

    const bool changed = applySelectionVelocityAbsolute(velocityFromY(position.y, ref.laneId));
    movedDuringDrag = changed;

    repaint();
    return true;
}

void GridEditorComponent::refreshEditorRegionState()
{
    EditorRegionState nextState;
    nextState.loopTickRange = loopRegionTicks;
    nextState.previewStartTick = previewStartTick;
    nextState.primaryLaneId = selectedTrack;

    int minTick = std::numeric_limits<int>::max();
    int maxTick = std::numeric_limits<int>::min();
    std::set<RuntimeLaneId> activeLanes;

    for (const auto& ref : selectedNotes)
    {
        const auto* track = findTrack(ref.laneId);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;

        const auto& note = track->notes[static_cast<size_t>(ref.index)];
        minTick = juce::jmin(minTick, noteStartTick(note));
        maxTick = juce::jmax(maxTick, noteEndTick(note));
        activeLanes.insert(ref.laneId);
    }

    if (maxTick > minTick)
        nextState.selectedTickRange = juce::Range<int>(minTick, maxTick);

    if (activeLanes.empty())
    {
        if (selectedTrack.isNotEmpty())
            nextState.activeLaneIds.push_back(selectedTrack);
    }
    else
    {
        nextState.activeLaneIds.assign(activeLanes.begin(), activeLanes.end());
    }

    editorRegionState = std::move(nextState);
    if (onEditorRegionStateChanged)
        onEditorRegionStateChanged(editorRegionState);
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
    strokeMode = StrokeMode::None;
    strokeEditedLanes.clear();
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
    refreshEditorRegionState();
    repaint();
}

std::optional<juce::Range<int>> GridEditorComponent::getLoopRegion() const
{
    return loopRegionTicks;
}

void GridEditorComponent::refreshTransientInputState()
{
    applyCursorForHover();
    refreshEditorRegionState();
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

    std::set<RuntimeLaneId> changedTracks;
    const bool changed = GridEditActions::removeNotes(makeModelContext(), selectedNotes, &changedTracks);

    clearSelectionInternal();
    for (const auto t : changedTracks)
        emitTrackEdited(t);
    repaint();
    return changed;
}

bool GridEditorComponent::copySelectionToClipboard()
{
    return copySelectionInternal(false);
}

bool GridEditorComponent::cutSelectionToClipboard()
{
    return copySelectionInternal(true);
}

int GridEditorComponent::currentPasteAnchorTick() const
{
    if (hoverDrawTick >= 0)
        return snappedTickFromTick(hoverDrawTick);

    if (hoverNote.has_value())
    {
        const auto* track = findTrack(hoverNote->laneId);
        if (track != nullptr && hoverNote->index >= 0 && hoverNote->index < static_cast<int>(track->notes.size()))
            return snappedTickFromTick(noteStartTick(track->notes[static_cast<size_t>(hoverNote->index)]));
    }

    return snappedTickFromTick(previewStartTick);
}

bool GridEditorComponent::pasteClipboardAtTick(int anchorTick,
                                              std::vector<SelectedNoteRef>* insertedSelectionOut,
                                              std::set<RuntimeLaneId>* changedTracksOut,
                                              bool clearSelectionBeforeInsert)
{
    if (clipboardNotes.empty())
        return false;

    if (clearSelectionBeforeInsert)
        clearSelectionInternal();

    std::set<RuntimeLaneId> changedTracks;
    std::vector<SelectedNoteRef> insertedSelection;
    const bool changed = GridEditActions::pasteNotes(makeModelContext(), clipboardNotes, anchorTick, &insertedSelection, &changedTracks);
    if (!changed)
        return false;

    selectedNotes = insertedSelection;
    if (!selectedNotes.empty())
        primarySelectedNote = selectedNotes.front();

    for (const auto t : changedTracks)
        emitTrackEdited(t);

    if (insertedSelectionOut != nullptr)
        *insertedSelectionOut = insertedSelection;
    if (changedTracksOut != nullptr)
        *changedTracksOut = changedTracks;

    refreshEditorRegionState();
    repaint();
    return true;
}

bool GridEditorComponent::pasteClipboardAtPlayhead()
{
    if (clipboardNotes.empty())
        return false;

    return pasteClipboardAtTick(currentPasteAnchorTick());
}

bool GridEditorComponent::duplicateSelectionToRight()
{
    return duplicateSelectionRepeated(1);
}

bool GridEditorComponent::duplicateSelectionRepeated(int repeatCount)
{
    if (repeatCount <= 0)
        return false;

    if (!copySelectionInternal(false))
        return false;

    std::vector<SelectedNoteRef> allInserted;
    bool changed = false;

    for (int repeatIndex = 1; repeatIndex <= repeatCount; ++repeatIndex)
    {
        const int insertTick = snappedTickFromTick(clipboardSourceStartTick + clipboardSpanTicks * repeatIndex);
        std::vector<SelectedNoteRef> insertedSelection;
        if (!pasteClipboardAtTick(insertTick, &insertedSelection, nullptr, repeatIndex == 1))
            continue;

        allInserted.insert(allInserted.end(), insertedSelection.begin(), insertedSelection.end());
        changed = true;
    }

    if (!changed)
        return false;

    selectedNotes = std::move(allInserted);
    if (!selectedNotes.empty())
        primarySelectedNote = selectedNotes.back();

    const int finalInsertTick = snappedTickFromTick(clipboardSourceStartTick + clipboardSpanTicks * repeatCount);
    previewStartTick = finalInsertTick;
    previewStartStep = juce::jmax(0, finalInsertTick / ticksPerStep());
    refreshEditorRegionState();
    repaint();
    return true;
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
        if (!isVisibleLane(track.laneId))
            continue;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
            selectedNotes.push_back({ track.laneId, i });
    }

    if (!selectedNotes.empty())
        primarySelectedNote = selectedNotes.front();

    refreshEditorRegionState();
    repaint();
    return !selectedNotes.empty();
}

bool GridEditorComponent::selectAllNotesInLane(const RuntimeLaneId& laneId)
{
    auto* track = findMutableTrack(laneId);
    if (track == nullptr)
        return false;

    clearSelectionInternal();
    for (int i = 0; i < static_cast<int>(track->notes.size()); ++i)
        selectedNotes.push_back({ laneId, i });

    if (!selectedNotes.empty())
        primarySelectedNote = selectedNotes.front();

    refreshEditorRegionState();
    repaint();
    return !selectedNotes.empty();
}

bool GridEditorComponent::setSelectionSemanticRole(const juce::String& role)
{
    normalizeSelection();
    if (selectedNotes.empty())
        return false;

    const auto normalizedRole = normalizedSemanticRole(role);
    std::set<RuntimeLaneId> changedLaneIds;
    bool changed = false;

    for (const auto& ref : selectedNotes)
    {
        auto* track = findMutableTrack(ref.laneId);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& note = track->notes[static_cast<size_t>(ref.index)];
        if (normalizedSemanticRole(note.semanticRole) == normalizedRole)
            continue;

        note.semanticRole = normalizedRole;
        changedLaneIds.insert(ref.laneId);
        changed = true;
    }

    if (!changed)
        return false;

    for (const auto& laneId : changedLaneIds)
        emitTrackEdited(laneId);

    repaint();
    return true;
}

bool GridEditorComponent::selectNotesBySemanticRole(const juce::String& role)
{
    const auto normalizedRole = normalizedSemanticRole(role);
    if (normalizedRole.isEmpty())
        return false;

    selectedNotes.clear();
    primarySelectedNote.reset();

    for (const auto& track : project.tracks)
    {
        for (int index = 0; index < static_cast<int>(track.notes.size()); ++index)
        {
            if (normalizedSemanticRole(track.notes[static_cast<size_t>(index)].semanticRole) != normalizedRole)
                continue;

            selectedNotes.push_back({ track.laneId, index });
        }
    }

    normalizeSelection();
    repaint();
    return !selectedNotes.empty();
}

bool GridEditorComponent::nudgeSelectionBySnap(int direction)
{
    normalizeSelection();
    if (selectedNotes.empty() || direction == 0)
        return false;

    collectDragSnapshots();
    if (dragSnapshots.empty())
        return false;

    const int deltaTicks = keyboardNudgeTicks() * direction;
    const int deltaSteps = deltaTicks / ticksPerStep();
    const int deltaMicro = deltaTicks - deltaSteps * ticksPerStep();
    if (!applySelectionMoveDelta(deltaSteps, 0, deltaMicro))
        return false;

    std::set<RuntimeLaneId> changedLaneIds;
    for (const auto& ref : selectedNotes)
        changedLaneIds.insert(ref.laneId);
    for (const auto& laneId : changedLaneIds)
        emitTrackEdited(laneId);

    repaint();
    return true;
}

bool GridEditorComponent::nudgeSelectionMicro(int direction)
{
    normalizeSelection();
    if (selectedNotes.empty() || direction == 0)
        return false;

    collectDragSnapshots();
    if (!applySelectionMicroOffsetDelta(keyboardMicroNudgeTicks() * direction))
        return false;

    std::set<RuntimeLaneId> changedLaneIds;
    for (const auto& ref : selectedNotes)
        changedLaneIds.insert(ref.laneId);
    for (const auto& laneId : changedLaneIds)
        emitTrackEdited(laneId);

    repaint();
    return true;
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
        if (isVisibleLane(track.laneId))
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
    const auto velocityPreviewTrack = primarySelectedNote.has_value() ? primarySelectedNote->laneId : selectedTrack;

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
        const int row = visibleLaneIndex(track.laneId);
        if (row < 0)
            continue;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
        {
            const auto& note = track.notes[static_cast<size_t>(i)];
            const auto boundsInt = noteBounds(note, row);
            const auto noteRect = boundsInt.toFloat();
            const bool noteIsSelected = isSelected(SelectedNoteRef{ track.laneId, i });
            const bool renderVelocityWave = velocityPreviewActive && (track.laneId == velocityPreviewTrack || noteIsSelected);

            const float normVel = juce::jlimit(0.0f, 1.0f, static_cast<float>(note.velocity) / 127.0f);
            const auto color = note.isGhost
                ? juce::Colour::fromRGB(120, 176, 255).withAlpha(0.45f + 0.45f * normVel)
                : juce::Colour::fromRGB(255, 164, 74).withAlpha(0.4f + 0.5f * normVel);
            const auto fillColour = noteIsSelected
                ? color.brighter(0.28f).withAlpha(0.92f)
                : color;
            const auto outlineColour = noteIsSelected
                ? juce::Colour::fromRGB(255, 236, 178).withAlpha(0.98f)
                : color.brighter(0.35f).withAlpha(0.72f);

            if (renderVelocityWave)
            {
                const float laneBottom = static_cast<float>(rulerHeight + row * laneHeight + laneHeight - 3);
                const float waveHeight = juce::jmax(4.0f, normVel * static_cast<float>(laneHeight - 6));
                const auto waveRect = juce::Rectangle<float>(noteRect.getX(), laneBottom - waveHeight, noteRect.getWidth(), waveHeight);
                g.setColour(fillColour.withAlpha(noteIsSelected ? 0.94f : 0.8f));
                g.fillRoundedRectangle(waveRect, 2.0f);
                g.setColour(outlineColour.withAlpha(0.95f));
                g.drawLine(waveRect.getX(), waveRect.getY(), waveRect.getRight(), waveRect.getY(), 1.6f);
                g.setColour(noteIsSelected ? outlineColour : juce::Colour::fromRGBA(255, 255, 255, 22));
                g.drawRoundedRectangle(noteRect, 2.0f, noteIsSelected ? 1.2f : 0.8f);
            }
            else
            {
                g.setColour(fillColour);
                g.fillRoundedRectangle(noteRect, 2.0f);

                if (noteIsSelected)
                {
                    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
                    g.fillRoundedRectangle(noteRect.reduced(1.0f, 1.0f), 2.0f);
                }

                // Duration strip makes note length explicit even with dense patterns.
                g.setColour(outlineColour.withAlpha(0.92f));
                const float stripY = noteRect.getBottom() - 2.5f;
                g.drawLine(noteRect.getX() + 1.0f, stripY, noteRect.getRight() - 1.0f, stripY, 1.6f);

                if (normalizedSemanticRole(note.semanticRole) == "anchor")
                {
                    g.setColour(juce::Colour::fromRGBA(255, 234, 164, 212));
                    g.drawLine(noteRect.getX() + 1.0f, noteRect.getY() + 1.5f, noteRect.getRight() - 1.0f, noteRect.getY() + 1.5f, 1.8f);
                }

                if (note.length > 1 && noteRect.getWidth() > 14.0f)
                {
                    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 188));
                    g.setFont(juce::Font(juce::FontOptions(10.0f)));
                    g.drawText(juce::String(note.length),
                               noteRect.withTrimmedTop(1.0f).withTrimmedBottom(4.0f).toNearestInt(),
                               juce::Justification::centredRight,
                               false);
                }

                const auto capabilities = laneEditorCapabilitiesForLane(track.laneId);
                if (capabilities.rendersNotePitchInGrid && noteRect.getWidth() > 24.0f)
                {
                    g.setColour(juce::Colour::fromRGBA(206, 224, 255, 210));
                    g.setFont(juce::Font(juce::FontOptions(9.0f)));
                    g.drawText(juce::String(note.pitch),
                               noteRect.withTrimmedLeft(3.0f).withTrimmedBottom(4.0f).toNearestInt(),
                               juce::Justification::centredLeft,
                               false);
                }

                if (noteRect.getWidth() > 24.0f && noteRect.getHeight() > 11.0f)
                {
                    g.setColour(juce::Colour::fromRGBA(20, 24, 30, noteIsSelected ? 210 : 164));
                    g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
                    g.drawText(juce::String(note.velocity),
                               noteRect.withTrimmedLeft(3.0f).withTrimmedRight(3.0f).withTrimmedTop(2.0f).toNearestInt(),
                               juce::Justification::centredRight,
                               false);
                }

                g.setColour(outlineColour.withAlpha(noteIsSelected ? 0.98f : 0.62f));
                g.drawRoundedRectangle(noteRect, 2.0f, noteIsSelected ? 1.8f : 1.0f);
            }

            const bool isHovered = hoverNote.has_value()
                && hoverNote->laneId == track.laneId
                && hoverNote->index == i;

            const auto semanticRole = normalizedSemanticRole(note.semanticRole);
            if (semanticRole.isNotEmpty())
            {
                const auto badgeBounds = juce::Rectangle<float>(noteRect.getX() + 3.0f,
                                                               noteRect.getY() + 3.0f,
                                                               juce::jmax(10.0f, juce::jmin(16.0f, noteRect.getWidth() - 6.0f)),
                                                               10.0f);
                const auto badgeColour = semanticRoleColour(semanticRole);
                g.setColour(badgeColour.withAlpha(noteIsSelected ? 0.94f : 0.82f));
                g.fillRoundedRectangle(badgeBounds, 3.0f);
                g.setColour(juce::Colour::fromRGBA(18, 20, 24, 220));
                g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
                g.drawText(semanticRoleBadge(semanticRole), badgeBounds.toNearestInt(), juce::Justification::centred, false);
            }

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

    if ((editorTool == EditorTool::Pencil || editorTool == EditorTool::Brush)
        && editMode != EditMode::MoveNote
        && editMode != EditMode::StretchNote
        && editMode != EditMode::Velocity
        && editMode != EditMode::MicroOffset
        && hoverDrawTrack.has_value()
        && hoverDrawTick >= 0)
    {
        const int row = visibleLaneIndex(*hoverDrawTrack);
        if (row >= 0)
        {
            NoteEvent previewNote;
            previewNote.length = juce::jmax(1,
                                            static_cast<int>(std::ceil(static_cast<double>(ticksForGridResolution())
                                                                       / static_cast<double>(juce::jmax(1, ticksPerStep())))));
            setNoteStartTick(previewNote, hoverDrawTick, juce::jmax(1, project.params.bars));

            const bool occupied = hasNoteAtTick(*hoverDrawTrack, noteStartTick(previewNote));
            const auto previewRect = noteBounds(previewNote, row).toFloat();
            g.setColour(occupied ? juce::Colour::fromRGBA(255, 108, 108, 52)
                                 : juce::Colour::fromRGBA(142, 188, 255, 58));
            g.fillRoundedRectangle(previewRect, 2.0f);
            g.setColour(occupied ? juce::Colour::fromRGBA(255, 176, 176, 176)
                                 : juce::Colour::fromRGBA(230, 238, 255, 186));
            g.drawRoundedRectangle(previewRect, 2.0f, 1.0f);
        }
    }

    if (!selectedNotes.empty())
    {
        for (const auto& ref : selectedNotes)
        {
            for (const auto& track : project.tracks)
            {
                if (track.laneId != ref.laneId)
                    continue;
                if (ref.index < 0 || ref.index >= static_cast<int>(track.notes.size()))
                    continue;

                const int row = visibleLaneIndex(track.laneId);
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

    const bool showVelocityOverlay = editMode == EditMode::Velocity
        || velocityWaveModeActive
        || (isVelocityEditKeyDown() && (!selectedNotes.empty() || hoverNote.has_value()));
    if (showVelocityOverlay)
    {
        int overlayPercent = velocityOverlayPercent;
        int overlayDelta = velocityOverlayDelta;
        if (editMode != EditMode::Velocity && !velocityWaveModeActive && isVelocityEditKeyDown())
        {
            int velocitySum = 0;
            int velocityCount = 0;
            if (!selectedNotes.empty())
            {
                for (const auto& ref : selectedNotes)
                {
                    const auto trackIt = std::find_if(project.tracks.begin(), project.tracks.end(), [&ref](const TrackState& track)
                    {
                        return track.laneId == ref.laneId;
                    });
                    if (trackIt == project.tracks.end() || ref.index < 0 || ref.index >= static_cast<int>(trackIt->notes.size()))
                        continue;

                    velocitySum += trackIt->notes[static_cast<size_t>(ref.index)].velocity;
                    ++velocityCount;
                }
            }
            else if (hoverNote.has_value())
            {
                const auto trackIt = std::find_if(project.tracks.begin(), project.tracks.end(), [this](const TrackState& track)
                {
                    return hoverNote.has_value() && track.laneId == hoverNote->laneId;
                });
                if (trackIt != project.tracks.end()
                    && hoverNote->index >= 0
                    && hoverNote->index < static_cast<int>(trackIt->notes.size()))
                {
                    velocitySum = trackIt->notes[static_cast<size_t>(hoverNote->index)].velocity;
                    velocityCount = 1;
                }
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
                    std::set<RuntimeLaneId> changed;
                    for (const auto& ref : selectedNotes)
                        changed.insert(ref.laneId);
                    for (const auto trackType : changed)
                        emitTrackEdited(trackType);
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
                std::set<RuntimeLaneId> changed;
                for (const auto& ref : selectedNotes)
                    changed.insert(ref.laneId);
                for (const auto t : changed)
                    emitTrackEdited(t);
                repaint();
                return;
            }
        }

        const auto p = event.getPosition();
        const int laneIndex = makeGeometry().yToLane(p.y);
        const auto lane = trackForVisibleLaneIndex(laneIndex);
        if (lane.has_value())
        {
            if (auto hit = findNoteAt(p, *lane); hit.has_value())
            {
                if (auto* track = findMutableTrack(hit->laneId))
                {
                    if (hit->index >= 0 && hit->index < static_cast<int>(track->notes.size()))
                    {
                        auto& note = track->notes[static_cast<size_t>(hit->index)];
                        note.velocity = juce::jlimit(1, 127, note.velocity + delta);
                        setSingleSelection(*hit);
                        emitTrackEdited(hit->laneId);
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
            onHorizontalZoomGesture(wheel.deltaY, event.getPosition());
        return;
    }

    if (laneHeightZoomModifierDown)
    {
        if (onLaneHeightZoomGesture)
            onLaneHeightZoomGesture(wheel.deltaY, event.getPosition());
        return;
    }

    juce::Component::mouseWheelMove(event, wheel);
}

void GridEditorComponent::mouseDown(const juce::MouseEvent& event)
{
    editMode = EditMode::None;
    strokeMode = StrokeMode::None;
    strokeEditedLanes.clear();
    drawVisitedTicks.clear();
    eraseVisitedKeys.clear();
    lastDragPoint = event.getPosition();
    movedDuringDrag = false;

    const int bars = std::max(1, project.params.bars);
    const int totalSteps = bars * 16;
    if (totalSteps <= 0)
        return;

    const auto p = event.getPosition();

    const bool velocityEditActive = isVelocityEditKeyDown() && p.y >= rulerHeight;
    const int velocityLaneIndex = velocityEditActive ? makeGeometry().yToLane(p.y) : -1;
    const auto velocityTrack = velocityEditActive ? trackForVisibleLaneIndex(velocityLaneIndex) : std::optional<RuntimeLaneId>{};
    const auto velocityHit = (velocityEditActive && velocityTrack.has_value())
        ? findNoteAt(event.getPosition(), *velocityTrack)
        : std::optional<SelectedNoteRef>{};

    if (velocityEditActive)
    {
        if (velocityTrack.has_value())
        {
            selectedTrack = *velocityTrack;
            notifyTrackFocused(*velocityTrack);
        }

        if (event.mods.isRightButtonDown())
        {
            showNoteContextMenu(p, velocityTrack);
            return;
        }

        if (event.mods.isLeftButtonDown())
        {
            if (velocityHit.has_value())
            {
                beginVelocityEditGesture(*velocityHit, p);
                return;
            }

            repaint();
            return;
        }
    }

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
        const auto clickedTrack = trackForVisibleLaneIndex(laneIndex);
        if (!clickedTrack.has_value())
            return;

        selectedTrack = *clickedTrack;
        notifyTrackFocused(*clickedTrack);
        editMode = EditMode::EraseDrag;
        strokeMode = StrokeMode::Erase;
        applyEraseAtCell(*clickedTrack, quantizedTickAtX(p.x, true));
        repaint();
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

    const int laneIndex = makeGeometry().yToLane(p.y);
    const auto clickedTrack = trackForVisibleLaneIndex(laneIndex);
    if (!clickedTrack.has_value())
        return;
    selectedTrack = *clickedTrack;
    notifyTrackFocused(*clickedTrack);

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
        strokeMode = StrokeMode::Erase;
        applyEraseAtCell(*clickedTrack, quantizedTickAtX(p.x, true));
        repaint();
        return;
    }

    if (editorTool == EditorTool::Cut)
    {
        editMode = EditMode::Cut;
        if (hit.has_value())
        {
            const int cutTick = quantizedTickAtX(event.getPosition().x, true);
            if (splitNoteAtTick(hit->laneId, hit->index, cutTick))
            {
                emitTrackEdited(hit->laneId);
                repaint();
            }
        }
        return;
    }

    if (editorTool == EditorTool::Brush)
    {
        const int tick = quantizedTickAtX(p.x, true);
        editMode = EditMode::BrushDraw;
        strokeMode = StrokeMode::Draw;
        drawTrack = *clickedTrack;
        applyDrawAtCell(drawTrack, tick);
        repaint();
        return;
    }

    if (editorTool == EditorTool::Pencil)
    {
        const int tick = quantizedTickAtX(p.x, true);
        const bool shouldErase = hit.has_value() || hasNoteAtTick(*clickedTrack, tick);

        strokeMode = shouldErase ? StrokeMode::Erase : StrokeMode::Draw;
        editMode = shouldErase ? EditMode::EraseDrag : EditMode::Draw;

        if (shouldErase)
            applyEraseAtCell(*clickedTrack, tick);
        else
            applyDrawAtCell(*clickedTrack, tick);

        repaint();
        return;
    }

    if (editorTool == EditorTool::Select)
    {
        if (hit.has_value())
        {
            const bool toggle = event.mods.isShiftDown() || event.mods.isCommandDown() || event.mods.isCtrlDown();
            if (toggle)
            {
                toggleSelection(*hit);
                repaint();
                return;
            }

            if (!isSelected(*hit))
                addSelection(*hit);
            else
                primarySelectedNote = *hit;

            dragStartX = p.x;
            dragStartY = p.y;

            for (const auto& track : project.tracks)
            {
                if (track.laneId != hit->laneId)
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
            editMode = isStretchEditKeyDown() ? EditMode::StretchNote : EditMode::MoveNote;

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
        strokeMode = StrokeMode::Draw;
        drawTrack = *clickedTrack;
        applyDrawAtCell(drawTrack, tick);
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
            if (track.laneId != hit->laneId)
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

        editMode = isStretchEditKeyDown() ? EditMode::StretchNote : EditMode::MoveNote;

        repaint();
        return;
    }

    const int tick = quantizedTickAtX(p.x, true);
    const bool added = placeDrawNoteAt(*clickedTrack, tick);
    if (added)
        emitTrackEdited(*clickedTrack);
    repaint();
}

void GridEditorComponent::mouseDrag(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();

    if (rulerDraggingLoop)
    {
        const int currentTick = quantizedTickAtX(p.x, true);
        const int startTick = juce::jmin(rulerDragStartTick, currentTick);
        const int endTick = juce::jmax(rulerDragStartTick, currentTick);
        rulerLoopDragMoved = rulerLoopDragMoved || (std::abs(currentTick - rulerDragStartTick) >= snapThresholdTicks());

        if (rulerLoopDragMoved && endTick > startTick)
        {
            loopRegionTicks = juce::Range<int>(startTick, endTick);
            refreshEditorRegionState();
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

    if (editMode == EditMode::Draw)
    {
        const auto fixedLane = (editorTool == EditorTool::Pencil)
            ? std::optional<RuntimeLaneId>{}
            : std::optional<RuntimeLaneId>(drawTrack);
        applyStrokeAlongSegment(lastDragPoint, p, false, fixedLane);
        lastDragPoint = p;
        movedDuringDrag = true;
        repaint();
        return;
    }

    if (editMode == EditMode::BrushDraw)
    {
        const int tick = quantizedTickAtX(p.x, true);
        if (drawVisitedTicks.insert(makeStrokeCellKey(drawTrack, tick)).second && placeDrawNoteAt(drawTrack, tick))
            strokeEditedLanes.insert(drawTrack);
        lastDragPoint = p;
        movedDuringDrag = true;
        repaint();
        return;
    }

    if (editMode == EditMode::EraseDrag)
    {
        applyStrokeAlongSegment(lastDragPoint, p, true);
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
            const int row = visibleLaneIndex(track.laneId);
            if (row < 0)
                continue;
            for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
            {
                if (marqueeRect.intersects(noteBounds(track.notes[static_cast<size_t>(i)], row)))
                    picks.push_back({ track.laneId, i });
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
        const auto targetLane = primarySelectedNote.has_value() ? primarySelectedNote->laneId : selectedTrack;
        if (applySelectionVelocityAbsolute(velocityFromY(p.y, targetLane)))
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
    auto* track = findMutableTrack(ref.laneId);
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
    const int deltaSteps = deltaTicks / ticksPerStep();
    const int deltaMicro = deltaTicks - deltaSteps * ticksPerStep();
    const int deltaLanes = static_cast<int>(std::round(static_cast<float>(p.y - dragStartY) / static_cast<float>(juce::jmax(1, laneHeight))));
    applySelectionMoveDelta(deltaSteps, deltaLanes, deltaMicro);
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
            refreshEditorRegionState();
            if (onPlaybackFlagChanged)
                onPlaybackFlagChanged(step);
        }
        else if (loopRegionTicks.has_value() && loopRegionTicks->getLength() <= 0)
        {
            loopRegionTicks.reset();
            refreshEditorRegionState();
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

    if ((editMode == EditMode::MoveNote
        || editMode == EditMode::StretchNote
        || editMode == EditMode::Velocity
        || editMode == EditMode::MicroOffset)
        && movedDuringDrag)
    {
        std::set<RuntimeLaneId> changed;
        for (const auto& ref : selectedNotes)
            changed.insert(ref.laneId);
        if (changed.empty() && primarySelectedNote.has_value())
            changed.insert(primarySelectedNote->laneId);
        for (const auto t : changed)
            emitTrackEdited(t);
    }
    else if (editMode == EditMode::Draw || editMode == EditMode::BrushDraw || editMode == EditMode::EraseDrag)
    {
        for (const auto& laneId : strokeEditedLanes)
            emitTrackEdited(laneId);
    }

    if (editMode == EditMode::Marquee)
    {
        marqueeRect = {};
        marqueeBaseSelection.clear();
    }

    editMode = EditMode::None;
    strokeMode = StrokeMode::None;
    strokeEditedLanes.clear();
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
    hoverDrawTrack.reset();
    hoverDrawTick = -1;
    hoverZone = HoverZone::None;
    applyCursorForHover();
    repaint();
}

void GridEditorComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!event.mods.isMiddleButtonDown())
    {
        juce::Component::mouseDoubleClick(event);
        return;
    }

    const auto position = event.getPosition();
    if (position.y < rulerHeight)
    {
        if (onHorizontalZoomGesture)
            onHorizontalZoomGesture(2.5f, position);
        return;
    }

    if (onLaneHeightZoomGesture)
        onLaneHeightZoomGesture(1.5f, position);
}

std::optional<GridEditorComponent::SelectedNoteRef> GridEditorComponent::findClosestNoteAt(juce::Point<int> position,
                                                                                            const RuntimeLaneId& laneId,
                                                                                            int maxDistancePx) const
{
    for (const auto& track : project.tracks)
    {
        if (track.laneId != laneId)
            continue;

        int bestIndex = -1;
        int bestDistance = std::numeric_limits<int>::max();
        const int row = visibleLaneIndex(track.laneId);
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
            return SelectedNoteRef { laneId, bestIndex };

        break;
    }

    return std::nullopt;
}

LaneEditorCapabilities GridEditorComponent::laneEditorCapabilitiesForLane(const RuntimeLaneId& laneId) const
{
    if (const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, laneId))
        return lane->editorCapabilities;

    return {};
}

int GridEditorComponent::noteStartTick(const NoteEvent& note) const
{
    return GridEditActions::noteStartTick(note, ticksPerStep());
}

int GridEditorComponent::noteEndTick(const NoteEvent& note) const
{
    return GridEditActions::noteEndTick(note, ticksPerStep());
}

bool GridEditorComponent::eraseNote(const SelectedNoteRef& ref)
{
    std::set<RuntimeLaneId> changedLaneIds;
    if (!GridEditActions::removeNotes(makeModelContext(), { ref }, &changedLaneIds))
        return false;

    normalizeSelection();
    strokeEditedLanes.insert(ref.laneId);
    return true;
}

bool GridEditorComponent::eraseNoteAtCell(const RuntimeLaneId& laneId, int tick)
{
    if (auto hit = findNoteCoveringTick(laneId, tick); hit.has_value())
        return eraseNote(*hit);

    return false;
}

bool GridEditorComponent::setNoteStartTick(NoteEvent& note, int targetTick, int bars)
{
    return GridEditActions::setNoteStartTick(note, targetTick, bars, ticksPerStep());
}

bool GridEditorComponent::splitNoteAtTick(const RuntimeLaneId& laneId, int noteIndex, int cutTick)
{
    return GridEditActions::splitNote(makeModelContext(), laneId, noteIndex, cutTick);
}

bool GridEditorComponent::eraseNoteAt(juce::Point<int> position, std::optional<RuntimeLaneId> laneHint)
{
    std::optional<SelectedNoteRef> hit;
    if (laneHint.has_value())
        hit = findNoteAt(position, *laneHint);
    else
        hit = findNoteAt(position);

    if (!hit.has_value())
        return false;

    const auto* track = findTrack(hit->laneId);
    if (track == nullptr || hit->index < 0 || hit->index >= static_cast<int>(track->notes.size()))
        return false;

    const auto& note = track->notes[static_cast<size_t>(hit->index)];
    const int key = makeStrokeCellKey(hit->laneId, noteStartTick(note));
    if (!eraseVisitedKeys.insert(key).second)
        return false;

    return eraseNote(*hit);
}

bool GridEditorComponent::eraseNotesAlongSegment(juce::Point<int> from, juce::Point<int> to)
{
    return applyStrokeAlongSegment(from, to, true);
}

bool GridEditorComponent::applyDrawAtCell(const RuntimeLaneId& laneId, int tick)
{
    const int key = makeStrokeCellKey(laneId, tick);
    if (!drawVisitedTicks.insert(key).second)
        return false;

    if (!placeDrawNoteAt(laneId, tick))
        return false;

    strokeEditedLanes.insert(laneId);
    return true;
}

bool GridEditorComponent::applyEraseAtCell(const RuntimeLaneId& laneId, int tick)
{
    const int key = makeStrokeCellKey(laneId, tick);
    if (!eraseVisitedKeys.insert(key).second)
        return false;

    return eraseNoteAtCell(laneId, tick);
}

bool GridEditorComponent::applyStrokeAlongSegment(juce::Point<int> from, juce::Point<int> to, bool erase, std::optional<RuntimeLaneId> fixedLane)
{
    const auto laneIds = orderedVisibleLaneIds();
    if (laneIds.empty())
        return false;

    const int maxLaneIndex = static_cast<int>(laneIds.size()) - 1;
    const int snapTicks = juce::jmax(1, snapTicksForCurrentResolution());
    const int maxColumn = juce::jmax(0, (juce::jmax(1, project.params.bars * 16 * ticksPerStep()) - 1) / snapTicks);
    const auto clampPointToGrid = [this](juce::Point<int> point)
    {
        return juce::Point<int>(juce::jmax(0, point.x), juce::jlimit(rulerHeight, juce::jmax(rulerHeight, getHeight() - 1), point.y));
    };

    const auto startPoint = clampPointToGrid(from);
    const auto endPoint = clampPointToGrid(to);
    const int startColumn = juce::jlimit(0, maxColumn, quantizedTickAtX(startPoint.x, true) / snapTicks);
    const int endColumn = juce::jlimit(0, maxColumn, quantizedTickAtX(endPoint.x, true) / snapTicks);

    const int startLaneIndex = fixedLane.has_value()
        ? visibleLaneIndex(*fixedLane)
        : juce::jlimit(0, maxLaneIndex, makeGeometry().yToLane(startPoint.y));
    const int endLaneIndex = fixedLane.has_value()
        ? visibleLaneIndex(*fixedLane)
        : juce::jlimit(0, maxLaneIndex, makeGeometry().yToLane(endPoint.y));

    if (startLaneIndex < 0 || endLaneIndex < 0)
        return false;

    bool changed = false;
    int column = startColumn;
    int laneIndex = startLaneIndex;
    const int deltaColumn = std::abs(endColumn - startColumn);
    const int deltaLane = std::abs(endLaneIndex - startLaneIndex);
    const int stepColumn = (startColumn <= endColumn) ? 1 : -1;
    const int stepLane = (startLaneIndex <= endLaneIndex) ? 1 : -1;
    int error = deltaColumn - deltaLane;

    while (true)
    {
        const auto& laneId = laneIds[static_cast<size_t>(laneIndex)];
        const int tick = clampTickToProject(column * snapTicks);
        changed = (erase ? applyEraseAtCell(laneId, tick) : applyDrawAtCell(laneId, tick)) || changed;

        if (column == endColumn && laneIndex == endLaneIndex)
            break;

        const int doubledError = error * 2;
        if (doubledError > -deltaLane)
        {
            error -= deltaLane;
            column += stepColumn;
        }
        if (doubledError < deltaColumn)
        {
            error += deltaColumn;
            laneIndex += stepLane;
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

GridGeometry GridEditorComponent::makeGeometry() const
{
    return GridGeometry({ stepWidth,
                          laneHeight,
                          rulerHeight,
                          juce::jmax(1, project.params.bars),
                          ticksPerStep(),
                          visualSubdivisionTicks() });
}

GridEditActions::ModelContext GridEditorComponent::makeModelContext()
{
    return { project, ticksPerStep() };
}

GridEditActions::TimelineContext GridEditorComponent::makeTimelineContext()
{
    return { previewStartStep,
             previewStartTick,
             loopRegionTicks,
             ticksPerStep(),
             juce::jmax(1, project.params.bars * 16) };
}

int GridEditorComponent::resizeHandleWidthPx(const NoteEvent& note, int row) const
{
    const auto b = noteBounds(note, row);
    return juce::jlimit(9, 18, b.getWidth() / 4);
}

void GridEditorComponent::updateHoverState(juce::Point<int> position)
{
    hoverNote.reset();
    hoverDrawTrack.reset();
    hoverDrawTick = -1;
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
        if (editorTool == EditorTool::Pencil || editorTool == EditorTool::Brush)
        {
            hoverDrawTrack = *lane;
            hoverDrawTick = quantizedTickAtX(position.x, true);
        }

        applyCursorForHover();
        repaint();
        return;
    }

    hoverNote = hit;
    for (const auto& track : project.tracks)
    {
        if (track.laneId != hit->laneId)
            continue;
        if (hit->index < 0 || hit->index >= static_cast<int>(track.notes.size()))
            break;

        const int row = visibleLaneIndex(track.laneId);
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
        return s.laneId == ref.laneId && s.index == ref.index;
    });
}

void GridEditorComponent::clearSelectionInternal()
{
    selectedNotes.clear();
    primarySelectedNote.reset();
    refreshEditorRegionState();
}

void GridEditorComponent::addSelection(const SelectedNoteRef& ref)
{
    if (isSelected(ref))
    {
        primarySelectedNote = ref;
        refreshEditorRegionState();
        return;
    }

    selectedNotes.push_back(ref);
    primarySelectedNote = ref;
    refreshEditorRegionState();
}

void GridEditorComponent::setSingleSelection(const SelectedNoteRef& ref)
{
    selectedNotes.clear();
    selectedNotes.push_back(ref);
    primarySelectedNote = ref;
    refreshEditorRegionState();
}

void GridEditorComponent::toggleSelection(const SelectedNoteRef& ref)
{
    auto it = std::find_if(selectedNotes.begin(), selectedNotes.end(), [&ref](const SelectedNoteRef& s)
    {
        return s.laneId == ref.laneId && s.index == ref.index;
    });

    if (it != selectedNotes.end())
        selectedNotes.erase(it);
    else
        selectedNotes.push_back(ref);

    if (selectedNotes.empty())
        primarySelectedNote.reset();
    else
        primarySelectedNote = selectedNotes.back();

    refreshEditorRegionState();
}

void GridEditorComponent::normalizeSelection()
{
    std::vector<SelectedNoteRef> valid;
    valid.reserve(selectedNotes.size());

    for (const auto& ref : selectedNotes)
    {
        const auto trackIt = std::find_if(project.tracks.begin(), project.tracks.end(), [&ref](const TrackState& t)
        {
            return t.laneId == ref.laneId;
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
        refreshEditorRegionState();
        return;
    }

    if (!primarySelectedNote.has_value() || !isSelected(*primarySelectedNote))
        primarySelectedNote = selectedNotes.front();

    refreshEditorRegionState();
}

void GridEditorComponent::collectDragSnapshots()
{
    normalizeSelection();
    dragSnapshots.clear();
    dragSnapshots.reserve(selectedNotes.size());

    for (const auto& ref : selectedNotes)
    {
        auto* track = findMutableTrack(ref.laneId);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;

        const auto& n = track->notes[static_cast<size_t>(ref.index)];
        DragSnapshot snap;
        snap.laneId = ref.laneId;
        snap.index = ref.index;
        snap.sourceNote = n;
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
    if (dragSnapshots.empty())
        collectDragSnapshots();

    int velocitySum = 0;
    int velocityCount = 0;
    int startVelocitySum = 0;
    const bool changed = GridEditActions::changeVelocityDelta(makeModelContext(),
                                                              dragSnapshots,
                                                              deltaVel,
                                                              &velocitySum,
                                                              &startVelocitySum,
                                                              &velocityCount);

    if (velocityCount > 0)
    {
        velocityOverlayPercent = static_cast<int>(std::round((static_cast<float>(velocitySum) / static_cast<float>(velocityCount)) * 100.0f / 127.0f));
        velocityOverlayDelta = static_cast<int>(std::round(static_cast<float>(velocitySum - startVelocitySum) / static_cast<float>(velocityCount)));
    }

    return changed;
}

bool GridEditorComponent::applySelectionVelocityAbsolute(int targetVelocity)
{
    if (dragSnapshots.empty())
        collectDragSnapshots();

    int velocitySum = 0;
    int velocityCount = 0;
    int startVelocitySum = 0;
    const bool changed = GridEditActions::changeVelocityAbsolute(makeModelContext(),
                                                                 dragSnapshots,
                                                                 targetVelocity,
                                                                 &velocitySum,
                                                                 &startVelocitySum,
                                                                 &velocityCount);

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

    const int span = juce::jmax(1, maxMouseX - minMouseX);
    juce::ignoreUnused(span);
    int velocitySum = 0;
    int velocityCount = 0;
    int startVelocitySum = 0;
    const bool changed = GridEditActions::changeVelocityWave(makeModelContext(),
                                                             dragSnapshots,
                                                             deltaVel,
                                                             minMouseX,
                                                             maxMouseX,
                                                             currentMouseX,
                                                             stepWidth,
                                                             dragStartX,
                                                             &velocitySum,
                                                             &startVelocitySum,
                                                             &velocityCount);

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

    return GridEditActions::changeMicroOffset(makeModelContext(), dragSnapshots, deltaTicks);
}

bool GridEditorComponent::applySelectionMoveDelta(int deltaSteps, int deltaLanes, int deltaMicro)
{
    std::set<RuntimeLaneId> changedLaneIds;
    std::vector<SelectedNoteRef> movedSelection;
    const bool changed = GridEditActions::moveSelection(makeModelContext(),
                                                        selectedNotes,
                                                        dragSnapshots,
                                                        orderedVisibleLaneIds(),
                                                        deltaSteps,
                                                        deltaLanes,
                                                        deltaMicro,
                                                        &movedSelection,
                                                        &changedLaneIds);
    if (!changed)
        return false;

    selectedNotes = std::move(movedSelection);
    normalizeSelection();
    if (primarySelectedNote.has_value())
        selectedTrack = primarySelectedNote->laneId;
    return true;
}

bool GridEditorComponent::applySelectionStretchTicks(int deltaTicks, int bars)
{
    juce::ignoreUnused(bars);
    return GridEditActions::resizeSelection(makeModelContext(), dragSnapshots, deltaTicks);
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
        auto* track = findMutableTrack(ref.laneId);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;

        const auto& n = track->notes[static_cast<size_t>(ref.index)];
        const int start = noteStartTick(n);
        const int end = noteEndTick(n);
        minTick = juce::jmin(minTick, start);
        maxTick = juce::jmax(maxTick, end);

        clipboardNotes.push_back({ ref.laneId, n, 0 });
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
                                                                                 std::optional<RuntimeLaneId> laneHint,
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

void GridEditorComponent::showNoteContextMenu(juce::Point<int> p, std::optional<RuntimeLaneId> laneHint)
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
    constexpr int kActionRoleAnchor = 300;
    constexpr int kActionRoleSupport = 301;
    constexpr int kActionRoleFill = 302;
    constexpr int kActionRoleAccent = 303;
    constexpr int kActionRoleClear = 304;
    constexpr int kActionSelectRoleAnchor = 320;
    constexpr int kActionSelectRoleSupport = 321;
    constexpr int kActionSelectRoleFill = 322;
    constexpr int kActionSelectRoleAccent = 323;

    juce::PopupMenu fillMenu;
    fillMenu.addItem(kActionFillBase + 2, "Fill every 2");
    fillMenu.addItem(kActionFillBase + 4, "Fill every 4");
    fillMenu.addItem(kActionFillBase + 8, "Fill every 8");
    fillMenu.addItem(kActionFillBase + 16, "Fill every 16");

    juce::PopupMenu semanticRoleMenu;
    semanticRoleMenu.addItem(kActionRoleAnchor, "Anchor (protected)");
    semanticRoleMenu.addItem(kActionRoleSupport, "Support");
    semanticRoleMenu.addItem(kActionRoleFill, "Fill");
    semanticRoleMenu.addItem(kActionRoleAccent, "Accent");
    semanticRoleMenu.addSeparator();
    semanticRoleMenu.addItem(kActionRoleClear, "Clear role");

    juce::PopupMenu selectRoleMenu;
    selectRoleMenu.addItem(kActionSelectRoleAnchor, "Select Anchor");
    selectRoleMenu.addItem(kActionSelectRoleSupport, "Select Support");
    selectRoleMenu.addItem(kActionSelectRoleFill, "Select Fill");
    selectRoleMenu.addItem(kActionSelectRoleAccent, "Select Accent");

    if (target == ContextMenuTarget::Empty)
    {
        menu.addItem(kActionPaste, "Paste", !clipboardNotes.empty());
        if (laneHint.has_value())
            menu.addSubMenu("Fill Notes", fillMenu, true);
        menu.addSubMenu("Select By Role", selectRoleMenu, true);
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
        menu.addSubMenu("Semantic Role", semanticRoleMenu, true);
        menu.addSubMenu("Select By Role", selectRoleMenu, true);
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
        menu.addSubMenu("Semantic Role", semanticRoleMenu, true);
        menu.addSubMenu("Select By Role", selectRoleMenu, true);
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

bool GridEditorComponent::applyContextMenuAction(int actionId, std::optional<RuntimeLaneId> laneHint, juce::Point<int> p)
{
    constexpr int kActionRoleAnchor = 300;
    constexpr int kActionRoleSupport = 301;
    constexpr int kActionRoleFill = 302;
    constexpr int kActionRoleAccent = 303;
    constexpr int kActionRoleClear = 304;
    constexpr int kActionSelectRoleAnchor = 320;
    constexpr int kActionSelectRoleSupport = 321;
    constexpr int kActionSelectRoleFill = 322;
    constexpr int kActionSelectRoleAccent = 323;

    if (actionId >= 100)
    {
        if (actionId >= 200)
        {
            if (actionId >= 320)
            {
                if (actionId == kActionSelectRoleAnchor)
                    return selectNotesBySemanticRole("anchor");
                if (actionId == kActionSelectRoleSupport)
                    return selectNotesBySemanticRole("support");
                if (actionId == kActionSelectRoleFill)
                    return selectNotesBySemanticRole("fill");
                if (actionId == kActionSelectRoleAccent)
                    return selectNotesBySemanticRole("accent");
                return false;
            }

            if (actionId >= 300)
            {
                if (actionId == kActionRoleAnchor)
                    return setSelectionSemanticRole("anchor");
                if (actionId == kActionRoleSupport)
                    return setSelectionSemanticRole("support");
                if (actionId == kActionRoleFill)
                    return setSelectionSemanticRole("fill");
                if (actionId == kActionRoleAccent)
                    return setSelectionSemanticRole("accent");
                if (actionId == kActionRoleClear)
                    return setSelectionSemanticRole({});
                return false;
            }

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
            const bool changed = splitNoteAtTick(hit->laneId, hit->index, quantizedTickAtX(p.x, true));
            if (changed)
            {
                emitTrackEdited(hit->laneId);
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
            return track.laneId == ref.laneId;
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
                return track.laneId == ref.laneId;
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

    auto refs = selectedNotes;
    std::sort(refs.begin(), refs.end(), [](const SelectedNoteRef& a, const SelectedNoteRef& b)
    {
        if (a.laneId != b.laneId)
            return a.laneId > b.laneId;
        return a.index > b.index;
    });

    std::set<RuntimeLaneId> changedTracks;
    std::vector<SelectedNoteRef> insertedSelection;
    const bool changed = GridEditActions::rollSelection(makeModelContext(), refs, divisions, &insertedSelection, &changedTracks);

    clearSelectionInternal();
    selectedNotes = insertedSelection;
    if (!selectedNotes.empty())
        primarySelectedNote = selectedNotes.front();

    for (const auto trackType : changedTracks)
        emitTrackEdited(trackType);
    repaint();
    return changed;
}

int GridEditorComponent::clampTickToProject(int tick) const
{
    return makeGeometry().clampTick(tick);
}

int GridEditorComponent::quantizeTick(int tick) const
{
    if (!isSnapEnabled())
        return tick;

    const int snap = juce::jmax(1, snapTicksForCurrentResolution());
    return static_cast<int>(std::round(static_cast<float>(tick) / static_cast<float>(snap))) * snap;
}

int GridEditorComponent::tickAtX(int x) const
{
    return makeGeometry().xToTick(x);
}

int GridEditorComponent::snappedTickFromTick(int tick) const
{
    return clampTickToProject(isSnapEnabled() ? quantizeTick(tick) : tick);
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

bool GridEditorComponent::isVisibleLane(const RuntimeLaneId& laneId) const
{
    const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, laneId);
    return lane != nullptr && lane->isVisibleInEditor;
}

std::vector<RuntimeLaneId> GridEditorComponent::orderedVisibleLaneIds() const
{
    std::vector<RuntimeLaneId> ordered;
    ordered.reserve(project.tracks.size());

    for (const auto& lane : laneDisplayOrder)
        if (isVisibleLane(lane))
            ordered.push_back(lane);

    for (const auto& track : project.tracks)
        if (isVisibleLane(track.laneId)
            && std::find(ordered.begin(), ordered.end(), track.laneId) == ordered.end())
            ordered.push_back(track.laneId);

    return ordered;
}

int GridEditorComponent::visibleLaneIndex(const RuntimeLaneId& laneId) const
{
    const auto ordered = orderedVisibleLaneIds();

    for (int index = 0; index < static_cast<int>(ordered.size()); ++index)
    {
        if (ordered[static_cast<size_t>(index)] == laneId)
            return index;
    }

    return -1;
}

std::optional<RuntimeLaneId> GridEditorComponent::trackForVisibleLaneIndex(int laneIndex) const
{
    if (laneIndex < 0)
        return std::nullopt;

    const auto ordered = orderedVisibleLaneIds();

    if (laneIndex >= static_cast<int>(ordered.size()))
        return std::nullopt;

    return ordered[static_cast<size_t>(laneIndex)];
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

    for (int bar = 0; bar < bars; ++bar)
    {
        const int x = static_cast<int>(std::round(static_cast<float>(bar * 16) * stepWidth));
        const int width = static_cast<int>(std::round(stepWidth * 16.0f));
        const auto barArea = juce::Rectangle<int>(x, rulerHeight, juce::jmax(1, width), juce::jmax(1, area.getHeight() - rulerHeight));
        g.setColour((bar % 2 == 0) ? juce::Colour::fromRGBA(255, 255, 255, 4)
                                   : juce::Colour::fromRGBA(0, 0, 0, 10));
        g.fillRect(barArea);
    }

    for (int row = 0; row < tracksCount; ++row)
    {
        const auto rowArea = area.withTop(rulerHeight + row * laneHeight).withHeight(laneHeight);
        g.setColour((row % 2 == 0) ? juce::Colour::fromRGB(27, 30, 35)
                                   : juce::Colour::fromRGB(22, 25, 30));
        g.fillRect(rowArea);
    }

    const int selectedRow = visibleLaneIndex(selectedTrack);
    if (selectedRow >= 0)
    {
        g.setColour(juce::Colour::fromRGBA(90, 180, 255, 30));
        g.fillRect(area.withTop(rulerHeight + selectedRow * laneHeight).withHeight(laneHeight));
    }

    const int totalTicks = bars * 16 * ticksPerStep();
    const int beatTicks = ticksPerStep() * 4;
    const int visualTicks = juce::jmax(1, visualSubdivisionTicks());
    const int fineTicks = juce::jmax(1, visualFineSubdivisionTicks());

    for (int tick = 0; tick <= totalTicks; tick += fineTicks)
    {
        const float x = (static_cast<float>(tick) / static_cast<float>(ticksPerStep())) * stepWidth;

        if (tick % (ticksPerStep() * 16) == 0)
        {
            g.setColour(juce::Colour::fromRGB(148, 158, 178));
            g.fillRect(juce::Rectangle<float>(x - 1.0f, 0.0f, 2.0f, static_cast<float>(area.getHeight())));
            continue;
        }
        else if (tick % beatTicks == 0)
        {
            g.setColour(juce::Colour::fromRGBA(116, 128, 146, 204));
            g.fillRect(juce::Rectangle<float>(x, static_cast<float>(rulerHeight), 1.2f, static_cast<float>(area.getHeight() - rulerHeight)));
            g.setColour(juce::Colour::fromRGBA(206, 214, 228, 94));
            g.fillRect(juce::Rectangle<float>(x, 0.0f, 1.2f, static_cast<float>(rulerHeight)));
            continue;
        }
        else if (tick % visualTicks == 0)
            g.setColour(juce::Colour::fromRGBA(86, 94, 108, 160));
        else
            g.setColour(juce::Colour::fromRGBA(160, 172, 192, 16));

        g.drawVerticalLine(static_cast<int>(std::round(x)), static_cast<float>(rulerHeight), static_cast<float>(area.getBottom()));
    }

    for (int s = 0; s <= steps; ++s)
    {
        const float x = static_cast<float>(s) * stepWidth;
        if (s % 16 == 0)
        {
            const int barNumber = s / 16 + 1;
            g.setColour(juce::Colour::fromRGB(182, 189, 201));
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(juce::String(barNumber), static_cast<int>(x) + 4, 2, 20, rulerHeight - 4, juce::Justification::centredLeft);
        }
        else if ((s % 4 == 0) && stepWidth >= 12.0f)
        {
            g.setColour(juce::Colour::fromRGBA(218, 224, 235, 112));
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            g.drawText(juce::String((s / 4) % 4 + 1), static_cast<int>(x) - 6, 8, 12, rulerHeight - 10, juce::Justification::centred, false);
        }
    }

    g.setColour(juce::Colour::fromRGB(58, 64, 78));
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
        const int row = visibleLaneIndex(track.laneId);
        if (row < 0)
            continue;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
        {
            if (noteBounds(track.notes[static_cast<size_t>(i)], row).expanded(3, 3).contains(position))
                return SelectedNoteRef { track.laneId, i };
        }
    }

    return std::nullopt;
}

std::optional<GridEditorComponent::SelectedNoteRef> GridEditorComponent::findNoteAt(juce::Point<int> position, const RuntimeLaneId& laneId) const
{
    for (const auto& track : project.tracks)
    {
        if (track.laneId != laneId)
            continue;

        const int row = visibleLaneIndex(track.laneId);
        if (row < 0)
            return std::nullopt;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
        {
            if (noteBounds(track.notes[static_cast<size_t>(i)], row).expanded(3, 3).contains(position))
                return SelectedNoteRef { track.laneId, i };
        }

        break;
    }

    return std::nullopt;
}

std::optional<GridEditorComponent::SelectedNoteRef> GridEditorComponent::findNoteCoveringTick(const RuntimeLaneId& laneId, int tick) const
{
    for (const auto& track : project.tracks)
    {
        if (track.laneId != laneId)
            continue;

        for (int i = 0; i < static_cast<int>(track.notes.size()); ++i)
        {
            const auto& note = track.notes[static_cast<size_t>(i)];
            const int startTick = noteStartTick(note);
            const int endTick = noteEndTick(note);
            if (tick >= startTick && tick < endTick)
                return SelectedNoteRef { laneId, i };
        }

        break;
    }

    return std::nullopt;
}

TrackState* GridEditorComponent::findMutableTrack(const RuntimeLaneId& laneId)
{
    return GridEditActions::findMutableTrack(makeModelContext(), laneId);
}

const TrackState* GridEditorComponent::findTrack(const RuntimeLaneId& laneId) const
{
    return GridEditActions::findTrack(project, laneId);
}

bool GridEditorComponent::hasNoteAtTick(const RuntimeLaneId& laneId, int tick) const
{
    return findNoteCoveringTick(laneId, tick).has_value();
}

void GridEditorComponent::emitTrackEdited(const RuntimeLaneId& laneId)
{
    if (!onTrackNotesEdited)
        return;

    if (const auto* track = findTrack(laneId))
    {
        onTrackNotesEdited(laneId, track->notes);
        return;
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
    if (!snapToGrid || !isSnapEnabled())
        return tick;

    const int snap = juce::jmax(1, snapTicksForCurrentResolution());
    return clampTickToProject(static_cast<int>(std::round(static_cast<float>(tick) / static_cast<float>(snap))) * snap);
}

bool GridEditorComponent::fillLaneRange(const RuntimeLaneId& laneId, juce::Range<int> tickRange, int stepInterval)
{
    const int intervalTicks = juce::jmax(1, stepInterval) * ticksPerStep();
    const int totalTicks = juce::jmax(1, project.params.bars * 16 * ticksPerStep());
    const int startTick = juce::jlimit(0, totalTicks - 1, tickRange.getStart());
    const int endTick = juce::jlimit(startTick + 1, totalTicks, tickRange.getEnd());

    bool changed = false;
    for (int tick = startTick; tick < endTick; tick += intervalTicks)
        changed = placeDrawNoteAt(laneId, tick) || changed;

    if (changed)
    {
        emitTrackEdited(laneId);
        repaint();
    }

    return changed;
}

bool GridEditorComponent::placeDrawNoteAt(const RuntimeLaneId& laneId, int tick)
{
    auto* track = findMutableTrack(laneId);
    if (track == nullptr)
        return false;

    const int bars = juce::jmax(1, project.params.bars);
    NoteEvent insertedNote;
    insertedNote.length = juce::jmax(1,
                                     static_cast<int>(std::ceil(static_cast<double>(defaultNoteLengthTicks())
                                                                / static_cast<double>(juce::jmax(1, ticksPerStep())))));
    insertedNote.velocity = 100;
    setNoteStartTick(insertedNote, tick, bars);

    if (hasNoteAtTick(laneId, noteStartTick(insertedNote)))
        return false;

    GridEditActions::addNote(makeModelContext(), laneId, insertedNote);

    clearSelectionInternal();
    return true;
}

juce::Rectangle<int> GridEditorComponent::noteBounds(const NoteEvent& note, int row) const
{
    return makeGeometry().noteRect(note, row);
}

int GridEditorComponent::snapTicksForCurrentResolution() const
{
    return effectiveSnapTicks();
}

int GridEditorComponent::ticksForGridResolution() const
{
    switch (gridResolution)
    {
        case GridResolution::OneQuarter: return ticksPerStep() * 4;
        case GridResolution::OneQuarterTriplet: return juce::jmax(1, (ticksPerStep() * 8) / 3);
        case GridResolution::OneEighth: return ticksPerStep() * 2;
        case GridResolution::OneEighthTriplet: return juce::jmax(1, (ticksPerStep() * 4) / 3);
        case GridResolution::OneSixteenth: return ticksPerStep();
        case GridResolution::OneSixteenthTriplet: return juce::jmax(1, (ticksPerStep() * 2) / 3);
        case GridResolution::OneThirtySecond: return juce::jmax(1, ticksPerStep() / 2);
        case GridResolution::OneThirtySecondTriplet: return juce::jmax(1, ticksPerStep() / 3);
        case GridResolution::OneSixtyFourth: return juce::jmax(1, ticksPerStep() / 4);
        case GridResolution::OneSixtyFourthTriplet: return juce::jmax(1, ticksPerStep() / 6);
        case GridResolution::Adaptive:
        case GridResolution::Micro:
        default: return ticksPerStep();
    }
}

bool GridEditorComponent::isSnapEnabled() const
{
    return gridResolution != GridResolution::Micro;
}

int GridEditorComponent::defaultNoteLengthTicks() const
{
    return isSnapEnabled() ? effectiveSnapTicks() : ticksPerStep();
}

int GridEditorComponent::effectiveSnapTicks() const
{
    if (!isSnapEnabled())
        return 1;

    if (gridResolution == GridResolution::Adaptive)
        return adaptiveSnapTicks();

    return juce::jmax(1, ticksForGridResolution());
}

int GridEditorComponent::adaptiveSnapTicks() const
{
    if (stepWidth >= 92.0f)
        return juce::jmax(1, ticksPerStep() / 4);
    if (stepWidth >= 64.0f)
        return juce::jmax(1, ticksPerStep() / 3);
    if (stepWidth >= 40.0f)
        return juce::jmax(1, ticksPerStep() / 2);
    if (stepWidth >= 22.0f)
        return ticksPerStep();
    if (stepWidth >= 12.0f)
        return ticksPerStep() * 2;
    return ticksPerStep() * 4;
}

int GridEditorComponent::keyboardNudgeTicks() const
{
    return isSnapEnabled() ? effectiveSnapTicks() : ticksPerStep();
}

int GridEditorComponent::keyboardMicroNudgeTicks() const
{
    return juce::jmax(1, ticksPerStep() / 8);
}

int GridEditorComponent::visualSubdivisionTicks() const
{
    if (stepWidth >= 112.0f)
        return juce::jmax(1, ticksPerStep() / 6);
    if (stepWidth >= 84.0f)
        return juce::jmax(1, ticksPerStep() / 4);
    if (stepWidth >= 56.0f)
        return juce::jmax(1, ticksPerStep() / 3);
    if (stepWidth >= 34.0f)
        return juce::jmax(1, ticksPerStep() / 2);
    if (stepWidth >= 18.0f)
        return ticksPerStep();
    if (stepWidth >= 10.0f)
        return ticksPerStep() * 2;
    return ticksPerStep() * 4;
}

int GridEditorComponent::visualFineSubdivisionTicks() const
{
    const int coarse = juce::jmax(1, visualSubdivisionTicks());
    if (coarse >= ticksPerStep() * 4)
        return ticksPerStep();
    if (coarse >= ticksPerStep() * 2)
        return ticksPerStep();
    if (coarse >= ticksPerStep())
        return juce::jmax(1, ticksPerStep() / 2);
    return coarse;
}

int GridEditorComponent::snapThresholdTicks() const
{
    return isSnapEnabled() ? effectiveSnapTicks() : visualSubdivisionTicks();
}

juce::String GridEditorComponent::effectiveSnapLabel() const
{
    const int snap = effectiveSnapTicks();
    if (!isSnapEnabled())
        return "Micro";
    if (snap == ticksPerStep() * 4)
        return "1/4";
    if (snap == juce::jmax(1, (ticksPerStep() * 8) / 3))
        return "1/4T";
    if (snap == ticksPerStep() * 2)
        return "1/8";
    if (snap == juce::jmax(1, (ticksPerStep() * 4) / 3))
        return "1/8T";
    if (snap == ticksPerStep())
        return "1/16";
    if (snap == juce::jmax(1, (ticksPerStep() * 2) / 3))
        return "1/16T";
    if (snap == juce::jmax(1, ticksPerStep() / 2))
        return "1/32";
    if (snap == juce::jmax(1, ticksPerStep() / 3))
        return "1/32T";
    if (snap == juce::jmax(1, ticksPerStep() / 4))
        return "1/64";
    if (snap == juce::jmax(1, ticksPerStep() / 6))
        return "1/64T";
    return "1/16";
}

juce::String GridEditorComponent::currentGridModeLabel() const
{
    if (gridResolution == GridResolution::Adaptive)
        return "Snap: Adaptive (" + effectiveSnapLabel() + ")";
    if (gridResolution == GridResolution::Micro)
        return "Snap: Micro";
    return "Snap: " + effectiveSnapLabel();
}

void GridEditorComponent::notifyGridModeDisplayChanged()
{
    if (onGridModeDisplayChanged)
        onGridModeDisplayChanged(currentGridModeLabel());
}
} // namespace bbg
