// HPDG_DemoReel <script.txt> <out folder>
// Plays a demo script against the real VST3 editor without a host: presses its buttons, picks
// genres, drops samples, and records what that produces - one UI snapshot per video frame
// (frames/*.jpg, unchanged frames are shared), the audio of the whole run (audio.wav, from
// processBlock) and a timeline of the script events with screen positions (timeline.jsonl).
// tools/demo_reel/compose.py turns that into the video (cursor, captions, title cards).
//
// Script: one command per line, # comments, "quoted" arguments. See tools/demo_reel/demo.txt.
//   skin Dark|Steel|Sketch          editor <w> <h> <scale>     wait <s>      bars <n> (at the current BPM)
//   click "<button text>"           click row <Track> "<button>"   click-on ... (only if the toggle is off)
//   dawdrag ["row" <Track>] "<button>" "<file>" (drag out into a DAW window)
//   fakeclick "<button>"            combo genre|substyle|bars|mode "<item>"   knob swing|density|quantize <v> <s>
//   bass 1|2|3                      toggle "<button>"          play / stop   set <paramId> <value>
//   drop "<file>"                   audition <start> <end> / audition-stop    generate-quiet   info
//   point <target> (moves the cursor)   highlight|zoom <target> [amount] [left] / highlight|zoom off
//   caption "line" ["line 2"]  caption-clear   chapter "<name>"   title "a" ["b"] ["c"]   title-clear
//   snap "<file.png>" [scale] (PNG of the editor + control rects as JSON)
//   badge "<text>"  badge-clear  cursor-hide   note "<text>" (callout at the next click)
//   targets: header, strip, grid, rack, row:<Track>, button:<text>, combo:<name>, knob:<name>
#include <cmath>
#include <iostream>

#define NOMINMAX
#include <windows.h>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/Plugin/PluginProcessor.h"
#include "../Source/UI/BassAmountSelector.h"
#include "../Source/UI/MainHeaderComponent.h"
#include "../Source/UI/SampleBreakStripComponent.h"
#include "../Source/UI/SketchTheme.h"
#include "../Source/UI/TrackListComponent.h"
#include "../Source/UI/TrackRowComponent.h"
#include "../Source/UI/Vst3GridLiteComponent.h"

using namespace bbg;

namespace
{
template <typename T>
T* findChild(juce::Component& root)
{
    if (auto* hit = dynamic_cast<T*>(&root))
        return hit;
    for (auto* child : root.getChildren())
        if (auto* hit = findChild<T>(*child))
            return hit;
    return nullptr;
}

template <typename T>
void findAll(juce::Component& root, std::vector<T*>& out)
{
    if (auto* hit = dynamic_cast<T*>(&root))
        out.push_back(hit);
    for (auto* child : root.getChildren())
        findAll<T>(*child, out);
}

// visible inside root (the editor is not on the desktop, so isShowing() is always false)
bool visibleIn(juce::Component& c, juce::Component& root)
{
    for (auto* p = &c; p != nullptr && p != &root; p = p->getParentComponent())
        if (!p->isVisible() || p->getWidth() <= 0)
            return false;
    return true;
}

juce::Button* findButton(juce::Component& root, const juce::String& text)
{
    std::vector<juce::Button*> buttons;
    findAll(root, buttons);
    for (auto* b : buttons)
        if (visibleIn(*b, root) && b->getButtonText() == text)
            return b;
    for (auto* b : buttons)
        if (visibleIn(*b, root) && b->getButtonText().startsWithIgnoreCase(text))
            return b;
    return nullptr;
}

// Win32 message pump: JUCE timers and async callbacks are window messages.
void pumpMessages(int milliseconds)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(milliseconds);
    do
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (milliseconds > 0)
            Sleep(5);
    } while (juce::Time::getMillisecondCounter() < end);
}

juce::String jsonString(const juce::String& s)
{
    return juce::JSON::toString(juce::var(s));
}

class Reel
{
public:
    Reel(const juce::File& outFolder, const juce::File& scriptFolder) : out(outFolder), scriptDir(scriptFolder)
    {
        out.createDirectory();
        framesDir = out.getChildFile("frames");
        framesDir.deleteRecursively();
        framesDir.createDirectory();
        timeline = std::make_unique<juce::FileOutputStream>(out.getChildFile("timeline.jsonl"));
        timeline->setPosition(0);
        timeline->truncate();
        frameList = std::make_unique<juce::FileOutputStream>(out.getChildFile("frames.txt"));
        frameList->setPosition(0);
        frameList->truncate();
    }

    ~Reel()
    {
        editor.reset();
        writeAudio();
    }

    bool run(const juce::StringArray& lines)
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            const auto line = lines[i].trim();
            if (line.isEmpty() || line.startsWith("#"))
                continue;
            juce::StringArray t;
            t.addTokens(line, " ", "\"");
            t.removeEmptyStrings();
            for (auto& s : t)
                s = s.unquoted();
            if (!execute(t))
            {
                std::cerr << "line " << (i + 1) << ": cannot run: " << line << "\n";
                return false;
            }
        }
        return true;
    }

private:
    // ---- output ----------------------------------------------------------------------------
    void event(const juce::String& type, const juce::String& fields = {})
    {
        timeline->writeText("{\"frame\":" + juce::String(frameIndex) + ",\"type\":" + jsonString(type)
                                + (fields.isNotEmpty() ? "," + fields : juce::String()) + "}\n",
                            false, false, nullptr);
        timeline->flush();
    }

    juce::Rectangle<int> screenRect(juce::Component& c) const
    {
        const auto r = editor->getLocalArea(&c, c.getLocalBounds()).toFloat() * scale;
        return r.toNearestInt();
    }

    static juce::String rectJson(juce::Rectangle<int> r)
    {
        return "[" + juce::String(r.getX()) + "," + juce::String(r.getY()) + "," + juce::String(r.getWidth()) + ","
            + juce::String(r.getHeight()) + "]";
    }

    void renderFrames(int count)
    {
        for (int f = 0; f < count; ++f)
            renderFrame();
    }

    void renderSeconds(double seconds) { renderFrames(juce::jmax(0, juce::roundToInt(seconds * fps))); }

    void renderFrame()
    {
        // audio for this frame
        juce::AudioBuffer<float> block(processor.getTotalNumOutputChannels(), samplesPerFrame);
        block.clear();
        juce::MidiBuffer midi;
        processor.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            audio[ch].insert(audio[ch].end(), block.getReadPointer(juce::jmin(ch, block.getNumChannels() - 1)),
                             block.getReadPointer(juce::jmin(ch, block.getNumChannels() - 1)) + samplesPerFrame);

        pumpMessages(0);
        const bool playing = processor.isPreviewPlaying();
        if (header != nullptr)
            header->setPreviewPlaying(playing);
        if (grid != nullptr)
            grid->setPlayheadStep(playing ? processor.getPreviewPlayheadStep() : -1.0f);

        // the UI is re-captured only when something may have changed
        const float playhead = playing ? processor.getPreviewPlayheadStep() : -1.0f;
        const int playheadPixelKey = playing ? juce::roundToInt(playhead * 8.0f) : -1;
        if (dirty || playheadPixelKey != lastPlayheadKey || lastFrameFile.isEmpty())
        {
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, scale);
            lastFrameFile = juce::String(frameIndex).paddedLeft('0', 6) + ".jpg";
            juce::FileOutputStream stream(framesDir.getChildFile(lastFrameFile));
            juce::JPEGImageFormat jpeg;
            jpeg.setQuality(0.94f);
            jpeg.writeImageToStream(image, stream);
            lastPlayheadKey = playheadPixelKey;
            dirty = false;
        }
        frameList->writeText(lastFrameFile + "\n", false, false, nullptr);
        ++frameIndex;
    }

    void writeAudio()
    {
        const int n = static_cast<int>(audio[0].size());
        juce::AudioBuffer<float> buffer(2, n);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom(ch, 0, audio[ch].data(), n);
        const auto file = out.getChildFile("audio.wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(new juce::FileOutputStream(file), sampleRate, 2, 32, {}, 0)); // float: keeps peaks over 0 dB
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer(buffer, 0, n);
        std::cout << "frames " << frameIndex << ", audio " << n / sampleRate << " s\n";
    }

    // ---- editor -----------------------------------------------------------------------------
    void buildEditor()
    {
        editor.reset();
        header = nullptr;
        grid = nullptr;
        strip = nullptr;
        processor.setVst3EditorSize(editorWidth, editorHeight);
        editor.reset(processor.createVst3EditorForTests());
        editor->setSize(editorWidth, editorHeight);
        editor->setVisible(true);
        pumpMessages(300);
        header = findChild<MainHeaderComponent>(*editor);
        grid = findChild<Vst3GridLiteComponent>(*editor);
        strip = findChild<SampleBreakStripComponent>(*editor);
        dirty = true;
    }

    void settle()
    {
        pumpMessages(250);
        dirty = true;
    }

    double bpm() const
    {
        return processor.getApvts().getRawParameterValue(ParamIds::bpm)->load();
    }

    void setParam(const juce::String& id, float value)
    {
        if (auto* p = processor.getApvts().getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    }

    // cursor travels to the point, then the action happens
    void moveTo(juce::Rectangle<int> target, double seconds = 0.4)
    {
        const auto c = target.getCentre();
        event("move", "\"x\":" + juce::String(c.x) + ",\"y\":" + juce::String(c.y) + ",\"frames\":"
                          + juce::String(juce::roundToInt(seconds * fps)));
        renderSeconds(seconds);
    }

    void clickAt(juce::Rectangle<int> target, const std::function<void()>& action, double after = 0.25)
    {
        moveTo(target);
        event("click", "\"rect\":" + rectJson(target));
        renderFrames(3);
        action();
        settle();
        renderSeconds(after);
    }

    void pressButton(juce::Button& b)
    {
        if (b.getClickingTogglesState())
            b.setToggleState(!b.getToggleState(), juce::sendNotificationSync);
        else if (b.onClick)
            b.onClick();
        else
            b.triggerClick();
    }

    TrackRowComponent* findRow(const juce::String& trackName)
    {
        std::vector<TrackRowComponent*> rows;
        findAll(*editor, rows);
        for (auto* row : rows)
            if (auto type = row->getRuntimeTrackType(); type.has_value() && trackName == toString(*type))
                return row;
        return nullptr;
    }

    juce::ComboBox* comboByName(const juce::String& name)
    {
        if (name == "genre") return &header->genreCombo;
        if (name == "substyle") return &header->substyleCombo;
        if (name == "bars") return &header->barsCombo;
        if (name == "playback") return &header->previewPlaybackModeCombo;
        if (name == "mode" && strip != nullptr)
            if (auto* c = findChild<juce::ComboBox>(*strip))
                return c;
        return nullptr;
    }

    juce::Slider* knobByName(const juce::String& name)
    {
        if (name == "swing") return &header->swingSlider;
        if (name == "density") return &header->densitySlider;
        if (name == "bpm") return &header->bpmSlider;
        if (name == "quantize" && strip != nullptr)
        {
            std::vector<juce::Slider*> sliders;
            findAll(*strip, sliders);
            for (auto* s : sliders)
                if (s != &strip->volumeSlider)
                    return s;
        }
        return nullptr;
    }

    juce::Component* componentByName(const juce::String& name)
    {
        if (name == "header") return header;
        if (name == "strip") return strip;
        if (name == "grid") return grid;
        if (name == "rack") return findChild<TrackListComponent>(*editor);
        if (name.startsWith("row:")) return findRow(name.fromFirstOccurrenceOf(":", false, false));
        if (name.startsWith("button:")) return findButton(*editor, name.fromFirstOccurrenceOf(":", false, false));
        if (name.startsWith("combo:")) return comboByName(name.fromFirstOccurrenceOf(":", false, false));
        if (name.startsWith("knob:")) return knobByName(name.fromFirstOccurrenceOf(":", false, false));
        return nullptr;
    }

    // ---- commands ---------------------------------------------------------------------------
    bool execute(const juce::StringArray& t)
    {
        const auto cmd = t[0];
        auto num = [&t](int i, double fallback) { return t.size() > i ? t[i].getDoubleValue() : fallback; };

        if (cmd == "skin")
        {
            for (const auto skin : sketch::allSkins())
                if (t[1] == sketch::skinName(skin))
                    sketch::setActiveSkin(skin);
            if (editor != nullptr)
            {
                buildEditor();
                event("skin", "\"name\":" + jsonString(t[1]));
            }
            return true;
        }
        if (cmd == "editor")
        {
            editorWidth = t[1].getIntValue();
            editorHeight = t[2].getIntValue();
            scale = static_cast<float>(num(3, 1.0));
            processor.prepareToPlay(sampleRate, samplesPerFrame);
            buildEditor();
            const auto r = editor->getLocalBounds().toFloat() * scale;
            event("editor", "\"width\":" + juce::String(juce::roundToInt(r.getWidth())) + ",\"height\":"
                                + juce::String(juce::roundToInt(r.getHeight())) + ",\"fps\":" + juce::String(fps));
            return true;
        }
        if (cmd == "wait") { renderSeconds(num(1, 1.0)); return true; }
        if (cmd == "bars")
        {
            renderSeconds(num(1, 1.0) * 4.0 * 60.0 / bpm());
            return true;
        }
        if (cmd == "set") { setParam(t[1], static_cast<float>(num(2, 0.0))); settle(); return true; }
        if (cmd == "generate-quiet") { processor.generatePattern(); settle(); return true; }
        if (cmd == "title" || cmd == "caption" || cmd == "chapter" || cmd == "badge" || cmd == "note")
        {
            juce::StringArray fields;
            for (int i = 1; i < t.size(); ++i)
                fields.add(jsonString(t[i]));
            event(cmd, "\"text\":[" + fields.joinIntoString(",") + "]");
            return true;
        }
        if (cmd == "caption-clear" || cmd == "title-clear" || cmd == "cursor-hide" || cmd == "badge-clear")
        {
            event(cmd);
            return true;
        }
        if (cmd == "highlight" || cmd == "zoom")
        {
            if (t[1] == "off")
            {
                event(cmd + "-off");
                return true;
            }
            auto* c = componentByName(t[1]);
            if (c == nullptr)
                return false;
            event(cmd, "\"rect\":" + rectJson(screenRect(*c)) + ",\"amount\":" + juce::String(num(2, 1.0))
                           + ",\"anchor\":" + jsonString(t.size() > 3 ? t[3] : juce::String("center")));
            return true;
        }
        if (cmd == "point")
        {
            auto* c = componentByName(t[1]);
            if (c == nullptr)
                return false;
            moveTo(screenRect(*c), num(2, 0.4));
            return true;
        }
        if (cmd == "click" || cmd == "click-on") // click-on: only when the toggle is off
        {
            juce::Button* b = nullptr;
            if (t[1] == "row")
            {
                if (auto* row = findRow(t[2]))
                    b = findButton(*row, t[3]);
            }
            else
            {
                b = findButton(*editor, t[1]);
            }
            if (b == nullptr)
                return false;
            if (cmd == "click-on" && b->getToggleState())
                return true;
            clickAt(screenRect(*b), [this, b] { pressButton(*b); });
            return true;
        }
        if (cmd == "dawdrag") // dawdrag "<button>" "<file>" | dawdrag row <Track> "<button>" "<file>": drag out into a DAW
        {
            juce::Button* b = nullptr;
            juce::String fileName;
            if (t[1] == "row")
            {
                if (auto* row = findRow(t[2]))
                    b = findButton(*row, t[3]);
                fileName = t[4];
            }
            else
            {
                b = findButton(*editor, t[1]);
                fileName = t[2];
            }
            if (b == nullptr)
                return false;
            const auto r = screenRect(*b);
            moveTo(r);
            const int frames = juce::roundToInt(3.4 * fps);
            event("dawdrag", "\"rect\":" + rectJson(r) + ",\"name\":" + jsonString(fileName) + ",\"frames\":" + juce::String(frames));
            renderFrames(frames);
            return true;
        }
        if (cmd == "fakeclick") // cursor clicks, nothing happens (export buttons open OS dialogs)
        {
            auto* b = findButton(*editor, t[1]);
            if (b == nullptr)
                return false;
            clickAt(screenRect(*b), [] {});
            return true;
        }
        if (cmd == "play" || cmd == "stop")
        {
            const bool start = cmd == "play";
            if (processor.isPreviewPlaying() == start)
                return true;
            clickAt(screenRect(header->playButton), [this, start] { header->onPlayToggled(start); }, 0.1);
            return true;
        }
        if (cmd == "bass")
        {
            auto* row = findRow("Sub808");
            auto* selector = row != nullptr ? findChild<BassAmountSelector>(*row) : nullptr;
            if (selector == nullptr)
                return false;
            const int index = t[1].getIntValue() - 1;
            auto r = screenRect(*selector);
            const int w = r.getWidth() / BassAmountSelector::kPositions;
            clickAt(juce::Rectangle<int>(r.getX() + index * w, r.getY(), w, r.getHeight()),
                    [selector, index] { selector->setValue(index, juce::sendNotificationSync); });
            return true;
        }
        if (cmd == "combo")
        {
            auto* combo = comboByName(t[1]);
            if (combo == nullptr)
                return false;
            int id = 0;
            juce::StringArray items;
            int chosen = -1;
            for (int i = 0; i < combo->getNumItems(); ++i)
            {
                items.add(jsonString(combo->getItemText(i)));
                if (combo->getItemText(i) == t[2])
                {
                    id = combo->getItemId(i);
                    chosen = i;
                }
            }
            if (id == 0)
            {
                std::cerr << "items: ";
                for (int i = 0; i < combo->getNumItems(); ++i)
                    std::cerr << combo->getItemText(i) << " | ";
                std::cerr << "\n";
                return false;
            }
            const auto r = screenRect(*combo);
            moveTo(r);
            event("click", "\"rect\":" + rectJson(r));
            event("menu", "\"rect\":" + rectJson(r) + ",\"items\":[" + items.joinIntoString(",") + "],\"chosen\":"
                              + juce::String(chosen) + ",\"current\":" + juce::String(combo->getSelectedItemIndex())
                              + ",\"frames\":" + juce::String(juce::roundToInt(0.8 * fps)));
            renderSeconds(0.8);
            combo->setSelectedId(id, juce::sendNotificationSync);
            settle();
            renderSeconds(0.3);
            return true;
        }
        if (cmd == "knob")
        {
            auto* slider = knobByName(t[1]);
            if (slider == nullptr)
                return false;
            const double target = num(2, slider->getValue());
            const double seconds = num(3, 1.2);
            const auto r = screenRect(*slider);
            const double start = slider->getValue();
            const int frames = juce::roundToInt(seconds * fps);
            juce::Point<int> from = r.getCentre(), to = r.getCentre();
            if (slider->isHorizontal())
            {
                // the thumb travels along the track (the text box sits on the right)
                const auto track = r.withTrimmedRight(juce::roundToInt(48 * scale)).reduced(juce::roundToInt(6 * scale), 0);
                auto xAt = [&](double v)
                {
                    return track.getX() + juce::roundToInt(track.getWidth() * (v - slider->getMinimum())
                                                           / (slider->getMaximum() - slider->getMinimum()));
                };
                from.x = xAt(start);
                to.x = xAt(target);
            }
            else
            {
                to.y += target > start ? -40 : 40;
            }
            moveTo(juce::Rectangle<int>(from.x, from.y, 1, 1));
            event("drag", "\"from\":[" + juce::String(from.x) + "," + juce::String(from.y) + "],\"to\":[" + juce::String(to.x)
                              + "," + juce::String(to.y) + "],\"frames\":" + juce::String(frames));
            for (int f = 1; f <= frames; ++f)
            {
                const double k = static_cast<double>(f) / frames;
                const double eased = k * k * (3.0 - 2.0 * k);
                slider->setValue(start + (target - start) * eased, juce::sendNotificationSync);
                dirty = true;
                renderFrame();
            }
            if (slider->onDragEnd)
                slider->onDragEnd();
            settle();
            return true;
        }
        if (cmd == "toggle")
        {
            auto* b = findButton(*editor, t[1]);
            if (b == nullptr)
                return false;
            clickAt(screenRect(*b), [this, b] { pressButton(*b); });
            return true;
        }
        if (cmd == "drop")
        {
            const auto file = scriptDir.getChildFile(t[1]);
            if (!file.existsAsFile() || strip == nullptr)
                return false;
            const auto r = screenRect(*strip);
            event("drop", "\"name\":" + jsonString(file.getFileName()) + ",\"rect\":" + rectJson(r) + ",\"frames\":"
                              + juce::String(juce::roundToInt(1.4 * fps)));
            renderSeconds(1.4);
            strip->onFileDropped(file);
            settle();
            renderSeconds(0.3);
            return true;
        }
        if (cmd == "audition")
        {
            processor.auditionSampleRegion(num(1, 0.0), num(2, 4.0), true);
            return true;
        }
        if (cmd == "audition-stop") { processor.stopSampleAudition(); return true; }
        if (cmd == "snap") // snap "<file.png>" [scale]: the editor as a PNG + <file>.json with control rects
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(t[1]);
            file.getParentDirectory().createDirectory();
            const float snapScale = static_cast<float>(num(2, 1.5));
            if (grid != nullptr)
                grid->setPlayheadStep(processor.isPreviewPlaying() ? processor.getPreviewPlayheadStep() : -1.0f);
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, snapScale);
            file.deleteFile();
            {
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
            // every visible control: kind, text, owning lane, rect in the PNG's pixels
            juce::StringArray items;
            std::function<void(juce::Component&, juce::String)> walk = [&](juce::Component& c, juce::String lane)
            {
                if (auto* row = dynamic_cast<TrackRowComponent*>(&c))
                    if (auto type = row->getRuntimeTrackType())
                        lane = toString(*type);
                juce::String kind, text;
                if (auto* b = dynamic_cast<juce::Button*>(&c)) { kind = "button"; text = b->getButtonText(); }
                else if (auto* cb = dynamic_cast<juce::ComboBox*>(&c)) { kind = "combo"; text = cb->getText(); }
                else if (dynamic_cast<juce::Slider*>(&c) != nullptr) { kind = "slider"; text = c.getName(); }
                else if (dynamic_cast<BassAmountSelector*>(&c) != nullptr) { kind = "bass"; }
                else if (&c == header) kind = "header";
                else if (&c == strip) kind = "strip";
                else if (&c == grid) kind = "grid";
                else if (dynamic_cast<TrackListComponent*>(&c) != nullptr) kind = "rack";
                else if (dynamic_cast<TrackRowComponent*>(&c) != nullptr) kind = "row";
                if (kind.isNotEmpty() && visibleIn(c, *editor))
                {
                    const auto r = (editor->getLocalArea(&c, c.getLocalBounds()).toFloat() * snapScale).toNearestInt();
                    items.add("{\"kind\":" + jsonString(kind) + ",\"text\":" + jsonString(text) + ",\"lane\":" + jsonString(lane)
                              + ",\"rect\":" + rectJson(r) + "}");
                }
                for (auto* child : c.getChildren())
                    walk(*child, lane);
            };
            walk(*editor, {});
            file.withFileExtension("json").replaceWithText("[\n" + items.joinIntoString(",\n") + "\n]\n");
            return true;
        }
        if (cmd == "dump808") // Sub808 notes + the lane rendered alone (for checking the register)
        {
            const auto project = processor.getProjectSnapshot();
            for (const auto& track : project.tracks)
            {
                if (track.type != TrackType::Sub808)
                    continue;
                std::cout << "Sub808 [" << processor.getSelectedLaneSampleName(TrackType::Sub808) << "] enabled "
                          << track.enabled << " transpose " << track.sub808Settings.transposeSemitones << " | notes:";
                for (const auto& n : track.notes)
                    std::cout << " " << n.pitch;
                std::cout << " | sub808Notes:";
                for (const auto& n : track.sub808Notes)
                    std::cout << " " << n.pitch;
                std::cout << "\n";
            }
            if (t.size() > 1)
            {
                const auto wav = processor.createTemporaryWavFile(TrackType::Sub808);
                const auto target = juce::File::getCurrentWorkingDirectory().getChildFile(t[1]);
                target.deleteFile();
                wav.copyFileTo(target);
            }
            return true;
        }
        if (cmd == "info")
        {
            const auto analysis = processor.getDrumBreakAnalysis();
            const auto harmony = processor.getSampleHarmony();
            std::cout << "bpm " << bpm() << " | sample bpm " << analysis.bpm << " hits " << analysis.hits.size()
                      << " | key " << (harmony.valid ? harmony.keyName() : juce::String("-")) << "\n";
            for (auto& c : { "genre", "substyle", "bars" })
            {
                auto* combo = comboByName(c);
                std::cout << "  " << c << ": ";
                for (int i = 0; i < combo->getNumItems(); ++i)
                    std::cout << combo->getItemText(i) << (i == combo->getSelectedItemIndex() ? "*" : "") << " | ";
                std::cout << "\n";
            }
            return true;
        }
        return false;
    }

    juce::File out, scriptDir, framesDir;
    std::unique_ptr<juce::FileOutputStream> timeline, frameList;
    BoomBapGeneratorAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    MainHeaderComponent* header = nullptr;
    Vst3GridLiteComponent* grid = nullptr;
    SampleBreakStripComponent* strip = nullptr;
    int editorWidth = 1600, editorHeight = 800;
    float scale = 1.2f;
    static constexpr int fps = 30;
    static constexpr double sampleRate = 48000.0;
    static constexpr int samplesPerFrame = 1600;
    std::array<std::vector<float>, 2> audio;
    int frameIndex = 0;
    bool dirty = true;
    int lastPlayheadKey = -2;
    juce::String lastFrameFile;
};
} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 3)
    {
        std::cerr << "HPDG_DemoReel <script.txt> <out folder>\n";
        return 2;
    }
    juce::StringArray lines;
    const juce::File script(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[1])));
    juce::MemoryBlock scriptBytes;
    script.loadFileAsData(scriptBytes);
    lines.addLines(juce::String::fromUTF8(static_cast<const char*>(scriptBytes.getData()), static_cast<int>(scriptBytes.getSize())));
    bool ok = false;
    {
        Reel reel { juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[2])), script.getParentDirectory() };
        ok = reel.run(lines);
    }
    return ok ? 0 : 1;
}
