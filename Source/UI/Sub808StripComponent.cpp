#include "Sub808StripComponent.h"

#include <cmath>

#include "UI/SketchTheme.h"

namespace bbg
{
namespace
{
juce::String hzText(double hz)
{
    return hz >= 1000.0 ? juce::String(hz / 1000.0, hz >= 10000.0 ? 0 : 1) + "k" : juce::String(juce::roundToInt(hz));
}

// the strip's filter position as the cutoff it sets (the engine's busFilter mapping)
juce::String filterText(double position)
{
    if (std::abs(position) < 0.01)
        return "off";
    if (position < 0.0)
        return "LP " + hzText(20000.0 * std::pow(200.0 / 20000.0, -position));
    return "HP " + hzText(20.0 * std::pow(400.0 / 20.0, position));
}

const juce::StringArray kModels { "Classic", "Tape", "Tube", "Iron", "Germanium", "Vinyl" };
} // namespace

Sub808StripComponent::Sub808StripComponent()
{
    setupKnob(filter, "FILTER", -1.0, 1.0, 0.0);
    setupKnob(drive, "DRIVE", 0.0, 1.0, 0.0);
    setupKnob(length, "LENGTH", 0.0, 1.0, 1.0);
    setupKnob(gain, "GAIN", -24.0, 12.0, 0.0);
    gain.slider.setSkewFactorFromMidPoint(0.0);
    setupKnob(sidechain, "SIDECHAIN", 0.0, 1.0, 0.0);
    sidechain.slider.setTooltip("Each kick ducks the bass (deeper = longer recovery)");

    driveModel.addItemList(kModels, 1);
    driveModel.setSelectedId(2, juce::dontSendNotification);
    driveModel.setTooltip("HamloMag model for DRIVE, when the sound has no saturator of its own");
    driveModel.onChange = [this] { knobMoved(); };
    addChildComponent(driveModel);

    editButton.setTooltip("Open HPDG 808: make a new bass, edit this one, save it as a preset");
    editButton.onClick = [this] { if (onEdit) onEdit(); };
    addChildComponent(editButton);

    soundLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    soundLabel.setJustificationType(juce::Justification::centredLeft);
    soundLabel.setColour(juce::Label::textColourId, sketch::Theme::graphite());
    addChildComponent(soundLabel);

    setRepaintsOnMouseActivity(true);
    updateValueTexts();
}

void Sub808StripComponent::setupKnob(Knob& knob, const juce::String& caption, double min, double max, double reset)
{
    knob.slider.setRange(min, max, 0.0);
    knob.slider.setValue(reset, juce::dontSendNotification);
    knob.slider.setDoubleClickReturnValue(true, reset);
    knob.slider.setPopupTitle(caption);
    knob.slider.onValueChange = [this] { knobMoved(); };
    knob.caption.setText(caption, juce::dontSendNotification);
    knob.caption.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    knob.caption.setJustificationType(juce::Justification::centred);
    knob.caption.setColour(juce::Label::textColourId, sketch::Theme::graphiteSoft());
    knob.value.setFont(juce::Font(juce::FontOptions(11.0f)));
    knob.value.setJustificationType(juce::Justification::centred);
    knob.value.setColour(juce::Label::textColourId, sketch::Theme::graphite());
    knob.caption.setInterceptsMouseClicks(false, false);
    knob.value.setInterceptsMouseClicks(false, false);
    addChildComponent(knob.slider);
    addChildComponent(knob.caption);
    addChildComponent(knob.value);
}

void Sub808StripComponent::setOpen(bool shouldBeOpen)
{
    if (open == shouldBeOpen)
        return;
    open = shouldBeOpen;
    for (auto* knob : { &filter, &drive, &length, &gain, &sidechain })
    {
        knob->slider.setVisible(open);
        knob->caption.setVisible(open);
        knob->value.setVisible(open);
    }
    driveModel.setVisible(open);
    editButton.setVisible(open);
    soundLabel.setVisible(open);
    resized();
    repaint();
    if (onOpenChanged)
        onOpenChanged(open);
}

void Sub808StripComponent::setState(const Sub808SoundState& newState, const juce::String& soundName)
{
    state = newState;
    const juce::ScopedValueSetter<bool> guard(updating, true);
    filter.slider.setValue(state.filter, juce::dontSendNotification);
    drive.slider.setValue(state.drive, juce::dontSendNotification);
    length.slider.setValue(state.length, juce::dontSendNotification);
    gain.slider.setValue(state.gainDb, juce::dontSendNotification);
    sidechain.slider.setValue(state.sidechain, juce::dontSendNotification);
    driveModel.setSelectedId(juce::jlimit(1, 6, state.driveModel), juce::dontSendNotification);
    soundLabel.setText("808  " + soundName, juce::dontSendNotification);
    updateValueTexts();
}

void Sub808StripComponent::knobMoved()
{
    updateValueTexts();
    if (updating)
        return;
    state.filter = static_cast<float>(filter.slider.getValue());
    state.drive = static_cast<float>(drive.slider.getValue());
    state.length = static_cast<float>(length.slider.getValue());
    state.gainDb = static_cast<float>(gain.slider.getValue());
    state.sidechain = static_cast<float>(sidechain.slider.getValue());
    state.driveModel = juce::jlimit(1, 6, driveModel.getSelectedId());
    if (onChange)
        onChange(state);
}

void Sub808StripComponent::updateValueTexts()
{
    filter.value.setText(filterText(filter.slider.getValue()), juce::dontSendNotification);
    const double d = drive.slider.getValue();
    drive.value.setText(d < 0.005 ? "off" : juce::String(juce::roundToInt(d * 100.0)) + "%", juce::dontSendNotification);
    const double l = length.slider.getValue();
    length.value.setText(l >= 0.999 ? "full" : juce::String(0.04 * std::pow(100.0, l), 2) + " s", juce::dontSendNotification);
    const double g = gain.slider.getValue();
    gain.value.setText((g > 0.04 ? "+" : "") + juce::String(g, 1) + " dB", juce::dontSendNotification);
    const double s = sidechain.slider.getValue();
    sidechain.value.setText(s < 0.005 ? "off" : "-" + juce::String(30.0 * s, 0) + " dB", juce::dontSendNotification);
    const bool tinted = std::abs(filter.slider.getValue()) > 0.01;
    filter.value.setColour(juce::Label::textColourId, tinted ? sketch::Theme::ochreDeep() : sketch::Theme::graphite());
}

void Sub808StripComponent::paint(juce::Graphics& g)
{
    auto bar = arrowBar().toFloat();
    const bool hot = isMouseOver(true) && arrowBar().contains(getMouseXYRelative());
    const float midY = bar.getCentreY();

    // the thin line, the arrow on it
    g.setColour(sketch::Theme::ochre().withAlpha(hot ? 0.95f : 0.6f));
    g.fillRect(juce::Rectangle<float>(bar.getX() + 6.0f, midY - 1.0f, bar.getWidth() - 12.0f, 2.0f));
    const float cx = bar.getCentreX();
    auto pill = juce::Rectangle<float>(cx - 34.0f, bar.getY() + 1.0f, 68.0f, bar.getHeight() - 2.0f);
    g.setColour(sketch::Theme::paperLight());
    g.fillRoundedRectangle(pill, 6.0f);
    g.setColour(sketch::Theme::ochre().withAlpha(hot ? 1.0f : 0.75f));
    g.drawRoundedRectangle(pill, 6.0f, 1.2f);
    juce::Path arrow;
    const float ay = midY + (open ? 2.0f : -2.0f);
    const float dir = open ? -1.0f : 1.0f; // closed: points up (the panel opens upwards into view)
    arrow.addTriangle(cx - 22.0f, ay + 2.5f * dir, cx - 14.0f, ay + 2.5f * dir, cx - 18.0f, ay - 2.5f * dir);
    g.fillPath(arrow);
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText("808", pill.withTrimmedLeft(16.0f).toNearestInt(), juce::Justification::centred);

    if (!open)
        return;
    auto body = getLocalBounds().withTrimmedTop(kClosedHeight).reduced(4, 2).toFloat();
    g.setColour(sketch::Theme::paperDeep().withAlpha(0.55f));
    g.fillRoundedRectangle(body, 6.0f);
    g.setColour(sketch::Theme::ochre().withAlpha(0.45f));
    g.drawRoundedRectangle(body, 6.0f, 1.0f);
}

void Sub808StripComponent::resized()
{
    if (!open)
        return;
    auto body = getLocalBounds().withTrimmedTop(kClosedHeight).reduced(10, 6);
    auto right = body.removeFromRight(juce::jmin(150, body.getWidth() / 3));
    soundLabel.setBounds(right.removeFromTop(22));
    right.removeFromTop(4);
    driveModel.setBounds(right.removeFromTop(24));
    right.removeFromTop(8);
    editButton.setBounds(right.removeFromTop(30));

    const int cell = juce::jmin(110, body.getWidth() / 5);
    for (auto* knob : { &filter, &drive, &length, &gain, &sidechain })
    {
        auto column = body.removeFromLeft(cell);
        knob->caption.setBounds(column.removeFromTop(14));
        knob->value.setBounds(column.removeFromBottom(15));
        const int size = juce::jmin(column.getWidth() - 8, column.getHeight());
        knob->slider.setBounds(column.withSizeKeepingCentre(size, size));
    }
}

void Sub808StripComponent::mouseUp(const juce::MouseEvent& event)
{
    if (arrowBar().contains(event.getPosition()) && !event.mouseWasDraggedSinceMouseDown())
        setOpen(!open);
}
} // namespace bbg
