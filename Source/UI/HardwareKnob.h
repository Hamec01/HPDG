#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg
{
namespace hardware_knob
{
inline juce::Colour shellBase() { return juce::Colour::fromRGB(8, 8, 8); }
inline juce::Colour shellRaised() { return juce::Colour::fromRGB(16, 14, 12); }
inline juce::Colour panelBase() { return juce::Colour::fromRGB(24, 21, 18); }
inline juce::Colour panelRaised() { return juce::Colour::fromRGB(34, 29, 25); }
inline juce::Colour panelInset() { return juce::Colour::fromRGB(12, 11, 10); }
inline juce::Colour copper() { return juce::Colour::fromRGB(185, 118, 61); }
inline juce::Colour amber() { return juce::Colour::fromRGB(232, 176, 96); }
inline juce::Colour amberBright() { return juce::Colour::fromRGB(252, 214, 143); }
inline juce::Colour steel() { return juce::Colour::fromRGB(179, 184, 190); }
inline juce::Colour textMain() { return juce::Colour::fromRGB(233, 226, 214); }
inline juce::Colour textMuted() { return juce::Colour::fromRGB(145, 136, 124); }

struct LookAndFeel final : juce::LookAndFeel_V4
{
    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)).reduced(6.0f);
        const float diameter = std::min(bounds.getWidth(), bounds.getHeight());
        bounds.setSize(diameter, diameter);
        bounds.setCentre(static_cast<float>(x) + static_cast<float>(width) * 0.5f,
                         static_cast<float>(y) + static_cast<float>(height) * 0.5f);

        const auto outline = slider.findColour(juce::Slider::rotarySliderOutlineColourId);
        const auto fill = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto thumb = slider.findColour(juce::Slider::thumbColourId);
        const auto track = slider.findColour(juce::Slider::trackColourId);
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const auto centre = bounds.getCentre();

        g.setColour(juce::Colours::black.withAlpha(0.34f));
        g.fillEllipse(bounds.translated(0.0f, 3.0f));

        juce::ColourGradient rim(shellRaised().brighter(0.20f), bounds.getX(), bounds.getY(), shellBase(), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(rim);
        g.fillEllipse(bounds);
        g.setColour(outline);
        g.drawEllipse(bounds, 1.2f);

        auto inner = bounds.reduced(diameter * 0.10f);
        juce::ColourGradient body(panelRaised().brighter(0.18f), inner.getX(), inner.getY(), panelInset(), inner.getX(), inner.getBottom(), false);
        body.addColour(0.42, panelBase());
        g.setGradientFill(body);
        g.fillEllipse(inner);

        auto highlight = inner.reduced(inner.getWidth() * 0.14f);
        highlight.setHeight(highlight.getHeight() * 0.44f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillEllipse(highlight);

        juce::Path trackPath;
        trackPath.addCentredArc(centre.x,
                                centre.y,
                                inner.getWidth() * 0.52f,
                                inner.getHeight() * 0.52f,
                                0.0f,
                                rotaryStartAngle,
                                rotaryEndAngle,
                                true);
        g.setColour(track.withAlpha(0.20f));
        g.strokePath(trackPath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valuePath;
        valuePath.addCentredArc(centre.x,
                                centre.y,
                                inner.getWidth() * 0.52f,
                                inner.getHeight() * 0.52f,
                                0.0f,
                                rotaryStartAngle,
                                angle,
                                true);
        g.setColour(fill.withAlpha(0.96f));
        g.strokePath(valuePath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path pointer;
        pointer.addRoundedRectangle(-1.7f, -inner.getHeight() * 0.31f, 3.4f, inner.getHeight() * 0.23f, 1.4f);
        g.setColour(thumb);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));

        g.setColour(juce::Colours::black.withAlpha(0.34f));
        g.fillEllipse(centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
        g.setColour(thumb.withAlpha(0.92f));
        g.fillEllipse(centre.x - 2.7f, centre.y - 2.7f, 5.4f, 5.4f);
    }
};

inline LookAndFeel lookAndFeel;
} // namespace hardware_knob

class RotaryKnobSlider : public juce::Slider
{
public:
    RotaryKnobSlider()
    {
        setLookAndFeel(&hardware_knob::lookAndFeel);
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setMouseDragSensitivity(240);
        setRotaryParameters(juce::MathConstants<float>::pi * 1.15f,
                            juce::MathConstants<float>::pi * 2.85f,
                            true);
        setWantsKeyboardFocus(false);
        setColour(juce::Slider::rotarySliderOutlineColourId, hardware_knob::copper().withAlpha(0.62f));
        setColour(juce::Slider::rotarySliderFillColourId, hardware_knob::amber().withAlpha(0.96f));
        setColour(juce::Slider::trackColourId, hardware_knob::steel().withAlpha(0.48f));
        setColour(juce::Slider::thumbColourId, hardware_knob::amberBright());
    }

    ~RotaryKnobSlider() override
    {
        setLookAndFeel(nullptr);
    }

    void setPopupTitle(const juce::String& title)
    {
        popupTitle = title;
    }

    bool isPointOverActiveZone(juce::Point<float> position) const
    {
        return isPointOverKnob(position);
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (!isPointOverKnob(event.position))
            return;

        juce::Slider::mouseWheelMove(event, wheel);
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (event.mods.isLeftButtonDown() && isPointOverKnob(event.position))
        {
            showNumericPopup();
            return;
        }

        juce::Slider::mouseDoubleClick(event);
    }

private:
    class NumericPopupContent final : public juce::Component
    {
    public:
        explicit NumericPopupContent(RotaryKnobSlider& ownerIn)
            : owner(&ownerIn)
        {
            titleLabel.setText(ownerIn.popupTitle.isNotEmpty() ? ownerIn.popupTitle : "Value", juce::dontSendNotification);
            titleLabel.setJustificationType(juce::Justification::centredLeft);
            titleLabel.setColour(juce::Label::textColourId, hardware_knob::textMain());
            addAndMakeVisible(titleLabel);

            valueEditor.setText(ownerIn.getTextFromValue(ownerIn.getValue()), juce::dontSendNotification);
            valueEditor.setJustification(juce::Justification::centred);
            valueEditor.setPopupMenuEnabled(false);
            valueEditor.setSelectAllWhenFocused(true);
            valueEditor.setColour(juce::TextEditor::backgroundColourId, hardware_knob::panelBase());
            valueEditor.setColour(juce::TextEditor::outlineColourId, hardware_knob::copper().withAlpha(0.55f));
            valueEditor.setColour(juce::TextEditor::focusedOutlineColourId, hardware_knob::amber());
            valueEditor.setColour(juce::TextEditor::textColourId, hardware_knob::textMain());
            valueEditor.onReturnKey = [safe = juce::Component::SafePointer<NumericPopupContent>(this)]
            {
                if (safe != nullptr)
                    safe->commitAndDismiss();
            };
            valueEditor.onEscapeKey = [safe = juce::Component::SafePointer<NumericPopupContent>(this)]
            {
                if (safe != nullptr)
                    safe->dismissPopup();
            };
            addAndMakeVisible(valueEditor);

            applyButton.setColour(juce::TextButton::buttonColourId, hardware_knob::amber());
            applyButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(18, 16, 14));
            applyButton.onClick = [safe = juce::Component::SafePointer<NumericPopupContent>(this)]
            {
                if (safe != nullptr)
                    safe->commitAndDismiss();
            };
            addAndMakeVisible(applyButton);

            cancelButton.setColour(juce::TextButton::buttonColourId, hardware_knob::panelRaised());
            cancelButton.setColour(juce::TextButton::textColourOffId, hardware_knob::textMain());
            cancelButton.onClick = [safe = juce::Component::SafePointer<NumericPopupContent>(this)]
            {
                if (safe != nullptr)
                    safe->dismissPopup();
            };
            addAndMakeVisible(cancelButton);

            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<NumericPopupContent>(this)]
            {
                if (safe != nullptr)
                {
                    safe->valueEditor.grabKeyboardFocus();
                    safe->valueEditor.selectAll();
                }
            });
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(hardware_knob::panelInset());
            g.fillRoundedRectangle(bounds, 9.0f);
            g.setColour(hardware_knob::copper().withAlpha(0.55f));
            g.drawRoundedRectangle(bounds, 9.0f, 1.0f);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(10);
            titleLabel.setBounds(area.removeFromTop(16));
            area.removeFromTop(6);
            valueEditor.setBounds(area.removeFromTop(24));
            area.removeFromTop(8);
            auto buttonRow = area.removeFromTop(24);
            cancelButton.setBounds(buttonRow.removeFromRight(58));
            buttonRow.removeFromRight(4);
            applyButton.setBounds(buttonRow.removeFromRight(64));
        }

    private:
        void commitAndDismiss()
        {
            if (owner == nullptr)
            {
                dismissPopup();
                return;
            }

            const auto text = valueEditor.getText().trim();
            if (text.isNotEmpty())
            {
                const double clampedValue = juce::jlimit(owner->getMinimum(), owner->getMaximum(), owner->getValueFromText(text));
                owner->setValue(clampedValue, juce::sendNotificationSync);
            }

            dismissPopup();
        }

        void dismissPopup()
        {
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
                callout->dismiss();
        }

        juce::Component::SafePointer<RotaryKnobSlider> owner;
        juce::Label titleLabel;
        juce::TextEditor valueEditor;
        juce::TextButton applyButton { "Apply" };
        juce::TextButton cancelButton { "Cancel" };
    };

    juce::Rectangle<float> getInteractiveKnobBounds() const
    {
        auto bounds = getLocalBounds().toFloat().reduced(4.0f);
        const float diameter = std::min(bounds.getWidth(), bounds.getHeight());
        bounds.setSize(diameter, diameter);
        bounds.setCentre(getLocalBounds().toFloat().getCentre());
        return bounds;
    }

    bool isPointOverKnob(juce::Point<float> position) const
    {
        const auto knobBounds = getInteractiveKnobBounds();
        const auto radius = knobBounds.getWidth() * 0.5f;
        const auto centre = knobBounds.getCentre();
        const auto dx = position.x - centre.x;
        const auto dy = position.y - centre.y;
        return (dx * dx) + (dy * dy) <= radius * radius;
    }

    void showNumericPopup()
    {
        auto content = std::make_unique<NumericPopupContent>(*this);
        content->setSize(188, 92);
        auto& callout = juce::CallOutBox::launchAsynchronously(std::move(content),
                                                               localAreaToGlobal(getInteractiveKnobBounds().getSmallestIntegerContainer()),
                                                               nullptr);
        callout.setDismissalMouseClicksAreAlwaysConsumed(false);
    }

    juce::String popupTitle;
};
} // namespace bbg