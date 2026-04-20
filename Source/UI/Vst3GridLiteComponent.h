#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/PatternProject.h"

namespace bbg
{
// VST3 uses a dedicated lightweight grid surface instead of the Standalone
// full authoring grid. The goal is to keep the same pattern/core architecture
// while avoiding the heavy interaction path that reproduces FL Studio hangs.
class Vst3GridLiteComponent final : public juce::Component
{
public:
    void setProject(const PatternProject& value);
    void setLaneDisplayOrder(const std::vector<RuntimeLaneId>& order);
    void setSelectedTrack(const RuntimeLaneId& laneId);
    void setRowHeight(int value);
    void setPlayheadStep(float step);
    void setPreviewStartStep(int step);
    void setLoopRegion(const std::optional<juce::Range<int>>& tickRange);
    int getPreferredContentHeight() const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

    std::function<void(const RuntimeLaneId&)> onLaneClicked;
    std::function<void(int)> onStepClicked;

private:
    struct VisibleLane
    {
        RuntimeLaneId laneId;
        juce::String laneName;
        std::optional<TrackType> trackType;
    };

    std::vector<VisibleLane> orderedVisibleLanes() const;
    juce::String laneDisplayName(const VisibleLane& lane) const;
    juce::Colour laneAccentColour(const VisibleLane& lane) const;
    juce::Colour noteColour(const NoteEvent& note, bool selectedLane) const;
    void drawNoteGlow(juce::Graphics& g, const juce::Rectangle<float>& rect, juce::Colour colour) const;
    std::optional<int> laneIndexAtY(int yInGrid, int effectiveRowHeight, int laneCount) const;

    PatternProject project;
    std::vector<RuntimeLaneId> laneDisplayOrder;
    RuntimeLaneId selectedTrack;
    std::optional<juce::Range<int>> loopRegionTicks;
    int rowHeight = 32;
    float playheadStep = -1.0f;
    int previewStartStep = 0;
};
} // namespace bbg
