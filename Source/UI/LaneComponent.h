#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "LaneGridComponent.h"
#include "LaneHeaderComponent.h"

namespace bbg
{
class LaneComponent : public juce::Component
{
public:
    explicit LaneComponent(const TrackInfo& info)
        : header(std::make_unique<LaneHeaderComponent>(info))
    {
        addAndMakeVisible(*header);
        addAndMakeVisible(grid);
        grid.setTrackType(info.type);
    }

    LaneHeaderComponent& getHeader() { return *header; }
    const LaneHeaderComponent& getHeader() const { return *header; }
    LaneGridComponent& getGrid() { return grid; }
    const LaneGridComponent& getGrid() const { return grid; }

    void setHeaderWidth(int width)
    {
        headerWidth = juce::jmax(120, width);
        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        header->setBounds(area.removeFromLeft(juce::jmin(headerWidth, area.getWidth())));
        grid.setBounds(area);
    }

private:
    std::unique_ptr<LaneHeaderComponent> header;
    LaneGridComponent grid;
    int headerWidth = 320;
};
} // namespace bbg