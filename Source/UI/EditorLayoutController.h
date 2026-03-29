#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "MainHeaderComponent.h"
#include "TrackRowComponent.h"

namespace bbg
{
class EditorLayoutController
{
public:
    struct State
    {
        int leftPanelWidth = 420;
        MainHeaderComponent::HeaderControlsMode headerControlsMode = MainHeaderComponent::HeaderControlsMode::Expanded;
    };

    static constexpr int defaultLeftPanelWidth = 420;

    const State& getState() const
    {
        return state;
    }

    State& getState()
    {
        return state;
    }

    void load(juce::ValueTree& tree)
    {
        if (tree.hasProperty(kUiLeftWidthProp))
            state.leftPanelWidth = static_cast<int>(tree.getProperty(kUiLeftWidthProp));

        if (tree.hasProperty(kUiHeaderModeProp))
        {
            const int stored = static_cast<int>(tree.getProperty(kUiHeaderModeProp));
            if (stored == 1)
                state.headerControlsMode = MainHeaderComponent::HeaderControlsMode::Compact;
            else if (stored == 2)
                state.headerControlsMode = MainHeaderComponent::HeaderControlsMode::Hidden;
            else
                state.headerControlsMode = MainHeaderComponent::HeaderControlsMode::Expanded;
        }
    }

    void save(juce::ValueTree& tree) const
    {
        tree.setProperty(kUiLeftWidthProp, state.leftPanelWidth, nullptr);

        int modeValue = 0;
        if (state.headerControlsMode == MainHeaderComponent::HeaderControlsMode::Compact)
            modeValue = 1;
        else if (state.headerControlsMode == MainHeaderComponent::HeaderControlsMode::Hidden)
            modeValue = 2;

        tree.setProperty(kUiHeaderModeProp, modeValue, nullptr);
    }

    LaneRackDisplayMode laneRackModeForWidth(int width) const
    {
        if (width <= rackCompactThreshold)
            return LaneRackDisplayMode::Minimal;
        if (width <= rackFullThreshold)
            return LaneRackDisplayMode::Compact;
        return LaneRackDisplayMode::Full;
    }

    int clampLeftPanelWidth(int rackMax)
    {
        state.leftPanelWidth = juce::jlimit(rackMinWidth, rackMax, state.leftPanelWidth);
        return state.leftPanelWidth;
    }

    void beginSplitterDrag(int startX)
    {
        splitterDragging = true;
        splitterDragStartX = startX;
        splitterStartWidth = state.leftPanelWidth;
    }

    bool isSplitterDragging() const
    {
        return splitterDragging;
    }

    int updateSplitterDrag(int currentX)
    {
        if (!splitterDragging)
            return state.leftPanelWidth;

        state.leftPanelWidth = splitterStartWidth + (currentX - splitterDragStartX);
        return state.leftPanelWidth;
    }

    void endSplitterDrag()
    {
        splitterDragging = false;
    }

    bool isGridEditorFullscreenMode() const
    {
        return gridEditorFullscreenMode;
    }

    bool isPianoRollFullscreenMode() const
    {
        return pianoRollFullscreenMode;
    }

    void setGridEditorFullscreenMode(bool shouldBeFullscreen)
    {
        gridEditorFullscreenMode = shouldBeFullscreen;
    }

    void toggleGridEditorFullscreenMode()
    {
        gridEditorFullscreenMode = !gridEditorFullscreenMode;
    }

    void setPianoRollFullscreenMode(bool shouldBeFullscreen)
    {
        pianoRollFullscreenMode = shouldBeFullscreen;
    }

    static constexpr auto kUiLeftWidthProp = "ui.leftPanelWidth";
    static constexpr auto kUiHeaderModeProp = "ui.headerControlsMode";
    static constexpr int rackMinWidth = 240;
    static constexpr int rackCompactThreshold = 320;
    static constexpr int rackFullThreshold = 460;

private:
    State state;
    bool splitterDragging = false;
    int splitterDragStartX = 0;
    int splitterStartWidth = defaultLeftPanelWidth;
    bool pianoRollFullscreenMode = false;
    bool gridEditorFullscreenMode = false;
};
} // namespace bbg