#pragma once

#include <functional>
#include <optional>

#include <juce_graphics/juce_graphics.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/PatternProject.h"

namespace bbg
{
class GridEditorComponent : public juce::Component
{
public:
    enum class GridResolution
    {
        OneEighth,
        OneSixteenth,
        OneThirtySecond,
        OneSixtyFourth,
        Triplet
    };

    void setProject(const PatternProject& value);
    void setStepWidth(float width);
    void setLaneHeight(int height);
    void setGridResolution(GridResolution resolution);
    void setSelectedTrack(TrackType type);
    void setPlayheadStep(float step);

    int getGridWidth() const;
    int getGridHeight() const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    std::function<void(TrackType, int)> onCellClicked;
    std::function<void(TrackType, const std::vector<NoteEvent>&)> onTrackNotesEdited;
    std::function<void(float)> onHorizontalZoomGesture;
    std::function<void(float)> onLaneHeightZoomGesture;

private:
    struct SelectedNoteRef
    {
        TrackType track = TrackType::Kick;
        int index = -1;
    };

    void invalidateStaticCache();
    void ensureStaticCache();
    std::optional<SelectedNoteRef> findNoteAt(juce::Point<int> position) const;
    juce::Rectangle<int> noteBounds(const NoteEvent& note, int row) const;
    int snapTicksForCurrentResolution() const;
    int ticksForGridResolution() const;

    bool isVisibleLane(TrackType type) const;
    int visibleLaneIndex(TrackType type) const;
    std::optional<TrackType> trackForVisibleLaneIndex(int laneIndex) const;

    PatternProject project;
    juce::Image staticGridCache;
    bool staticCacheDirty = true;

    float stepWidth = 20.0f;
    int laneHeight = 30;
    int rulerHeight = 24;
    GridResolution gridResolution = GridResolution::OneSixteenth;
    TrackType selectedTrack = TrackType::Kick;
    float playheadStep = -1.0f;
    int lastPlayheadPixel = -1;

    std::optional<SelectedNoteRef> selectedNote;
    bool draggingSelectedNote = false;
    int dragStartX = 0;
    int dragOriginalTicks = 0;
};
} // namespace bbg
