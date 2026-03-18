#include "Sub808PianoRollComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

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

Sub808PianoRollComponent::Sub808PianoRollComponent()
{
    updateMouseCursor();
}

void Sub808PianoRollComponent::setEditorTool(GridEditorComponent::EditorTool tool)
{
    if (editorTool == tool)
        return;

    editorTool = tool;
    editMode = EditMode::None;
    drawVisitedKeys.clear();
    eraseVisitedKeys.clear();
    updateMouseCursor();
    repaint();
}

void Sub808PianoRollComponent::setInputBindings(const GridEditorComponent::InputBindings& bindings)
{
    inputBindings = bindings;
}

void Sub808PianoRollComponent::refreshTransientInputState()
{
    updateMouseCursor();
    repaint();
}

bool Sub808PianoRollComponent::deleteSelectedNotes()
{
    if (selectedIndices.empty())
        return false;

    std::sort(selectedIndices.begin(), selectedIndices.end(), std::greater<int>());
    selectedIndices.erase(std::unique(selectedIndices.begin(), selectedIndices.end()), selectedIndices.end());

    bool changed = false;
    for (const int idx : selectedIndices)
    {
        if (idx < 0 || idx >= static_cast<int>(notes.size()))
            continue;
        notes.erase(notes.begin() + idx);
        changed = true;
    }

    clearSelection();
    if (changed)
        commit();
    repaint();
    return changed;
}

bool Sub808PianoRollComponent::copySelectionToClipboard()
{
    return copySelectionInternal(false);
}

bool Sub808PianoRollComponent::cutSelectionToClipboard()
{
    return copySelectionInternal(true);
}

bool Sub808PianoRollComponent::pasteClipboardAtAnchor()
{
    if (clipboardNotes.empty())
        return false;

    const int anchorTick = snappedTickFromTick(pasteAnchorTick);
    clearSelection();
    std::vector<NoteEvent> insertedNotes;
    insertedNotes.reserve(clipboardNotes.size());

    for (const auto& item : clipboardNotes)
    {
        NoteEvent note = item.note;
        setNoteStartTick(note, juce::jlimit(0, totalTicks() - 1, anchorTick + item.relativeTick));
        notes.push_back(note);
        insertedNotes.push_back(note);
    }

    sortNotes();

    std::vector<bool> consumed(notes.size(), false);
    for (const auto& note : insertedNotes)
    {
        for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i)
        {
            const auto& current = notes[static_cast<size_t>(i)];
            if (consumed[static_cast<size_t>(i)])
                continue;

            if (noteStartTick(current) == noteStartTick(note)
                && current.pitch == note.pitch
                && current.length == note.length
                && current.velocity == note.velocity
                && current.microOffset == note.microOffset)
            {
                consumed[static_cast<size_t>(i)] = true;
                selectedIndices.push_back(i);
                activeNoteIndex = i;
                break;
            }
        }
    }

    commit();
    repaint();
    return true;
}

bool Sub808PianoRollComponent::duplicateSelectionToRight()
{
    if (!copySelectionInternal(false))
        return false;

    pasteAnchorTick = snappedTickFromTick(clipboardSourceStartTick + clipboardSpanTicks);
    return pasteClipboardAtAnchor();
}

void Sub808PianoRollComponent::clearNoteSelection()
{
    clearSelection();
    repaint();
}

bool Sub808PianoRollComponent::selectAllNotes()
{
    clearSelection();
    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
        selectedIndices.push_back(i);
    activeNoteIndex = selectedIndices.empty() ? -1 : selectedIndices.front();
    repaint();
    return !selectedIndices.empty();
}

void Sub808PianoRollComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 16, 20));

    const auto topBar = topBarBounds();
    const auto gridArea = noteGridBounds();
    const auto velocityArea = velocityLaneBounds();

    g.setColour(juce::Colour::fromRGB(20, 23, 28));
    g.fillRect(topBar);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 22));
    g.drawHorizontalLine(static_cast<float>(topBar.getBottom()), 0.0f, static_cast<float>(getWidth()));

    const int btnH = topBar.getHeight() - 8;
    const juce::Rectangle<int> octaveDownButton(4, 4, 24, btnH);
    const juce::Rectangle<int> octaveUpButton(30, 4, 24, btnH);
    const juce::Rectangle<int> octaveInfo(56, 4, 44, btnH);
    const juce::Rectangle<int> pencilBtn(108, 4, 46, btnH);
    const juce::Rectangle<int> brushBtn(156, 4, 52, btnH);
    const juce::Rectangle<int> selectBtn(210, 4, 46, btnH);
    const juce::Rectangle<int> cutBtn(258, 4, 42, btnH);
    const juce::Rectangle<int> eraseBtn(302, 4, 50, btnH);
    const juce::Rectangle<int> hotkeysBtn(topBar.getRight() - 184, topBar.getY() + 4, 36, btnH);
    const juce::Rectangle<int> snapButton(topBar.getRight() - 146, topBar.getY() + 4, 140, btnH);

    const auto colActive = juce::Colour::fromRGB(68, 110, 200);
    const auto colNormal = juce::Colour::fromRGB(44, 52, 64);

    // Oct buttons
    g.setColour(colNormal);
    g.fillRoundedRectangle(octaveDownButton.toFloat(), 3.0f);
    g.fillRoundedRectangle(octaveUpButton.toFloat(), 3.0f);

    // Tool buttons — active one is highlighted in blue
    g.setColour(editorTool == GridEditorComponent::EditorTool::Pencil ? colActive : colNormal);
    g.fillRoundedRectangle(pencilBtn.toFloat(), 3.0f);
    g.setColour(editorTool == GridEditorComponent::EditorTool::Brush ? colActive : colNormal);
    g.fillRoundedRectangle(brushBtn.toFloat(), 3.0f);
    g.setColour(editorTool == GridEditorComponent::EditorTool::Select ? colActive : colNormal);
    g.fillRoundedRectangle(selectBtn.toFloat(), 3.0f);
    g.setColour(editorTool == GridEditorComponent::EditorTool::Cut ? colActive : colNormal);
    g.fillRoundedRectangle(cutBtn.toFloat(), 3.0f);
    g.setColour(editorTool == GridEditorComponent::EditorTool::Erase ? colActive : colNormal);
    g.fillRoundedRectangle(eraseBtn.toFloat(), 3.0f);

    // Keys / Snap buttons
    g.setColour(colNormal);
    g.fillRoundedRectangle(hotkeysBtn.toFloat(), 3.0f);
    g.fillRoundedRectangle(snapButton.toFloat(), 4.0f);

    g.setColour(juce::Colour::fromRGB(165, 178, 198));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText("-", octaveDownButton, juce::Justification::centred, false);
    g.drawText("+", octaveUpButton, juce::Justification::centred, false);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("Oct " + juce::String(focusedOctave), octaveInfo, juce::Justification::centredLeft, false);
    g.drawText("Pencil", pencilBtn, juce::Justification::centred, false);
    g.drawText("Brush",  brushBtn,  juce::Justification::centred, false);
    g.drawText("Sel",   selectBtn,  juce::Justification::centred, false);
    g.drawText("Cut",   cutBtn,     juce::Justification::centred, false);
    g.drawText("Erase", eraseBtn,   juce::Justification::centred, false);
    g.drawText("Keys",  hotkeysBtn, juce::Justification::centred, false);
    g.drawText("Snap: " + snapModeLabel(), snapButton.reduced(8, 0), juce::Justification::centredLeft, false);

    g.setColour(juce::Colour::fromRGB(22, 25, 30));
    g.fillRect(0, gridArea.getY(), kKeyboardWidth, gridArea.getHeight());
    g.fillRect(0, velocityArea.getY(), kKeyboardWidth, velocityArea.getHeight());

    for (int i = 0; i < kPitchRows; ++i)
    {
        const int pitch = kMaxPitch - i;
        const bool black = (pitch % 12 == 1 || pitch % 12 == 3 || pitch % 12 == 6 || pitch % 12 == 8 || pitch % 12 == 10);
        const int y = gridArea.getY() + i * rowHeight;

        g.setColour(black ? juce::Colour::fromRGB(18, 21, 26) : juce::Colour::fromRGB(28, 32, 38));
        g.fillRect(kKeyboardWidth, y, gridArea.getWidth() - kKeyboardWidth, rowHeight);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 20));
        g.drawHorizontalLine(static_cast<float>(y), static_cast<float>(kKeyboardWidth), static_cast<float>(gridArea.getRight()));

        if ((pitch % 12) == 0)
        {
            g.setColour(juce::Colour::fromRGB(164, 174, 190));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("C" + juce::String((pitch / 12) - 1), 4, y, kKeyboardWidth - 8, rowHeight, juce::Justification::centredLeft, false);
        }
    }

    const int steps = totalSteps();
    for (int s = 0; s <= steps; ++s)
    {
        const int x = kKeyboardWidth + static_cast<int>(std::round(static_cast<float>(s) * stepWidth));
        if (s % 16 == 0)
            g.setColour(juce::Colour::fromRGB(98, 110, 130));
        else if (s % 4 == 0)
            g.setColour(juce::Colour::fromRGB(62, 70, 84));
        else
            g.setColour(juce::Colour::fromRGB(44, 50, 60));

        g.drawVerticalLine(x, static_cast<float>(gridArea.getY()), static_cast<float>(velocityArea.getBottom()));
    }

    g.setColour(juce::Colour::fromRGB(26, 29, 35));
    g.fillRect(velocityArea);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
    g.drawHorizontalLine(static_cast<float>(velocityArea.getY()), static_cast<float>(kKeyboardWidth), static_cast<float>(velocityArea.getRight()));
    g.setColour(juce::Colour::fromRGB(148, 160, 182));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("Velocity", 6, velocityArea.getY() + 4, kKeyboardWidth - 10, 14, juce::Justification::centredLeft, false);

    const bool velocityPreviewActive = editMode == EditMode::Velocity || velocityWaveModeActive || isVelocityEditKeyDown();

    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        const auto& n = notes[static_cast<size_t>(i)];
        const auto r = noteBounds(n).toFloat();
        const bool isSelected = isSelectedIndex(i);

        const float v = juce::jlimit(0.0f, 1.0f, static_cast<float>(n.velocity) / 127.0f);
        const auto base = juce::Colour::fromRGB(255, 164, 74).withAlpha(0.42f + 0.5f * v);

        if (velocityPreviewActive)
        {
            const float waveHeight = juce::jmax(4.0f, v * juce::jmax(6.0f, r.getHeight() - 2.0f));
            const auto waveRect = juce::Rectangle<float>(r.getX(), r.getBottom() - waveHeight, r.getWidth(), waveHeight);
            g.setColour(base.withAlpha(isSelected ? 0.92f : 0.78f));
            g.fillRoundedRectangle(waveRect, 2.0f);
            g.setColour(base.brighter(0.4f).withAlpha(0.95f));
            g.drawLine(waveRect.getX(), waveRect.getY(), waveRect.getRight(), waveRect.getY(), 1.5f);
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
            g.drawRoundedRectangle(r, 2.0f, 0.8f);
        }
        else
        {
            g.setColour(base);
            g.fillRoundedRectangle(r, 2.0f);

            g.setColour(base.brighter(0.35f).withAlpha(0.9f));
            g.drawLine(r.getX() + 1.0f, r.getBottom() - 2.0f, r.getRight() - 1.0f, r.getBottom() - 2.0f, 1.4f);

            g.setColour(juce::Colour::fromRGBA(210, 226, 255, 190));
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            g.drawText(juce::String(n.pitch), r.toNearestInt().reduced(4, 2), juce::Justification::centredLeft, false);
        }

        if (isSelected)
        {
            g.setColour(juce::Colour::fromRGB(255, 236, 178));
            g.drawRoundedRectangle(r.expanded(1.0f), 2.5f, 1.8f);
        }

        const auto vh = velocityHandleBounds(n).toFloat();
        g.setColour(base.withAlpha(isSelected ? 0.9f : 0.68f));
        g.fillRoundedRectangle(vh, 2.0f);
        if (isSelected)
        {
            g.setColour(juce::Colour::fromRGB(255, 236, 178));
            g.drawRoundedRectangle(vh.expanded(1.0f), 2.3f, 1.2f);
        }
    }

    if (editMode == EditMode::Marquee && !marqueeRect.isEmpty())
    {
        g.setColour(juce::Colour::fromRGBA(105, 177, 255, 42));
        g.fillRect(marqueeRect);
        g.setColour(juce::Colour::fromRGB(128, 198, 255));
        g.drawRect(marqueeRect, 1);
    }

    if ((editMode == EditMode::Velocity || velocityWaveModeActive || (isVelocityEditKeyDown() && !selectedIndices.empty())) && !selectedIndices.empty())
    {
        int overlayPercent = velocityOverlayPercent;
        int overlayDelta = velocityOverlayDelta;
        if (editMode != EditMode::Velocity && !velocityWaveModeActive && isVelocityEditKeyDown())
        {
            int velocitySum = 0;
            for (const int idx : selectedIndices)
            {
                if (idx < 0 || idx >= static_cast<int>(notes.size()))
                    continue;
                velocitySum += notes[static_cast<size_t>(idx)].velocity;
            }

            if (!selectedIndices.empty())
                overlayPercent = static_cast<int>(std::round((static_cast<float>(velocitySum) / static_cast<float>(selectedIndices.size())) * 100.0f / 127.0f));
            overlayDelta = 0;
        }

        const auto overlay = juce::Rectangle<int>(juce::jmax(8, getWidth() - 168), kTopBarHeight + 8, 156, 24);
        g.setColour(juce::Colour::fromRGBA(18, 22, 28, 230));
        g.fillRoundedRectangle(overlay.toFloat(), 5.0f);
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 28));
        g.drawRoundedRectangle(overlay.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colour::fromRGB(210, 222, 238));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        const juce::String label = velocityWaveModeActive ? "Velocity Wave: " : "Velocity: ";
        juce::String value = juce::String(overlayPercent) + "%";
        if (overlayDelta != 0)
            value += " (" + juce::String(overlayDelta > 0 ? "+" : "") + juce::String(overlayDelta) + ")";
        g.drawText(label + value,
                   overlay,
                   juce::Justification::centred,
                   false);
    }
}

void Sub808PianoRollComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();

    if (topBarBounds().contains(p))
    {
        constexpr int btnH = kTopBarHeight - 8;
        const juce::Rectangle<int> octaveDownButton(4, 4, 24, btnH);
        const juce::Rectangle<int> octaveUpButton(30, 4, 24, btnH);
        const juce::Rectangle<int> pencilBtn(108, 4, 46, btnH);
        const juce::Rectangle<int> brushBtn(156, 4, 52, btnH);
        const juce::Rectangle<int> selectBtn(210, 4, 46, btnH);
        const juce::Rectangle<int> cutBtn(258, 4, 42, btnH);
        const juce::Rectangle<int> eraseBtn(302, 4, 50, btnH);
        const juce::Rectangle<int> hotkeysBtn(topBarBounds().getRight() - 184, 4, 36, btnH);
        const juce::Rectangle<int> snapButton(topBarBounds().getRight() - 146, topBarBounds().getY() + 4, 140, btnH);

        auto applyTool = [this](GridEditorComponent::EditorTool tool)
        {
            setEditorTool(tool);
            if (onToolChanged)
                onToolChanged(tool);
        };

        if (octaveDownButton.contains(p)) { shiftFocusedOctave(-1); repaint(); return; }
        if (octaveUpButton.contains(p))   { shiftFocusedOctave(1);  repaint(); return; }
        if (pencilBtn.contains(p))        { applyTool(GridEditorComponent::EditorTool::Pencil); return; }
        if (brushBtn.contains(p))         { applyTool(GridEditorComponent::EditorTool::Brush); return; }
        if (selectBtn.contains(p))        { applyTool(GridEditorComponent::EditorTool::Select); return; }
        if (cutBtn.contains(p))           { applyTool(GridEditorComponent::EditorTool::Cut); return; }
        if (eraseBtn.contains(p))         { applyTool(GridEditorComponent::EditorTool::Erase); return; }
        if (hotkeysBtn.contains(p))
        {
            if (onOpenHotkeys) onOpenHotkeys();
            return;
        }
        if (snapButton.contains(p))
        {
            cycleSnapMode(!event.mods.isRightButtonDown());
            repaint();
            return;
        }
        return;
    }

    if (velocityLaneBounds().contains(p))
    {
        if (auto hit = findVelocityHandleAt(p); hit.has_value())
        {
            if (!isSelectedIndex(*hit))
                setSingleSelection(*hit);

            activeNoteIndex = *hit;
            velocityDragStartValue = notes[static_cast<size_t>(*hit)].velocity;
            dragStartX = p.x;
            dragStartY = p.y;
            collectDragSnapshots();
            editMode = EditMode::Velocity;
            velocityWaveModeActive = false;
            velocityOverlayPercent = notes[static_cast<size_t>(*hit)].velocity * 100 / 127;
            velocityOverlayDelta = 0;
            repaint();
        }
        return;
    }

    if (!noteGridBounds().contains(p) || p.x < kKeyboardWidth)
        return;

    lastDragPoint = p;
    pasteAnchorTick = tickAtX(p.x);
    drawVisitedKeys.clear();
    eraseVisitedKeys.clear();

    if (editorTool == GridEditorComponent::EditorTool::Erase && !event.mods.isRightButtonDown())
    {
        editMode = EditMode::EraseDrag;
        eraseNoteAt(p);
        repaint();
        return;
    }

    if (event.mods.isRightButtonDown())
    {
        showContextMenu(p);
        return;
    }

    if (editorTool == GridEditorComponent::EditorTool::Cut)
    {
        if (auto hit = findNoteAt(p); hit.has_value())
        {
            if (splitNoteAtTick(*hit, snappedTickAtX(p.x, event.mods.isAltDown())))
            {
                sortNotes();
                commit();
                repaint();
            }
        }
        return;
    }

    if (auto hit = findNoteAt(p); hit.has_value())
    {
        const bool additiveSelect = event.mods.isShiftDown() || event.mods.isCommandDown() || event.mods.isCtrlDown();
        if (additiveSelect)
        {
            if (isSelectedIndex(*hit))
                selectedIndices.erase(std::remove(selectedIndices.begin(), selectedIndices.end(), *hit), selectedIndices.end());
            else
                selectedIndices.push_back(*hit);

            activeNoteIndex = selectedIndices.empty() ? -1 : *hit;
        }
        else if (!isSelectedIndex(*hit))
        {
            setSingleSelection(*hit);
        }

        if (selectedIndices.empty())
        {
            editMode = EditMode::None;
            repaint();
            return;
        }

        dragStartX = p.x;
        dragStartY = p.y;
        collectDragSnapshots();
        activeNoteIndex = *hit;

        if (isVelocityEditKeyDown())
        {
            editMode = EditMode::Velocity;
            velocityWaveModeActive = false;
            velocityOverlayPercent = notes[static_cast<size_t>(*hit)].velocity * 100 / 127;
            velocityOverlayDelta = 0;
            repaint();
            return;
        }

        editMode = isStretchEditKeyDown() ? EditMode::Stretch : EditMode::Move;

        repaint();
        return;
    }

    if (editorTool == GridEditorComponent::EditorTool::Brush)
    {
        editMode = EditMode::BrushDraw;
        placeDrawNoteAt(p);
        repaint();
        return;
    }

    if (editorTool == GridEditorComponent::EditorTool::Pencil)
    {
        if (placeDrawNoteAt(p))
            commit();
        repaint();
        return;
    }

    const bool additiveSelect = event.mods.isShiftDown() || event.mods.isCommandDown() || event.mods.isCtrlDown();
    marqueeBaseSelection = selectedIndices;
    if (!additiveSelect)
        clearSelection();

    marqueeStart = p;
    marqueeRect = juce::Rectangle<int>(p.x, p.y, 0, 0);
    marqueeAdditive = additiveSelect;
    editMode = EditMode::Marquee;
    repaint();
}

void Sub808PianoRollComponent::mouseDrag(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();

    if (editMode == EditMode::BrushDraw)
    {
        placeDrawNoteAt(p);
        repaint();
        return;
    }

    if (editMode == EditMode::EraseDrag)
    {
        eraseNotesAlongSegment(lastDragPoint, p);
        lastDragPoint = p;
        repaint();
        return;
    }

    if (editMode == EditMode::Marquee)
    {
        marqueeRect = juce::Rectangle<int>::leftTopRightBottom(juce::jmin(marqueeStart.x, p.x),
                                                                juce::jmin(marqueeStart.y, p.y),
                                                                juce::jmax(marqueeStart.x, p.x),
                                                                juce::jmax(marqueeStart.y, p.y));
        marqueeRect = marqueeRect.getIntersection(noteGridBounds().withTrimmedLeft(kKeyboardWidth));
        setMarqueeSelection(marqueeRect, marqueeAdditive);
        repaint();
        return;
    }

    if (editMode == EditMode::Velocity)
    {
        const int delta = dragStartY - p.y;
        applySelectionVelocityWave(delta, p.x);
        repaint();
        return;
    }

    if (selectedIndices.empty())
        return;

    if (editMode == EditMode::Move)
    {
        const int dx = p.x - dragStartX;
        const int rawTickDelta = static_cast<int>(std::round(static_cast<float>(dx) * static_cast<float>(ticksPerStep()) / juce::jmax(1.0f, stepWidth)));
        const int tickDelta = quantizeTick(rawTickDelta, event.mods.isAltDown());
        const int pitchDelta = pitchAtY(p.y) - pitchAtY(dragStartY);

        for (const auto& snapshot : dragSnapshots)
        {
            if (snapshot.index < 0 || snapshot.index >= static_cast<int>(notes.size()))
                continue;

            auto& n = notes[static_cast<size_t>(snapshot.index)];
            setNoteStartTick(n, snapshot.startTick + tickDelta);
            n.pitch = juce::jlimit(kMinPitch, kMaxPitch, snapshot.startPitch + pitchDelta);
        }
        repaint();
        return;
    }

    if (editMode == EditMode::Stretch)
    {
        const int deltaX = p.x - dragStartX;
        const int rawDeltaTicks = static_cast<int>(std::round(static_cast<float>(deltaX) * static_cast<float>(ticksPerStep()) / juce::jmax(1.0f, stepWidth)));
        const int deltaTicks = juce::jmax(0, quantizeTick(rawDeltaTicks, event.mods.isAltDown()));
        for (const auto& snapshot : dragSnapshots)
        {
            if (snapshot.index < 0 || snapshot.index >= static_cast<int>(notes.size()))
                continue;

            auto& n = notes[static_cast<size_t>(snapshot.index)];
            const int startTick = snapshot.startTick;
            const int baseEndTick = startTick + snapshot.startLength * ticksPerStep();
            const int clampedEnd = juce::jlimit(startTick + ticksPerStep(), totalTicks(), baseEndTick + deltaTicks);
            n.length = juce::jmax(1,
                                  static_cast<int>(std::ceil(static_cast<double>(clampedEnd - startTick)
                                                             / static_cast<double>(ticksPerStep()))));
        }
        repaint();
    }
}

void Sub808PianoRollComponent::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (editMode != EditMode::None)
    {
        sortNotes();
        commit();
    }

    marqueeRect = {};
    marqueeBaseSelection.clear();
    dragSnapshots.clear();
    drawVisitedKeys.clear();
    eraseVisitedKeys.clear();
    velocityWaveModeActive = false;
    velocityOverlayDelta = 0;
    editMode = EditMode::None;
    updateMouseCursor();
    repaint();
}

void Sub808PianoRollComponent::mouseMove(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    updateMouseCursor();
}

void Sub808PianoRollComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (topBarBounds().contains(event.getPosition()))
    {
        cycleSnapMode(wheel.deltaY >= 0.0f);
        repaint();
        return;
    }

    if (selectedIndices.empty())
        return;

    const int delta = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    if (delta == 0)
        return;

    for (const int idx : selectedIndices)
    {
        if (idx < 0 || idx >= static_cast<int>(notes.size()))
            continue;

        auto& n = notes[static_cast<size_t>(idx)];
        if (velocityLaneBounds().contains(event.getPosition()))
            n.velocity = juce::jlimit(1, 127, n.velocity + delta * 3);
        else
            n.pitch = juce::jlimit(kMinPitch, kMaxPitch, n.pitch + delta);
    }

    commit();
    repaint();
}

void Sub808PianoRollComponent::setBars(int barsCount)
{
    bars = juce::jmax(1, barsCount);
    repaint();
}

void Sub808PianoRollComponent::setStepWidth(float width)
{
    stepWidth = juce::jlimit(8.0f, 160.0f, width);
    repaint();
}

void Sub808PianoRollComponent::setNotes(const std::vector<NoteEvent>& notesIn)
{
    notes = notesIn;
    clearSelection();
    sortNotes();
    repaint();
}

int Sub808PianoRollComponent::totalSteps() const
{
    return bars * 16;
}

int Sub808PianoRollComponent::totalTicks() const
{
    return totalSteps() * ticksPerStep();
}

int Sub808PianoRollComponent::contentWidth() const
{
    return kKeyboardWidth + static_cast<int>(std::round(static_cast<float>(totalSteps()) * stepWidth));
}

int Sub808PianoRollComponent::contentHeight() const
{
    return kTopBarHeight + kPitchRows * rowHeight + kVelocityLaneHeight;
}

juce::Rectangle<int> Sub808PianoRollComponent::topBarBounds() const
{
    return { 0, 0, getWidth(), kTopBarHeight };
}

juce::Rectangle<int> Sub808PianoRollComponent::noteGridBounds() const
{
    return { 0, kTopBarHeight, getWidth(), kPitchRows * rowHeight };
}

juce::Rectangle<int> Sub808PianoRollComponent::velocityLaneBounds() const
{
    return { 0, kTopBarHeight + kPitchRows * rowHeight, getWidth(), kVelocityLaneHeight };
}

int Sub808PianoRollComponent::noteStartTick(const NoteEvent& n) const
{
    return n.step * ticksPerStep() + n.microOffset;
}

int Sub808PianoRollComponent::noteEndTick(const NoteEvent& n) const
{
    return noteStartTick(n) + std::max(1, n.length) * ticksPerStep();
}

void Sub808PianoRollComponent::setNoteStartTick(NoteEvent& n, int tick)
{
    const int clamped = juce::jlimit(0, totalTicks() - 1, tick);
    int step = clamped / ticksPerStep();
    int micro = clamped - step * ticksPerStep();
    if (micro > ticksPerStep() / 2)
    {
        micro -= ticksPerStep();
        ++step;
    }

    n.step = juce::jlimit(0, totalSteps() - 1, step);
    n.microOffset = juce::jlimit(-240, 240, micro);
}

int Sub808PianoRollComponent::snapTickSize() const
{
    switch (snapMode)
    {
        case SnapMode::Free: return 1;
        case SnapMode::OneEighth: return ticksPerStep() * 2;
        case SnapMode::OneQuarter: return ticksPerStep() * 4;
        case SnapMode::Triplet: return ticksPerStep() / 3;
        case SnapMode::OneSixteenth:
        default: return ticksPerStep();
    }
}

int Sub808PianoRollComponent::clampTickToRoll(int tick) const
{
    return juce::jlimit(0, juce::jmax(0, totalTicks() - 1), tick);
}

int Sub808PianoRollComponent::quantizeTick(int tick, bool bypassSnap) const
{
    if (bypassSnap || snapMode == SnapMode::Free)
        return clampTickToRoll(tick);

    const int snap = juce::jmax(1, snapTickSize());
    return clampTickToRoll(static_cast<int>(std::round(static_cast<float>(tick) / static_cast<float>(snap))) * snap);
}

int Sub808PianoRollComponent::tickAtX(int x) const
{
    const int local = juce::jmax(0, x - kKeyboardWidth);
    const int tick = static_cast<int>(std::round((static_cast<float>(local) / juce::jmax(1.0f, stepWidth)) * static_cast<float>(ticksPerStep())));
    return clampTickToRoll(tick);
}

int Sub808PianoRollComponent::snappedTickFromTick(int tick, bool bypassSnap) const
{
    return quantizeTick(tick, bypassSnap);
}

int Sub808PianoRollComponent::snappedTickAtX(int x, bool bypassSnap) const
{
    return snappedTickFromTick(tickAtX(x), bypassSnap);
}

int Sub808PianoRollComponent::pitchAtY(int y) const
{
    const int row = juce::jlimit(0, kPitchRows - 1, (y - noteGridBounds().getY()) / juce::jmax(1, rowHeight));
    return juce::jlimit(kMinPitch, kMaxPitch, kMaxPitch - row);
}

int Sub808PianoRollComponent::yForPitch(int pitch) const
{
    const int p = juce::jlimit(kMinPitch, kMaxPitch, pitch);
    return noteGridBounds().getY() + (kMaxPitch - p) * rowHeight;
}

juce::Rectangle<int> Sub808PianoRollComponent::noteBounds(const NoteEvent& n) const
{
    const float microStep = static_cast<float>(n.microOffset) / static_cast<float>(ticksPerStep());
    const float x = static_cast<float>(kKeyboardWidth) + (static_cast<float>(n.step) + microStep) * stepWidth;
    const float w = stepWidth * static_cast<float>(std::max(1, n.length));
    const int y = yForPitch(n.pitch);

    return juce::Rectangle<int>(static_cast<int>(std::floor(x + 1.0f)),
                                y + 1,
                                static_cast<int>(std::ceil(juce::jmax(3.0f, w - 2.0f))),
                                juce::jmax(6, rowHeight - 2));
}

juce::Rectangle<int> Sub808PianoRollComponent::velocityHandleBounds(const NoteEvent& n) const
{
    const float microStep = static_cast<float>(n.microOffset) / static_cast<float>(ticksPerStep());
    const float startX = static_cast<float>(kKeyboardWidth) + (static_cast<float>(n.step) + microStep) * stepWidth;
    const float endX = startX + stepWidth * static_cast<float>(juce::jmax(1, n.length));

    const auto lane = velocityLaneBounds();
    const float ratio = juce::jlimit(0.0f, 1.0f, static_cast<float>(n.velocity) / 127.0f);
    const int laneBottom = lane.getBottom() - 6;
    const int laneTop = lane.getY() + 18;
    const int h = juce::jmax(3, static_cast<int>(std::round((laneBottom - laneTop) * ratio)));
    const int y = laneBottom - h;

    return juce::Rectangle<int>(static_cast<int>(std::floor(startX + 2.0f)),
                                y,
                                juce::jmax(3, static_cast<int>(std::ceil(juce::jmax(5.0f, endX - startX - 4.0f)))),
                                h);
}

std::optional<int> Sub808PianoRollComponent::findNoteAt(juce::Point<int> p) const
{
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i)
    {
        if (noteBounds(notes[static_cast<size_t>(i)]).expanded(2, 2).contains(p))
            return i;
    }
    return std::nullopt;
}

std::optional<int> Sub808PianoRollComponent::findVelocityHandleAt(juce::Point<int> p) const
{
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i)
    {
        if (velocityHandleBounds(notes[static_cast<size_t>(i)]).expanded(2, 1).contains(p))
            return i;
    }
    return std::nullopt;
}

bool Sub808PianoRollComponent::isSelectedIndex(int index) const
{
    return std::find(selectedIndices.begin(), selectedIndices.end(), index) != selectedIndices.end();
}

void Sub808PianoRollComponent::clearSelection()
{
    selectedIndices.clear();
    activeNoteIndex = -1;
}

void Sub808PianoRollComponent::setSingleSelection(int index)
{
    selectedIndices = { index };
    activeNoteIndex = index;
}

void Sub808PianoRollComponent::setMarqueeSelection(const juce::Rectangle<int>& marquee, bool additive)
{
    std::set<int> result;
    if (additive)
        result.insert(marqueeBaseSelection.begin(), marqueeBaseSelection.end());

    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        if (marquee.intersects(noteBounds(notes[static_cast<size_t>(i)])))
            result.insert(i);
    }

    selectedIndices.assign(result.begin(), result.end());
    activeNoteIndex = selectedIndices.empty() ? -1 : selectedIndices.back();
}

void Sub808PianoRollComponent::collectDragSnapshots()
{
    dragSnapshots.clear();
    for (const int idx : selectedIndices)
    {
        if (idx < 0 || idx >= static_cast<int>(notes.size()))
            continue;

        const auto& n = notes[static_cast<size_t>(idx)];
        dragSnapshots.push_back({ idx, noteStartTick(n), n.pitch, juce::jmax(1, n.length), n.velocity });
    }
}

bool Sub808PianoRollComponent::applySelectionVelocityDelta(int deltaVel)
{
    if (dragSnapshots.empty())
        collectDragSnapshots();

    bool changed = false;
    int velocitySum = 0;
    int velocityCount = 0;
    int startVelocitySum = 0;

    for (const auto& snapshot : dragSnapshots)
    {
        if (snapshot.index < 0 || snapshot.index >= static_cast<int>(notes.size()))
            continue;

        auto& n = notes[static_cast<size_t>(snapshot.index)];
        const int next = juce::jlimit(1, 127, snapshot.startVelocity + deltaVel);
        if (next != n.velocity)
        {
            n.velocity = next;
            changed = true;
        }

        velocitySum += n.velocity;
        startVelocitySum += snapshot.startVelocity;
        ++velocityCount;
    }

    if (velocityCount > 0)
    {
        velocityOverlayPercent = static_cast<int>(std::round((static_cast<float>(velocitySum) / static_cast<float>(velocityCount)) * 100.0f / 127.0f));
        velocityOverlayDelta = static_cast<int>(std::round(static_cast<float>(velocitySum - startVelocitySum) / static_cast<float>(velocityCount)));
    }

    return changed;
}

bool Sub808PianoRollComponent::applySelectionVelocityWave(int deltaVel, int currentMouseX)
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

    for (const auto& snapshot : dragSnapshots)
    {
        if (snapshot.index < 0 || snapshot.index >= static_cast<int>(notes.size()))
            continue;

        auto& n = notes[static_cast<size_t>(snapshot.index)];
        const int noteX = static_cast<int>(std::round((static_cast<float>(snapshot.startTick) / static_cast<float>(ticksPerStep())) * stepWidth));
        const float ratio = juce::jlimit(0.0f,
                                         1.0f,
                                         static_cast<float>(noteX - minMouseX) / static_cast<float>(span));
        const float weighted = currentMouseX >= dragStartX ? ratio : (1.0f - ratio);
        const int next = juce::jlimit(1, 127, snapshot.startVelocity + static_cast<int>(std::round(static_cast<float>(deltaVel) * weighted)));
        if (next != n.velocity)
        {
            n.velocity = next;
            changed = true;
        }

        velocitySum += n.velocity;
        startVelocitySum += snapshot.startVelocity;
        ++velocityCount;
    }

    if (velocityCount > 0)
    {
        velocityOverlayPercent = static_cast<int>(std::round((static_cast<float>(velocitySum) / static_cast<float>(velocityCount)) * 100.0f / 127.0f));
        velocityOverlayDelta = static_cast<int>(std::round(static_cast<float>(velocitySum - startVelocitySum) / static_cast<float>(velocityCount)));
    }

    return changed;
}

bool Sub808PianoRollComponent::copySelectionInternal(bool removeAfterCopy)
{
    if (selectedIndices.empty())
        return false;

    clipboardNotes.clear();
    int minTick = std::numeric_limits<int>::max();
    int maxTick = 0;

    for (const int idx : selectedIndices)
    {
        if (idx < 0 || idx >= static_cast<int>(notes.size()))
            continue;

        const auto& note = notes[static_cast<size_t>(idx)];
        const int startTick = noteStartTick(note);
        const int endTick = noteEndTick(note);
        minTick = juce::jmin(minTick, startTick);
        maxTick = juce::jmax(maxTick, endTick);
        clipboardNotes.push_back({ note, 0 });
    }

    if (clipboardNotes.empty())
        return false;

    for (auto& item : clipboardNotes)
        item.relativeTick = noteStartTick(item.note) - minTick;

    clipboardSourceStartTick = minTick;
    clipboardSpanTicks = juce::jmax(ticksPerStep(), maxTick - minTick);

    if (removeAfterCopy)
        return deleteSelectedNotes();

    return true;
}

Sub808PianoRollComponent::ContextMenuTarget Sub808PianoRollComponent::contextMenuTargetAt(juce::Point<int> p, std::optional<int>* hitOut)
{
    const auto hit = findNoteAt(p);
    if (hitOut != nullptr)
        *hitOut = hit;

    if (!hit.has_value())
        return ContextMenuTarget::Empty;

    if (selectedIndices.size() > 1 && isSelectedIndex(*hit))
        return ContextMenuTarget::Selection;

    return ContextMenuTarget::SingleNote;
}

void Sub808PianoRollComponent::showContextMenu(juce::Point<int> p)
{
    std::optional<int> hit;
    const auto target = contextMenuTargetAt(p, &hit);
    if (hit.has_value() && target == ContextMenuTarget::SingleNote)
        setSingleSelection(*hit);

    juce::PopupMenu menu;
    constexpr int kActionCut = 1;
    constexpr int kActionCopy = 2;
    constexpr int kActionPaste = 3;
    constexpr int kActionDuplicate = 4;
    constexpr int kActionDelete = 5;
    constexpr int kActionSelectAll = 6;
    constexpr int kActionDeselect = 7;
    constexpr int kActionSplit = 8;
    constexpr int kActionQuickRoll = 9;
    constexpr int kActionAdvancedRollBase = 100;

    if (target == ContextMenuTarget::Empty)
    {
        menu.addItem(kActionPaste, "Paste", !clipboardNotes.empty());
        menu.addItem(kActionSelectAll, "Select All", !notes.empty());
        menu.addItem(kActionDeselect, "Deselect", !selectedIndices.empty());
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
        menu.addItem(kActionQuickRoll, "Create Roll", quickRollEnabled);

        juce::PopupMenu advancedRollMenu;
        for (const int division : advancedDivisions)
            advancedRollMenu.addItem(kActionAdvancedRollBase + division, "Divide x" + juce::String(division));
        menu.addSubMenu("Advanced Roll", advancedRollMenu, !advancedDivisions.empty());
    }

    menu.showMenuAsync(juce::PopupMenu::Options{}.withParentComponent(this),
                       [safe = juce::Component::SafePointer<Sub808PianoRollComponent>(this), p](int choice)
                       {
                           if (safe == nullptr || choice <= 0)
                               return;
                           safe->applyContextAction(choice, p);
                       });
}

bool Sub808PianoRollComponent::applyContextAction(int actionId, juce::Point<int> p)
{
    if (actionId >= 100)
        return rollSelectionWithDivisions(actionId - 100);
    if (actionId == 1)
        return cutSelectionToClipboard();
    if (actionId == 2)
        return copySelectionToClipboard();
    if (actionId == 3)
        return pasteClipboardAtAnchor();
    if (actionId == 4)
        return duplicateSelectionToRight();
    if (actionId == 5)
        return deleteSelectedNotes();
    if (actionId == 6)
        return selectAllNotes();
    if (actionId == 7)
    {
        clearNoteSelection();
        return true;
    }
    if (actionId == 8)
    {
        if (auto hit = findNoteAt(p); hit.has_value())
        {
            const bool changed = splitNoteAtTick(*hit, snappedTickAtX(p.x));
            if (changed)
            {
                sortNotes();
                commit();
                repaint();
            }
            return changed;
        }
    }
    if (actionId == 9)
        return rollSelectionQuick();

    return false;
}

bool Sub808PianoRollComponent::canQuickRollSelection() const
{
    const auto available = availableAdvancedRollDivisions();
    return std::find(available.begin(), available.end(), 2) != available.end();
}

std::vector<int> Sub808PianoRollComponent::availableAdvancedRollDivisions() const
{
    if (selectedIndices.empty())
        return {};

    static constexpr int candidates[] { 2, 3, 4, 6, 8, 12, 16 };
    std::vector<int> divisions;

    for (const int division : candidates)
    {
        bool validForAll = true;
        for (const int idx : selectedIndices)
        {
            if (idx < 0 || idx >= static_cast<int>(notes.size()))
            {
                validForAll = false;
                break;
            }

            const auto& note = notes[static_cast<size_t>(idx)];
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

bool Sub808PianoRollComponent::rollSelectionQuick()
{
    return rollSelectionWithDivisions(2);
}

bool Sub808PianoRollComponent::rollSelectionWithDivisions(int divisions)
{
    if (selectedIndices.empty() || divisions < 2)
        return false;

    const auto available = availableAdvancedRollDivisions();
    if (std::find(available.begin(), available.end(), divisions) == available.end())
        return false;

    std::vector<NoteEvent> insertedNotes;
    auto refs = selectedIndices;
    std::sort(refs.begin(), refs.end(), std::greater<int>());

    for (const int idx : refs)
    {
        if (idx < 0 || idx >= static_cast<int>(notes.size()))
            continue;

        const auto source = notes[static_cast<size_t>(idx)];
        const int startTick = noteStartTick(source);
        const int segmentSteps = source.length / divisions;

        for (int i = 0; i < divisions; ++i)
        {
            NoteEvent rolled = source;
            rolled.length = segmentSteps;
            setNoteStartTick(rolled, startTick + i * segmentSteps * ticksPerStep());
            insertedNotes.push_back(rolled);
        }

        notes.erase(notes.begin() + idx);
    }

    clearSelection();
    for (const auto& note : insertedNotes)
        notes.push_back(note);
    sortNotes();

    std::vector<bool> consumed(notes.size(), false);
    for (const auto& note : insertedNotes)
    {
        for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i)
        {
            if (consumed[static_cast<size_t>(i)])
                continue;

            const auto& current = notes[static_cast<size_t>(i)];
            if (noteStartTick(current) == noteStartTick(note)
                && current.pitch == note.pitch
                && current.length == note.length
                && current.velocity == note.velocity
                && current.microOffset == note.microOffset)
            {
                consumed[static_cast<size_t>(i)] = true;
                selectedIndices.push_back(i);
                activeNoteIndex = i;
                break;
            }
        }
    }

    commit();
    repaint();
    return !insertedNotes.empty();
}

bool Sub808PianoRollComponent::splitNoteAtTick(int noteIndex, int cutTick)
{
    if (noteIndex < 0 || noteIndex >= static_cast<int>(notes.size()))
        return false;

    auto& note = notes[static_cast<size_t>(noteIndex)];
    const int startTick = noteStartTick(note);
    const int endTick = noteEndTick(note);
    if (cutTick <= startTick + ticksPerStep() || cutTick >= endTick - ticksPerStep())
        return false;

    NoteEvent tail = note;
    setNoteStartTick(tail, cutTick);
    note.length = juce::jmax(1,
                             static_cast<int>(std::ceil(static_cast<double>(cutTick - startTick)
                                                        / static_cast<double>(ticksPerStep()))));
    tail.length = juce::jmax(1,
                             static_cast<int>(std::ceil(static_cast<double>(endTick - cutTick)
                                                        / static_cast<double>(ticksPerStep()))));
    notes.push_back(tail);
    sortNotes();
    return true;
}

bool Sub808PianoRollComponent::eraseNoteAt(juce::Point<int> p)
{
    const auto hit = findNoteAt(p);
    if (!hit.has_value() || *hit < 0 || *hit >= static_cast<int>(notes.size()))
        return false;

    const auto& note = notes[static_cast<size_t>(*hit)];
    const int key = ((noteStartTick(note) & 0x0fffff) << 4) ^ (note.pitch & 0x0f);
    if (!eraseVisitedKeys.insert(key).second)
        return false;

    if (isSelectedIndex(*hit) && selectedIndices.size() > 1)
    {
        std::sort(selectedIndices.begin(), selectedIndices.end(), std::greater<int>());
        for (const int idx : selectedIndices)
            if (idx >= 0 && idx < static_cast<int>(notes.size()))
                notes.erase(notes.begin() + idx);
        clearSelection();
        return true;
    }

    notes.erase(notes.begin() + *hit);
    clearSelection();
    return true;
}

bool Sub808PianoRollComponent::eraseNotesAlongSegment(juce::Point<int> from, juce::Point<int> to)
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

bool Sub808PianoRollComponent::placeDrawNoteAt(juce::Point<int> p)
{
    if (!noteGridBounds().contains(p) || p.x < kKeyboardWidth)
        return false;

    NoteEvent note;
    note.pitch = pitchAtY(p.y);
    const int tick = snappedTickAtX(p.x);
    note.step = juce::jlimit(0, totalSteps() - 1, tick / ticksPerStep());
    note.length = 1;
    note.velocity = 100;
    note.microOffset = tick - note.step * ticksPerStep();

    const int drawKey = ((tick & 0x0fffff) << 7) ^ (note.pitch & 0x7f);
    if (!drawVisitedKeys.insert(drawKey).second)
        return false;

    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        const auto& existing = notes[static_cast<size_t>(i)];
        if (existing.pitch == note.pitch && noteStartTick(existing) == tick)
            return false;
    }

    notes.push_back(note);
    sortNotes();
    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        if (notes[static_cast<size_t>(i)].pitch == note.pitch
            && noteStartTick(notes[static_cast<size_t>(i)]) == tick)
        {
            setSingleSelection(i);
            activeNoteIndex = i;
            break;
        }
    }
    return true;
}

bool Sub808PianoRollComponent::isVelocityEditKeyDown() const
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

bool Sub808PianoRollComponent::isStretchEditKeyDown() const
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

juce::String Sub808PianoRollComponent::snapModeLabel() const
{
    switch (snapMode)
    {
        case SnapMode::Free: return "Free";
        case SnapMode::OneEighth: return "1/8";
        case SnapMode::OneQuarter: return "1/4";
        case SnapMode::Triplet: return "Triplet";
        case SnapMode::OneSixteenth:
        default: return "1/16";
    }
}

void Sub808PianoRollComponent::cycleSnapMode(bool forward)
{
    static constexpr std::array<SnapMode, 5> modes {
        SnapMode::Free,
        SnapMode::OneSixteenth,
        SnapMode::OneEighth,
        SnapMode::OneQuarter,
        SnapMode::Triplet
    };

    auto it = std::find(modes.begin(), modes.end(), snapMode);
    int index = (it != modes.end()) ? static_cast<int>(std::distance(modes.begin(), it)) : 1;
    index += forward ? 1 : -1;
    if (index < 0)
        index = static_cast<int>(modes.size()) - 1;
    if (index >= static_cast<int>(modes.size()))
        index = 0;
    snapMode = modes[static_cast<size_t>(index)];
}

void Sub808PianoRollComponent::shiftFocusedOctave(int delta)
{
    if (delta == 0)
        return;

    const int semitoneDelta = delta * 12;
    bool changedSelection = false;
    for (const int idx : selectedIndices)
    {
        if (idx < 0 || idx >= static_cast<int>(notes.size()))
            continue;

        auto& n = notes[static_cast<size_t>(idx)];
        const int nextPitch = juce::jlimit(kMinPitch, kMaxPitch, n.pitch + semitoneDelta);
        if (nextPitch != n.pitch)
        {
            n.pitch = nextPitch;
            changedSelection = true;
        }
    }

    if (changedSelection)
        commit();

    focusedOctave = juce::jlimit(1, 7, focusedOctave + delta);
}

void Sub808PianoRollComponent::updateMouseCursor()
{
    if (editMode == EditMode::Stretch || isStretchEditKeyDown())
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(toolCursorFor(editorTool));
}

void Sub808PianoRollComponent::sortNotes()
{
    std::vector<NoteEvent> selectedSnapshots;
    selectedSnapshots.reserve(selectedIndices.size());
    for (const int idx : selectedIndices)
    {
        if (idx >= 0 && idx < static_cast<int>(notes.size()))
            selectedSnapshots.push_back(notes[static_cast<size_t>(idx)]);
    }

    std::optional<NoteEvent> activeSnapshot;
    if (activeNoteIndex >= 0 && activeNoteIndex < static_cast<int>(notes.size()))
        activeSnapshot = notes[static_cast<size_t>(activeNoteIndex)];

    std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        if (a.microOffset != b.microOffset)
            return a.microOffset < b.microOffset;
        return a.pitch < b.pitch;
    });

    selectedIndices.clear();
    for (const auto& selectedNote : selectedSnapshots)
    {
        for (int i = 0; i < static_cast<int>(notes.size()); ++i)
        {
            const auto& current = notes[static_cast<size_t>(i)];
            if (current.pitch == selectedNote.pitch
                && current.step == selectedNote.step
                && current.length == selectedNote.length
                && current.velocity == selectedNote.velocity
                && current.microOffset == selectedNote.microOffset)
            {
                selectedIndices.push_back(i);
                break;
            }
        }
    }

    activeNoteIndex = -1;
    if (activeSnapshot.has_value())
    {
        for (int i = 0; i < static_cast<int>(notes.size()); ++i)
        {
            const auto& current = notes[static_cast<size_t>(i)];
            if (current.pitch == activeSnapshot->pitch
                && current.step == activeSnapshot->step
                && current.length == activeSnapshot->length
                && current.microOffset == activeSnapshot->microOffset)
            {
                activeNoteIndex = i;
                break;
            }
        }
    }
}

void Sub808PianoRollComponent::commit()
{
    if (onNotesEdited)
        onNotesEdited(notes);
}
} // namespace bbg
