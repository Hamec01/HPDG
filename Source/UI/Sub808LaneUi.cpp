#include "Sub808LaneUi.h"

#include "../Bass808/BassEngine.h"
#include "../Bass808/BassSettingsIO.h"
#include "../Core/TimingGrid.h"
#include "../Plugin/PluginProcessor.h"

namespace bbg
{
void showLaneSampleList(BoomBapGeneratorAudioProcessor& processor,
                        juce::Component& parent,
                        const RuntimeLaneId& laneId,
                        std::function<void()> refresh,
                        std::function<void()> openBassEditor)
{
    const auto project = processor.getProjectSnapshot();
    const TrackState* lane = nullptr;
    for (const auto& track : project.tracks)
        if (track.laneId == laneId)
            lane = &track;
    if (lane == nullptr)
        return;
    const auto type = lane->type;
    const bool bassLane = type == TrackType::Sub808;
    const auto sound = lane->sub808Sound;
    const auto names = processor.getLaneSampleNames(type);
    const int selected = processor.getSelectedLaneSampleIndex(type);

    static constexpr int kSampleId = 1, kFactoryId = 1000, kUserId = 2000, kDeleteId = 3000, kEditId = 4000;
    juce::PopupMenu menu;
    menu.addSectionHeader(bassLane ? "Lane samples" : "Samples");
    for (int i = 0; i < names.size(); ++i)
        menu.addItem(kSampleId + i, names[i], true, !(bassLane && sound.custom) && i == selected);
    if (names.isEmpty())
        menu.addItem(-1, "(no samples in this lane's folder)", false);

    const auto userFiles = bassUserPresetFiles();
    if (bassLane)
    {
        menu.addSectionHeader("HPDG 808");
        const auto& presets = bassPresets();
        juce::PopupMenu genreMenu;
        juce::String genre;
        for (size_t i = 0; i <= presets.size(); ++i)
        {
            if (i == presets.size() || genre != presets[i].genre)
            {
                if (genre.isNotEmpty())
                    menu.addSubMenu(genre, genreMenu);
                genreMenu = {};
                if (i == presets.size())
                    break;
                genre = presets[i].genre;
            }
            genreMenu.addItem(kFactoryId + static_cast<int>(i), presets[i].name, true, sound.custom && sound.name == presets[i].name);
        }
        juce::PopupMenu mine, remove;
        for (int i = 0; i < userFiles.size(); ++i)
        {
            const auto name = userFiles[i].getFileNameWithoutExtension();
            mine.addItem(kUserId + i, name, true, sound.custom && sound.name == name);
            remove.addItem(kDeleteId + i, name);
        }
        if (userFiles.isEmpty())
            mine.addItem(-1, "(none yet: SAVE in the editor)", false);
        menu.addSubMenu("My basses", mine);
        menu.addSubMenu("Delete my bass", remove, !userFiles.isEmpty());
        menu.addSeparator();
        menu.addItem(kEditId, "Edit in HPDG 808...");
    }

    juce::Component::SafePointer<juce::Component> safeParent(&parent);
    menu.showMenuAsync(juce::PopupMenu::Options().withParentComponent(&parent).withMousePosition(),
                       [&processor, safeParent, type, sound, userFiles, refresh, openBassEditor](int choice)
    {
        if (safeParent == nullptr || choice <= 0)
            return;
        if (choice < kFactoryId)
        {
            processor.selectLaneSampleIndex(type, choice - kSampleId);
            refresh();
            return;
        }
        if (choice == kEditId)
        {
            openBassEditor();
            return;
        }
        if (choice >= kDeleteId)
        {
            const auto file = userFiles[choice - kDeleteId];
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Delete bass",
                                               "Delete \"" + file.getFileNameWithoutExtension() + "\"? (it goes to the Recycle Bin)",
                                               "Delete", "Cancel", safeParent.getComponent(),
                                               juce::ModalCallbackFunction::create([file](int ok)
                                               {
                                                   if (ok != 0)
                                                       file.moveToTrash();
                                               }));
            return;
        }
        // an HPDG 808 sound on the lane: the strip's settings stay
        BassPresetFile preset;
        if (choice >= kUserId)
        {
            const auto read = readBassPreset(userFiles[choice - kUserId]);
            if (!read.has_value())
                return;
            preset = *read;
        }
        else
        {
            const auto& factory = bassPresets()[static_cast<size_t>(choice - kFactoryId)];
            preset.name = factory.name;
            preset.settings = factory.settings;
        }
        auto next = sound;
        next.custom = true;
        next.name = preset.name;
        next.settings = bassSettingsToTree(preset.settings);
        next.sampleWav = {};
        if (preset.sample != nullptr)
            next.sampleWav = preset.sample->toWavMemory();
        else if (preset.settings.sampleLevelDb > -59.5f)
        {
            // a preset that plays a sample (Kick to 808) and has none of its own: the lane's
            if (sound.custom && sound.sampleWav.getSize() > 0)
                next.sampleWav = sound.sampleWav;
            else if (const auto bank = processor.getSub808BankSample())
                next.sampleWav = bank->toWavMemory();
        }
        processor.setSub808Sound(next);
        refresh();
    });
}

Sub808EditorOverlay::Host makeSub808EditorHost(BoomBapGeneratorAudioProcessor& processor,
                                               std::function<void()> soundChanged,
                                               std::function<void()> close)
{
    Sub808EditorOverlay::Host host;
    host.getSound = [&processor] { return processor.getSub808Sound(); };
    host.setSound = [&processor, soundChanged](const Sub808SoundState& sound)
    {
        processor.setSub808Sound(sound);
        if (soundChanged)
            soundChanged();
    };
    host.getBankSample = [&processor] { return processor.getSub808BankSample(); };
    host.getBankSampleName = [&processor] { return processor.getSelectedLaneSampleName(TrackType::Sub808); };
    host.audition = [&processor](int note, int velocity) { processor.auditionSub808Note(note, velocity, TimingGrid::Sixteenth * 32); };
    host.releaseAudition = [&processor](int note) { processor.releaseSub808AuditionNote(note); };
    host.close = std::move(close);
    return host;
}

juce::String sub808SoundName(BoomBapGeneratorAudioProcessor& processor, const Sub808SoundState& sound)
{
    return sound.custom ? sound.name : processor.getSelectedLaneSampleName(TrackType::Sub808);
}
} // namespace bbg
