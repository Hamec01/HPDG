#pragma once

#include <algorithm>
#include <cmath>

#include <juce_graphics/juce_graphics.h>

#include "../Core/NoteEvent.h"

namespace bbg
{
struct GridGeometryConfig
{
    float stepWidth = 20.0f;
    int laneHeight = 30;
    int rulerHeight = 24;
    int bars = 1;
    int ticksPerStep = 1;
    int resolutionTicks = 1;
};

class GridGeometry
{
public:
    explicit GridGeometry(GridGeometryConfig configuration)
        : config(configuration)
    {
        config.bars = std::max(1, config.bars);
        config.ticksPerStep = std::max(1, config.ticksPerStep);
        config.resolutionTicks = std::max(1, config.resolutionTicks);
        config.stepWidth = juce::jmax(1.0f, config.stepWidth);
        config.laneHeight = std::max(1, config.laneHeight);
    }

    int totalSteps() const
    {
        return config.bars * 16;
    }

    int totalTicks() const
    {
        return totalSteps() * config.ticksPerStep;
    }

    int clampTick(int tick) const
    {
        return juce::jlimit(0, juce::jmax(0, totalTicks() - 1), tick);
    }

    float stepToX(int step) const
    {
        return static_cast<float>(step) * config.stepWidth;
    }

    int xToStep(int x) const
    {
        const int step = static_cast<int>(std::floor(static_cast<float>(x) / config.stepWidth));
        return juce::jlimit(0, juce::jmax(0, totalSteps() - 1), step);
    }

    float tickToX(int tick) const
    {
        return (static_cast<float>(clampTick(tick)) / static_cast<float>(config.ticksPerStep)) * config.stepWidth;
    }

    int xToTick(int x) const
    {
        const float tickFloat = (static_cast<float>(x) * static_cast<float>(config.ticksPerStep)) / config.stepWidth;
        return clampTick(static_cast<int>(std::floor(tickFloat)));
    }

    float laneToY(int laneIndex) const
    {
        return static_cast<float>(config.rulerHeight + laneIndex * config.laneHeight);
    }

    int yToLane(int y) const
    {
        if (y < config.rulerHeight)
            return -1;

        return (y - config.rulerHeight) / config.laneHeight;
    }

    juce::Rectangle<int> cellRect(int step, int laneIndex) const
    {
        return juce::Rectangle<int>(static_cast<int>(std::floor(stepToX(step))),
                                    static_cast<int>(std::floor(laneToY(laneIndex))),
                                    static_cast<int>(std::ceil(config.stepWidth)),
                                    config.laneHeight);
    }

    juce::Rectangle<int> noteRect(const NoteEvent& note, int laneIndex) const
    {
        const float microStep = static_cast<float>(note.microOffset) / static_cast<float>(config.ticksPerStep);
        const float microShiftPx = juce::jlimit(-0.98f * config.stepWidth, 0.98f * config.stepWidth, microStep * config.stepWidth);
        const float x = stepToX(note.step) + microShiftPx;
        const float y = laneToY(laneIndex);
        const float subStepWidth = config.stepWidth * (static_cast<float>(config.resolutionTicks)
                                                      / static_cast<float>(config.ticksPerStep));

        float width = config.stepWidth * static_cast<float>(std::max(1, note.length));
        if (note.length <= 1 && config.resolutionTicks < config.ticksPerStep)
            width = juce::jmax(2.0f, subStepWidth - 1.0f);

        return juce::Rectangle<int>(static_cast<int>(std::floor(x + 1.0f)),
                                    static_cast<int>(std::floor(y + 2.0f)),
                                    static_cast<int>(std::ceil(juce::jmax(2.0f, width - 2.0f))),
                                    juce::jmax(4, config.laneHeight - 4));
    }

private:
    GridGeometryConfig config;
};
} // namespace bbg