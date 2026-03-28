#pragma once

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace bbg
{
class Sub808EditModel
{
public:
    struct PitchedNoteEvent
    {
        int pitch = 36;
        int step = 0;
        int length = 1;
        int velocity = 100;
        int microOffset = 0;
        bool isGhost = false;
        juce::String semanticRole;
        bool isSlide = false;
        bool isLegato = false;
        bool glideToNext = false;
    };

    struct DragSnapshot
    {
        int index = -1;
        int startTick = 0;
        int startPitch = 36;
        int startLength = 1;
        int startVelocity = 100;
    };

    struct ClipboardNote
    {
        PitchedNoteEvent note;
        int relativeTick = 0;
    };

    static bool isSelectedIndex(const std::vector<int>& selectedIndices, int index)
    {
        return std::find(selectedIndices.begin(), selectedIndices.end(), index) != selectedIndices.end();
    }

    static void clearSelection(std::vector<int>& selectedIndices, int& activeNoteIndex)
    {
        selectedIndices.clear();
        activeNoteIndex = -1;
    }

    static void setSingleSelection(std::vector<int>& selectedIndices, int& activeNoteIndex, int index)
    {
        selectedIndices = { index };
        activeNoteIndex = index;
    }

    static bool deleteSelectedNotes(std::vector<PitchedNoteEvent>& notes,
                                    std::vector<int>& selectedIndices,
                                    int& activeNoteIndex)
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

        clearSelection(selectedIndices, activeNoteIndex);
        return changed;
    }

    static void setMarqueeSelection(const juce::Rectangle<int>& marquee,
                                    bool additive,
                                    const std::vector<int>& marqueeBaseSelection,
                                    const std::vector<PitchedNoteEvent>& notes,
                                    std::vector<int>& selectedIndices,
                                    int& activeNoteIndex,
                                    const std::function<juce::Rectangle<int>(const PitchedNoteEvent&)>& noteBoundsFor)
    {
        std::set<int> result;
        if (additive)
            result.insert(marqueeBaseSelection.begin(), marqueeBaseSelection.end());

        for (int i = 0; i < static_cast<int>(notes.size()); ++i)
        {
            if (marquee.intersects(noteBoundsFor(notes[static_cast<size_t>(i)])))
                result.insert(i);
        }

        selectedIndices.assign(result.begin(), result.end());
        activeNoteIndex = selectedIndices.empty() ? -1 : selectedIndices.back();
    }

    static void collectDragSnapshots(const std::vector<int>& selectedIndices,
                                     const std::vector<PitchedNoteEvent>& notes,
                                     std::vector<DragSnapshot>& dragSnapshots,
                                     const std::function<int(const PitchedNoteEvent&)>& noteStartTickFor)
    {
        dragSnapshots.clear();
        for (const int idx : selectedIndices)
        {
            if (idx < 0 || idx >= static_cast<int>(notes.size()))
                continue;

            const auto& note = notes[static_cast<size_t>(idx)];
            dragSnapshots.push_back({ idx,
                                      noteStartTickFor(note),
                                      note.pitch,
                                      juce::jmax(1, note.length),
                                      note.velocity });
        }
    }

    static bool copySelectionToClipboard(const std::vector<int>& selectedIndices,
                                         const std::vector<PitchedNoteEvent>& notes,
                                         std::vector<ClipboardNote>& clipboardNotes,
                                         int& clipboardSourceStartTick,
                                         int& clipboardSpanTicks,
                                         const std::function<int(const PitchedNoteEvent&)>& noteStartTickFor,
                                         const std::function<int(const PitchedNoteEvent&)>& noteEndTickFor)
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
            const int startTick = noteStartTickFor(note);
            const int endTick = noteEndTickFor(note);
            minTick = juce::jmin(minTick, startTick);
            maxTick = juce::jmax(maxTick, endTick);
            clipboardNotes.push_back({ note, 0 });
        }

        if (clipboardNotes.empty())
            return false;

        for (auto& item : clipboardNotes)
            item.relativeTick = noteStartTickFor(item.note) - minTick;

        clipboardSourceStartTick = minTick;
        clipboardSpanTicks = juce::jmax(1, maxTick - minTick);
        return true;
    }

    static bool pasteClipboard(std::vector<PitchedNoteEvent>& notes,
                               const std::vector<ClipboardNote>& clipboardNotes,
                               int anchorTick,
                               int totalTicks,
                               std::vector<int>& selectedIndices,
                               int& activeNoteIndex,
                               std::vector<PitchedNoteEvent>* insertedNotesOut,
                               const std::function<void(PitchedNoteEvent&, int)>& setNoteStartTick)
    {
        if (clipboardNotes.empty())
            return false;

        clearSelection(selectedIndices, activeNoteIndex);

        std::vector<PitchedNoteEvent> insertedNotes;
        insertedNotes.reserve(clipboardNotes.size());

        for (const auto& item : clipboardNotes)
        {
            PitchedNoteEvent note = item.note;
            setNoteStartTick(note, juce::jlimit(0, juce::jmax(0, totalTicks - 1), anchorTick + item.relativeTick));
            notes.push_back(note);
            insertedNotes.push_back(note);
        }

        if (insertedNotesOut != nullptr)
            *insertedNotesOut = insertedNotes;

        return !insertedNotes.empty();
    }

    static void sortNotes(std::vector<PitchedNoteEvent>& notes,
                          std::vector<int>& selectedIndices,
                          int& activeNoteIndex)
    {
        std::vector<PitchedNoteEvent> selectedSnapshots;
        selectedSnapshots.reserve(selectedIndices.size());
        for (const int idx : selectedIndices)
        {
            if (idx >= 0 && idx < static_cast<int>(notes.size()))
                selectedSnapshots.push_back(notes[static_cast<size_t>(idx)]);
        }

        std::optional<PitchedNoteEvent> activeSnapshot;
        if (activeNoteIndex >= 0 && activeNoteIndex < static_cast<int>(notes.size()))
            activeSnapshot = notes[static_cast<size_t>(activeNoteIndex)];

        std::stable_sort(notes.begin(), notes.end(), [](const PitchedNoteEvent& a, const PitchedNoteEvent& b)
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
                    && current.microOffset == selectedNote.microOffset
                    && current.isSlide == selectedNote.isSlide
                    && current.isLegato == selectedNote.isLegato
                    && current.glideToNext == selectedNote.glideToNext)
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
                    && current.microOffset == activeSnapshot->microOffset
                    && current.isSlide == activeSnapshot->isSlide
                    && current.isLegato == activeSnapshot->isLegato
                    && current.glideToNext == activeSnapshot->glideToNext)
                {
                    activeNoteIndex = i;
                    break;
                }
            }
        }
    }
};
} // namespace bbg
