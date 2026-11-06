#include "Sub808EditorOverlay.h"

#include "Bass808/Bass808Editor.h"
#include "Bass808/Bass808Processor.h"
#include "Bass808/BassSettingsIO.h"
#include "Bass808/Sub808Sound.h"

namespace bbg
{
namespace
{
constexpr int kEditorWidth = 1000, kEditorHeight = 650, kTitleHeight = 34;
const juce::Colour kBackdrop { 0xc0000000 };
const juce::Colour kFrame { juce::Colour::fromRGB(32, 30, 32) };
const juce::Colour kAccent { juce::Colour::fromRGB(196, 72, 60) };
const juce::Colour kText { juce::Colour::fromRGB(196, 188, 176) };
} // namespace

Sub808EditorOverlay::Sub808EditorOverlay(Host hostCallbacks) : host(std::move(hostCallbacks))
{
    bass = std::make_unique<Bass808Processor>();
    loadFromLane();
    editor = std::make_unique<Bass808Editor>(*bass);
    addAndMakeVisible(*editor);
    if (!state.custom)
        editor->showTab(1); // the lane's sample is in the sample layer: show it
    bass->keyboardState.addListener(this);

    title.setText("HPDG 808  /  SUB808 LANE", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(title);

    laneSampleButton.setTooltip("Back to the lane's own sample (the < > keys on the lane browse them)");
    laneSampleButton.onClick = [this]
    {
        state.custom = false;
        if (host.setSound)
            host.setSound(state);
        loadFromLane();
        editor->showTab(1);
    };
    closeButton.onClick = [this] { timerCallback(); editor->stopAudition(); if (host.close) host.close(); };
    for (auto* button : { &laneSampleButton, &closeButton })
    {
        button->setColour(juce::TextButton::buttonColourId, kFrame.brighter(0.15f));
        button->setColour(juce::TextButton::textColourOffId, kText);
        addAndMakeVisible(*button);
    }

    setWantsKeyboardFocus(true);
    startTimerHz(15);
}

Sub808EditorOverlay::~Sub808EditorOverlay()
{
    stopTimer();
    if (editor != nullptr) editor->stopAudition();
    bass->keyboardState.removeListener(this);
    editor = nullptr; // before the processor it edits
    bass = nullptr;
}

void Sub808EditorOverlay::loadFromLane()
{
    state = host.getSound ? host.getSound() : Sub808SoundState {};
    if (state.custom)
    {
        std::shared_ptr<const BassSample> sample;
        if (state.sampleWav.getSize() > 0)
            sample = BassSample::fromWavMemory(state.sampleWav, state.name, {});
        bass->setSampleDirect(sample);
        bass->writeSettings(bassSettingsFromTree(state.settings));
        bass->setUserPresetName(state.name);
    }
    else
    {
        // the lane's bank sample in the sample layer, played as the lane plays it: a starting point
        bass->setSampleDirect(host.getBankSample ? host.getBankSample() : nullptr);
        bass->writeSettings(sub808SampleOnlySettings());
        bass->setUserPresetName(host.getBankSampleName ? host.getBankSampleName() : juce::String("Lane sample"));
    }
    // what is on screen now is what the lane plays: nothing goes back until the user changes something
    shownSettings = bass->currentSettings();
    shownSample = bass->getSample();
    shownName = bass->presetName();
}

void Sub808EditorOverlay::timerCallback()
{
    const auto settings = bass->currentSettings();
    const auto sample = bass->getSample();
    const auto name = bass->presetName();
    const bool settingsChanged = !bassSettingsToTree(settings).isEquivalentTo(bassSettingsToTree(shownSettings));
    if (!settingsChanged && sample == shownSample && name == shownName)
        return;

    state.custom = true;
    state.name = name;
    state.settings = bassSettingsToTree(settings);
    if (sample != shownSample || (state.sampleWav.getSize() == 0 && sample != nullptr))
        state.sampleWav = sample != nullptr ? sample->toWavMemory() : juce::MemoryBlock {};
    shownSettings = settings;
    shownSample = sample;
    shownName = name;
    if (host.setSound)
        host.setSound(state);
}

void Sub808EditorOverlay::handleNoteOn(juce::MidiKeyboardState*, int, int note, float velocity)
{
    timerCallback(); // the note plays what is on screen
    if (host.audition)
        host.audition(note, juce::jlimit(1, 127, juce::roundToInt(velocity * 127.0f)));
}

void Sub808EditorOverlay::handleNoteOff(juce::MidiKeyboardState*, int, int note, float)
{
    if (host.releaseAudition) host.releaseAudition(note);
}

juce::Rectangle<int> Sub808EditorOverlay::frameBounds() const
{
    const int width = juce::roundToInt(kEditorWidth * editorScale);
    const int height = juce::roundToInt(kEditorHeight * editorScale) + kTitleHeight;
    return getLocalBounds().withSizeKeepingCentre(width, height);
}

void Sub808EditorOverlay::paint(juce::Graphics& g)
{
    g.fillAll(kBackdrop);
    const auto frame = frameBounds().expanded(3).toFloat();
    g.setColour(kFrame);
    g.fillRoundedRectangle(frame, 6.0f);
    g.setColour(kAccent);
    g.drawRoundedRectangle(frame, 6.0f, 1.5f);
}

void Sub808EditorOverlay::resized()
{
    editorScale = juce::jlimit(0.4f, 1.0f, juce::jmin(static_cast<float>(getWidth() - 16) / kEditorWidth,
                                                      static_cast<float>(getHeight() - 16 - kTitleHeight) / kEditorHeight));
    auto frame = frameBounds();
    auto bar = frame.removeFromTop(kTitleHeight).reduced(8, 4);
    closeButton.setBounds(bar.removeFromRight(80));
    bar.removeFromRight(8);
    laneSampleButton.setBounds(bar.removeFromRight(120));
    title.setBounds(bar);
    if (editor != nullptr)
    {
        editor->setBounds(0, 0, kEditorWidth, kEditorHeight);
        editor->setTransform(juce::AffineTransform::scale(editorScale).translated(static_cast<float>(frame.getX()),
                                                                                  static_cast<float>(frame.getY())));
    }
}

bool Sub808EditorOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        timerCallback(); editor->stopAudition();
        if (host.close)
            host.close();
        return true;
    }
    return false;
}
} // namespace bbg
