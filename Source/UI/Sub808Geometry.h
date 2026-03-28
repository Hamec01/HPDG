#pragma once

#include <cmath>

#include <juce_graphics/juce_graphics.h>

namespace bbg
{
class Sub808Geometry
{
public:
    struct Config
    {
        int width = 0;
        int totalSteps = 16;
        float stepWidth = 20.0f;
        int rowHeight = 14;
        int topBarHeight = 30;
        int keyboardWidth = 76;
        int velocityLaneHeight = 92;
        int minPitch = 24;
        int maxPitch = 84;
        int ticksPerStep = 960;
    };

    explicit Sub808Geometry(Config configIn)
        : config(configIn)
    {
    }

    int contentWidth() const
    {
        return config.keyboardWidth + static_cast<int>(std::round(static_cast<float>(config.totalSteps) * config.stepWidth));
    }

    int contentHeight() const
    {
        return config.topBarHeight + pitchRows() * config.rowHeight + config.velocityLaneHeight;
    }

    juce::Rectangle<int> topBarBounds() const
    {
        return { 0, 0, config.width, config.topBarHeight };
    }

    juce::Rectangle<int> noteGridBounds() const
    {
        return { 0, config.topBarHeight, config.width, pitchRows() * config.rowHeight };
    }

    juce::Rectangle<int> velocityLaneBounds() const
    {
        return { 0, config.topBarHeight + pitchRows() * config.rowHeight, config.width, config.velocityLaneHeight };
    }

    int pitchAtY(int y) const
    {
        const int row = juce::jlimit(0, pitchRows() - 1, (y - noteGridBounds().getY()) / juce::jmax(1, config.rowHeight));
        return juce::jlimit(config.minPitch, config.maxPitch, config.maxPitch - row);
    }

    int yForPitch(int pitch) const
    {
        const int clampedPitch = juce::jlimit(config.minPitch, config.maxPitch, pitch);
        return noteGridBounds().getY() + (config.maxPitch - clampedPitch) * config.rowHeight;
    }

    juce::Rectangle<int> noteBounds(int step, int microOffset, int length, int pitch) const
    {
        const float microStep = static_cast<float>(microOffset) / static_cast<float>(juce::jmax(1, config.ticksPerStep));
        const float x = static_cast<float>(config.keyboardWidth) + (static_cast<float>(step) + microStep) * config.stepWidth;
        const float width = config.stepWidth * static_cast<float>(juce::jmax(1, length));
        const int y = yForPitch(pitch);

        return juce::Rectangle<int>(static_cast<int>(std::floor(x + 1.0f)),
                                    y + 1,
                                    static_cast<int>(std::ceil(juce::jmax(3.0f, width - 2.0f))),
                                    juce::jmax(6, config.rowHeight - 2));
    }

private:
    int pitchRows() const
    {
        return (config.maxPitch - config.minPitch + 1);
    }

    Config config;
};
} // namespace bbg