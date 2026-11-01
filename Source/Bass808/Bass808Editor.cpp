#include "Bass808Editor.h"

#include "Bass808/BassSettingsIO.h"

#include "UI/SketchTheme.h"

namespace bbg
{
namespace
{
using namespace b808;
using Item = Page::Item;

constexpr int kWidth = 1000, kHeight = 650;
constexpr int kHeaderHeight = 64, kTabTop = 70, kTabHeight = 28, kScreenTop = 104, kScreenHeight = 262, kPageTop = 378;

bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;aif;aiff;flac;mp3;ogg");
}
} // namespace

// Holds a note while pressed: hear the sound without a MIDI keyboard (the last played note, C2 at first).
class Bass808Editor::PlayButton final : public juce::TextButton
{
public:
    explicit PlayButton(Bass808Processor& processor) : juce::TextButton("PLAY"), bass(processor) {}
    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::TextButton::mouseDown(event);
        note = bass.lastNote();
        bass.keyboardState.noteOn(1, note, 0.9f);
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::TextButton::mouseUp(event);
        bass.keyboardState.noteOff(1, note, 0.0f);
    }

private:
    Bass808Processor& bass;
    int note = 36;
};

Bass808Editor::Bass808Editor(Bass808Processor& processor)
    : juce::AudioProcessorEditor(processor), bass(processor), screen(processor)
{
#if !HPDG_BASS808_EMBEDDED // inside HPDG the skin stays HPDG's own
    sketch::setActiveSkin(sketch::Skin::Dark); // HPDG's knobs take the Dark skin's colours
#endif
    setLookAndFeel(&look);

    fillPresetBox();
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= kUserPresetId && id - kUserPresetId < userPresets.size())
        {
            if (const auto preset = readBassPreset(userPresets[id - kUserPresetId]))
                bass.applyUserPreset(*preset);
        }
        else if (id > 0)
        {
            bass.applyPreset(id - 1);
        }
        shownPreset = bass.getCurrentProgram();
    };
    previousPreset.onClick = [this] { stepPreset(-1); };
    nextPreset.onClick = [this] { stepPreset(1); };
    saveButton.setTooltip("Save this sound as a preset (Documents/HamloProd/Bass808 Presets, shared with HPDG)");
    saveButton.onClick = [this] { savePreset(); };
    addAndMakeVisible(presetBox);
    addAndMakeVisible(previousPreset);
    addAndMakeVisible(nextPreset);
    addAndMakeVisible(saveButton);
    nameEditor.setTextToShowWhenEmpty("preset name", juce::Colours::grey);
    nameEditor.onReturnKey = [this] { writePreset(nameEditor.getText()); };
    nameEditor.onEscapeKey = [this] { endNaming(); };
    addChildComponent(nameEditor);

    mode = std::make_unique<ParamCombo>(bass.apvts, "mode", "Mode");
    addAndMakeVisible(*mode);
    play = std::make_unique<PlayButton>(bass);
    addAndMakeVisible(*play);
    for (const auto& [id, text] : { std::pair { "glide", "Glide" }, { "velocity", "Velocity" }, { "tune", "Tune" }, { "gain", "Gain" } })
    {
        headerKnobs.push_back(std::make_unique<ParamKnob>(bass.apvts, id, text, 34));
        addAndMakeVisible(*headerKnobs.back());
    }

    static const char* tabNames[] { "SYNTH", "SAMPLE", "MIX", "FILTER  /  FX" };
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        tabs[i].setButtonText(tabNames[i]);
        tabs[i].setRadioGroupId(808);
        tabs[i].setClickingTogglesState(true);
        tabs[i].onClick = [this, i] { selectTab(static_cast<int>(i)); };
        addAndMakeVisible(tabs[i]);
    }
    addAndMakeVisible(screen);

    auto& state = bass.apvts;
    pages[0] = std::make_unique<Page>(state, std::vector<Page::Group> {
        { "OSCILLATOR", 2, { { Item::Combo, "wave", "Wave" }, { Item::Knob, "shape", "Shape" }, { Item::Knob, "synthLevel", "Level" },
                             { Item::Combo, "subOct", "Sub Octave" }, { Item::Knob, "sub", "Sub" } } },
        { "PITCH", 2, { { Item::Knob, "punch", "Punch" }, { Item::Knob, "punchTime", "Punch Time" } } },
        { "808 TONE", 2, { { Item::Knob, "drive", "Drive" }, { Item::Knob, "even", "Even" }, { Item::Knob, "saturation", "Saturation" },
                           { Item::Knob, "tone", "Tone" }, { Item::Knob, "click", "Click" }, { Item::Knob, "boost", "Boost" } } },
        { "ENVELOPE", 2, { { Item::Knob, "attack", "Attack" }, { Item::Knob, "hold", "Hold" }, { Item::Knob, "decay", "Decay" },
                           { Item::Knob, "sustain", "Sustain" }, { Item::Knob, "release", "Release" } } },
    });
    pages[1] = std::make_unique<Page>(state, std::vector<Page::Group> {
        { "SAMPLE", 2, { { Item::Knob, "smpLevel", "Level" }, { Item::Knob, "smpTune", "Tune" }, { Item::Knob, "smpFine", "Fine" },
                         { Item::Knob, "smpStretch", "Stretch" }, { Item::Knob, "smpStart", "Start" }, { Item::Knob, "smpEnd", "End" },
                         { Item::Knob, "smpLoopStart", "Loop Start" } } },
        { "FILE", 2, { { Item::Action, nullptr, "LOAD", [this] { chooseSample(); } }, { Item::Action, nullptr, "CLEAR", [this] { bass.clearSample(); } } } },
        { "PLAYBACK", 2, { { Item::Toggle, "smpLoop", "LOOP" }, { Item::Toggle, "smpFollow", "FOLLOW KEY" }, { Item::Toggle, "smpLock", "LOCK TIME" },
                           { Item::Toggle, "smpReverse", "REVERSE" }, { Item::Toggle, "smpInvert", "INVERT" } } },
    });
    pages[2] = std::make_unique<Page>(state, std::vector<Page::Group> {
        { "LAYERS", 1, { { Item::Knob, "synthLevel", "Synth" }, { Item::Knob, "smpLevel", "Sample" }, { Item::Knob, "handoff", "Handoff" },
                         { Item::Toggle, "smpInvert", "INVERT" } } },
        { "WEIGHT", 1, { { Item::Knob, "sub", "Sub" }, { Item::Knob, "phantom", "Phantom" } } },
        { "ENVELOPE", 1, { { Item::Knob, "attack", "Attack" }, { Item::Knob, "hold", "Hold" }, { Item::Knob, "decay", "Decay" },
                           { Item::Knob, "sustain", "Sustain" }, { Item::Knob, "release", "Release" } } },
    });
    pages[3] = std::make_unique<Page>(state, std::vector<Page::Group> {
        { "FILTER", 2, { { Item::Combo, "fltType", "Type" }, { Item::Knob, "cutoff", "Cutoff" }, { Item::Knob, "reso", "Resonance" },
                         { Item::Knob, "fltKey", "Key" }, { Item::Knob, "fltEnv", "Env" }, { Item::Knob, "fltDecay", "Env Decay" },
                         { Item::Knob, "vowel", "Vowel" } } },
        { "LFO", 2, { { Item::Knob, "lfoRate", "Rate" }, { Item::Combo, "lfoSync", "Sync" }, { Item::Knob, "lfoDepth", "Depth" },
                      { Item::Toggle, "lfoRetrig", "RETRIG" } } },
        { "HAMLOMAG SATURATOR", 2, { { Item::Combo, "satModel", "Model" }, { Item::Knob, "satDrive", "Drive" }, { Item::Knob, "satHarm", "Harmonics" },
                                     { Item::Knob, "satEvenOdd", "Even/Odd" }, { Item::Knob, "satMix", "Mix" }, { Item::Knob, "phantom", "Phantom" } } },
    });
    for (auto& page : pages)
        addChildComponent(*page);

    tabs[0].setToggleState(true, juce::dontSendNotification);
    selectTab(0);
    setSize(kWidth, kHeight);
    startTimerHz(10);
}

Bass808Editor::~Bass808Editor()
{
    setLookAndFeel(nullptr);
}

void Bass808Editor::showTab(int index)
{
    index = juce::jlimit(0, static_cast<int>(tabs.size()) - 1, index);
    tabs[static_cast<size_t>(index)].setToggleState(true, juce::dontSendNotification);
    selectTab(index);
}

void Bass808Editor::selectTab(int index)
{
    for (size_t i = 0; i < pages.size(); ++i)
        pages[i]->setVisible(static_cast<int>(i) == index);
    screen.setMode(static_cast<Bass808Screen::Mode>(index));
}

void Bass808Editor::fillPresetBox()
{
    // factory presets grouped by genre, then the user's own (the shared preset folder)
    presetBox.clear(juce::dontSendNotification);
    const auto& presets = bassPresets();
    juce::String genre;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (genre != presets[i].genre)
        {
            genre = presets[i].genre;
            presetBox.addSectionHeading(genre);
        }
        presetBox.addItem(juce::String(presets[i].genre) + "  " + presets[i].name, static_cast<int>(i) + 1);
    }
    userPresets = bassUserPresetFiles();
    if (!userPresets.isEmpty())
        presetBox.addSectionHeading("User");
    for (int i = 0; i < userPresets.size(); ++i)
        presetBox.addItem("User  " + userPresets[i].getFileNameWithoutExtension(), kUserPresetId + i);
}

void Bass808Editor::savePreset()
{
    // the name is typed in place of the preset list (no popup window: inside a host one can open
    // behind the plugin and the save never happens)
    if (!naming)
    {
        naming = true;
        nameEditor.setText(bass.presetName(), juce::dontSendNotification);
        nameEditor.setBounds(presetBox.getBounds());
        presetBox.setVisible(false);
        nameEditor.setVisible(true);
        saveButton.setButtonText("OK");
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
        return;
    }
    writePreset(nameEditor.getText());
}

void Bass808Editor::endNaming()
{
    naming = false;
    nameEditor.setVisible(false);
    presetBox.setVisible(true);
    saveButton.setButtonText("SAVE");
}

void Bass808Editor::writePreset(const juce::String& typed)
{
    const auto name = juce::File::createLegalFileName(typed.trim());
    endNaming();
    if (name.isEmpty())
        return;
    BassPresetFile preset;
    preset.name = name;
    preset.settings = bass.currentSettings();
    if (preset.settings.sampleLevelDb > -59.5f)
        preset.sample = bass.getSample();
    const auto file = bassUserPresetFolder().getChildFile(name + ".hpbass");
    if (writeBassPreset(file, preset))
    {
        bass.setUserPresetName(name);
        fillPresetBox();
        const int index = userPresets.indexOf(file);
        if (index >= 0)
            presetBox.setSelectedId(kUserPresetId + index, juce::dontSendNotification);
        message = "Saved: " + name;
    }
    else
    {
        message = "Could not write " + file.getFullPathName();
    }
    messageTicks = 40;
    repaint();
}

void Bass808Editor::stepPreset(int delta)
{
    const int count = bass.getNumPrograms();
    bass.applyPreset((bass.getCurrentProgram() + delta + count) % count);
}

void Bass808Editor::timerCallback()
{
    if (bass.isUserPreset())
    {
        // a user preset, a lane's own sound: its name (the list entry when it is one of the user files)
        if (presetBox.getSelectedId() < kUserPresetId && presetBox.getText() != bass.presetName())
            presetBox.setText(bass.presetName(), juce::dontSendNotification);
    }
    else if (bass.getCurrentProgram() != shownPreset || presetBox.getSelectedId() != shownPreset + 1)
    {
        shownPreset = bass.getCurrentProgram();
        presetBox.setSelectedId(shownPreset + 1, juce::dontSendNotification);
    }
    if (messageTicks > 0 && --messageTicks == 0)
        repaint();
}

void Bass808Editor::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser>("Load a sample into HPDG 808", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc)
                         {
                             if (fc.getResult().existsAsFile())
                                 loadSample(fc.getResult());
                         });
}

void Bass808Editor::loadSample(const juce::File& file)
{
    juce::String error;
    if (bass.loadSample(file, error))
    {
        message = "Loaded " + file.getFileName();
        tabs[1].setToggleState(true, juce::dontSendNotification);
        selectTab(1);
    }
    else
    {
        message = error;
    }
    messageTicks = 40;
    repaint();
}

bool Bass808Editor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && isAudioFile(files[0]);
}

void Bass808Editor::fileDragEnter(const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void Bass808Editor::fileDragExit(const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void Bass808Editor::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (!files.isEmpty())
        loadSample(juce::File(files[0]));
}

void Bass808Editor::paint(juce::Graphics& g)
{
    g.fillAll(colour::background);
    // header strip
    auto header = getLocalBounds().removeFromTop(kHeaderHeight).toFloat();
    juce::ColourGradient band(colour::panel, 0.0f, 0.0f, colour::background, 0.0f, header.getBottom(), false);
    g.setGradientFill(band);
    g.fillRect(header);
    g.setColour(colour::panelEdge);
    g.drawHorizontalLine(kHeaderHeight - 1, 0.0f, static_cast<float>(getWidth()));

    g.setColour(colour::accentGlow);
    g.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    g.drawText("HPDG 808", 18, 10, 150, 30, juce::Justification::centredLeft);
    g.setColour(colour::textMuted);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("BASS INSTRUMENT  /  HAMLOPROD", 19, 38, 200, 14, juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(11.5f));
    g.drawText("PRESET", 196, 8, 60, 14, juce::Justification::centredLeft);

    if (messageTicks > 0)
    {
        g.setColour(colour::tealBright);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(message, 560, kTabTop, kWidth - 572, kTabHeight, juce::Justification::centredRight);
    }
}

void Bass808Editor::paintOverChildren(juce::Graphics& g)
{
    if (!dragOver)
        return;
    g.setColour(colour::accent.withAlpha(0.18f));
    g.fillRect(getLocalBounds());
    g.setColour(colour::accentGlow);
    g.drawRect(getLocalBounds(), 3);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("DROP TO LOAD INTO THE SAMPLE LAYER", getLocalBounds(), juce::Justification::centred);
}

void Bass808Editor::resized()
{
    presetBox.setBounds(196, 24, 206, 28);
    previousPreset.setBounds(406, 24, 26, 28);
    nextPreset.setBounds(434, 24, 26, 28);
    saveButton.setBounds(464, 24, 50, 28);
    mode->setBounds(522, 6, 108, 50);
    play->setBounds(642, 24, 64, 28);
    int x = kWidth - 12 - static_cast<int>(headerKnobs.size()) * 66;
    for (auto& knob : headerKnobs)
    {
        knob->setBounds(x, 2, 66, 60);
        x += 66;
    }
    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i].setBounds(12 + static_cast<int>(i) * 136, kTabTop, 130, kTabHeight);
    screen.setBounds(12, kScreenTop, kWidth - 24, kScreenHeight);
    for (auto& page : pages)
        page->setBounds(12, kPageTop, kWidth - 24, kHeight - kPageTop - 8);
}
} // namespace bbg
