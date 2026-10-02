#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg
{
class DragGestureButton : public juce::TextButton
{
public:
    explicit DragGestureButton(const juce::String& text = {})
        : juce::TextButton(text)
    {
    }

    std::function<void()> onClickAction;
    std::function<void()> onDragAction;

    // Optional pictogram drawn instead of the text: a note for MIDI, a waveform for audio.
    enum class Icon
    {
        None,
        Midi,
        Wave
    };

    void setIcon(Icon newIcon)
    {
        icon = newIcon;
        repaint();
    }

protected:
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        if (icon == Icon::None)
        {
            juce::TextButton::paintButton(g, highlighted, down);
            return;
        }

        getLookAndFeel().drawButtonBackground(g, *this, findColour(buttonColourId), highlighted, down);

        const float boxH = juce::jlimit(8.0f, 18.0f, getHeight() - 4.0f);
        const auto box = getLocalBounds().toFloat().withSizeKeepingCentre(juce::jmin(getWidth() - 8.0f, boxH * 1.7f), boxH);
        const auto colour = (isEnabled() ? juce::Colours::white : juce::Colours::grey).withAlpha(down ? 0.75f : 0.95f);
        g.setColour(colour);
        juce::Path path;
        if (icon == Icon::Midi)
        {
            // Two beamed eighth notes, heads large enough to read at small row heights.
            const float h = box.getHeight();
            const float headW = h * 0.62f;
            const float headH = h * 0.46f;
            const float x1 = box.getX() + box.getWidth() * 0.10f;
            const float x2 = box.getX() + box.getWidth() * 0.56f;
            const float headTop = box.getBottom() - headH;
            path.addEllipse(x1, headTop, headW, headH);
            path.addEllipse(x2, headTop, headW, headH);
            const float stemW = juce::jmax(1.3f, h * 0.12f);
            const float stemTop = box.getY();
            const float stemBottom = headTop + headH * 0.5f;
            path.addRectangle(x1 + headW - stemW, stemTop, stemW, stemBottom - stemTop);
            path.addRectangle(x2 + headW - stemW, stemTop, stemW, stemBottom - stemTop);
            path.addRectangle(x1 + headW - stemW, stemTop, x2 - x1 + stemW, juce::jmax(2.0f, h * 0.22f));
            g.fillPath(path);
        }
        else
        {
            // Waveform bars.
            static constexpr float heights[] { 0.35f, 0.7f, 1.0f, 0.55f, 0.85f, 0.45f, 0.25f };
            const int bars = static_cast<int>(std::size(heights));
            const float step = box.getWidth() / static_cast<float>(bars);
            for (int i = 0; i < bars; ++i)
            {
                const float barH = box.getHeight() * heights[i];
                path.addRoundedRectangle(box.getX() + i * step + step * 0.22f, box.getCentreY() - barH * 0.5f,
                                         step * 0.56f, barH, step * 0.25f);
            }
            g.fillPath(path);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragStarted = false;
        dragOrigin = event.getMouseDownPosition();
        juce::TextButton::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        juce::TextButton::mouseDrag(event);

        if (dragStarted)
            return;

        const auto delta = event.getPosition() - dragOrigin;
        if (delta.getDistanceFromOrigin() < 6)
            return;

        dragStarted = true;
        if (onDragAction)
            onDragAction();
    }

    void clicked() override
    {
        if (dragStarted)
            return;

        if (onClickAction)
            onClickAction();
    }

private:
    Icon icon = Icon::None;
    bool dragStarted = false;
    juce::Point<int> dragOrigin;
};
} // namespace bbg
