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

protected:
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
    bool dragStarted = false;
    juce::Point<int> dragOrigin;
};
} // namespace bbg
