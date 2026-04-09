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
        int lowerPanelHeight = 236;
        MainHeaderComponent::HeaderControlsMode headerControlsMode = MainHeaderComponent::HeaderControlsMode::Expanded;
    };

    static constexpr int defaultLeftPanelWidth = 420;
    static constexpr int defaultLowerPanelHeight = 236;

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

        if (tree.hasProperty(kUiLowerPanelHeightProp))
            state.lowerPanelHeight = static_cast<int>(tree.getProperty(kUiLowerPanelHeightProp));

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
        tree.setProperty(kUiLowerPanelHeightProp, state.lowerPanelHeight, nullptr);

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

    int clampLowerPanelHeight(int minHeight, int maxHeight)
    {
        state.lowerPanelHeight = juce::jlimit(minHeight, maxHeight, state.lowerPanelHeight);
        return state.lowerPanelHeight;
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

    void beginLowerPanelSplitterDrag(int startY)
    {
        lowerPanelSplitterDragging = true;
        lowerPanelSplitterDragStartY = startY;
        lowerPanelStartHeight = state.lowerPanelHeight;
    }

    bool isLowerPanelSplitterDragging() const
    {
        return lowerPanelSplitterDragging;
    }

    bool isAnySplitterDragging() const
    {
        return splitterDragging || lowerPanelSplitterDragging;
    }

    int updateSplitterDrag(int currentX)
    {
        if (!splitterDragging)
            return state.leftPanelWidth;

        state.leftPanelWidth = splitterStartWidth + (currentX - splitterDragStartX);
        return state.leftPanelWidth;
    }

    int updateLowerPanelSplitterDrag(int currentY)
    {
        if (!lowerPanelSplitterDragging)
            return state.lowerPanelHeight;

        state.lowerPanelHeight = lowerPanelStartHeight - (currentY - lowerPanelSplitterDragStartY);
        return state.lowerPanelHeight;
    }

    void endSplitterDrag()
    {
        splitterDragging = false;
    }

    void endLowerPanelSplitterDrag()
    {
        lowerPanelSplitterDragging = false;
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
    static constexpr auto kUiLowerPanelHeightProp = "ui.lowerPanelHeight";
    static constexpr auto kUiHeaderModeProp = "ui.headerControlsMode";
    static constexpr int rackMinWidth = 240;
    static constexpr int rackCompactThreshold = 320;
    static constexpr int rackFullThreshold = 460;

private:
    State state;
    bool splitterDragging = false;
    int splitterDragStartX = 0;
    int splitterStartWidth = defaultLeftPanelWidth;
    bool lowerPanelSplitterDragging = false;
    int lowerPanelSplitterDragStartY = 0;
    int lowerPanelStartHeight = defaultLowerPanelHeight;
    bool pianoRollFullscreenMode = false;
    bool gridEditorFullscreenMode = false;
};
} // namespace bbg