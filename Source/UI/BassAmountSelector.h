#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "SketchTheme.h"

namespace bbg
{
// "How much bass" for the bass lane row: three buttons [1][2][3] next to the lane name.
// 1 Low (a few long notes), 2 More, 3 Full (plays all the way). The mouse wheel steps too.
class BassAmountSelector final : public juce::Component,
                                 public juce::SettableTooltipClient
{
public:
    static constexpr int kPositions = 3;

    BassAmountSelector()
    {
        setTooltip("Bass amount: 1 Low - a few long notes / 2 More / 3 Full - plays all the way.\n"
                   "Click a number: the bass line is generated again.");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    int getValue() const noexcept { return value; }

    void setValue(int newValue, juce::NotificationType notification)
    {
        newValue = juce::jlimit(0, kPositions - 1, newValue);
        if (newValue == value)
            return;
        value = newValue;
        repaint();
        if (notification != juce::dontSendNotification && onChange)
            onChange(value);
    }

    std::function<void(int)> onChange;

    void paint(juce::Graphics& g) override
    {
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        for (int i = 0; i < kPositions; ++i)
        {
            const auto box = segment(i).toFloat().reduced(1.0f);
            const bool selected = i == value;
            g.setColour(selected ? sketch::Theme::ochre() : hovered == i ? sketch::Theme::paper() : sketch::Theme::paperLight());
            g.fillRoundedRectangle(box, 2.5f);
            g.setColour(selected ? sketch::Theme::graphite() : sketch::Theme::graphiteSoft());
            g.drawRoundedRectangle(box, 2.5f, selected ? 1.4f : 1.0f);
            g.setColour(sketch::Theme::graphite());
            g.drawText(juce::String(i + 1), box, juce::Justification::centred, false);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override { setHovered(indexAt(e.x)); }
    void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }
    void mouseDown(const juce::MouseEvent& e) override { setValue(indexAt(e.x), juce::sendNotificationSync); }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const float delta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (std::abs(delta) > 1.0e-4f)
            setValue(value + (delta > 0.0f ? 1 : -1), juce::sendNotificationSync);
    }

private:
    juce::Rectangle<int> segment(int index) const
    {
        const int width = getWidth() / kPositions;
        const int height = juce::jmin(getHeight(), 18);
        return { index * width, (getHeight() - height) / 2, width, height };
    }

    int indexAt(int x) const
    {
        return juce::jlimit(0, kPositions - 1, x * kPositions / juce::jmax(1, getWidth()));
    }

    void setHovered(int index)
    {
        if (hovered != index)
        {
            hovered = index;
            repaint();
        }
    }

    int value = 0;
    int hovered = -1;
};
} // namespace bbg
