// HPDG_UiSnapshot <out folder> [width height]   or   <out folder> trim <audio file> [bars]
// Renders the VST3 editor (after a generated pattern) to <out>/<skin>.png for every UI skin, so the
// look can be checked without a DAW.
#include <iostream>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/Plugin/PluginProcessor.h"
#include "../Source/UI/SampleTrimEditorComponent.h"
#include "../Source/UI/SketchTheme.h"

using namespace bbg;

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File out(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const int width = argc > 3 ? juce::String(argv[2]).getIntValue() : 1500;
    const int height = argc > 3 ? juce::String(argv[3]).getIntValue() : 940;
    out.createDirectory();

    if (argc > 2 && juce::String(argv[2]) == "fonts")
    {
        // candidate typefaces for the skins, rendered side by side
        const juce::StringArray candidates { "Bahnschrift|SemiBold Condensed", "Bahnschrift|Condensed", "Bahnschrift|SemiBold",
                                             "Bahnschrift|Regular", "Bahnschrift|Light", "Bahnschrift|Light Condensed",
                                             "Bahnschrift SemiBold Condensed|Regular", "Bahnschrift Light Condensed|Regular",
                                             "Segoe UI|Semibold", "Agency FB|Bold", "Arial Narrow|Bold", "Consolas|Regular" };
        juce::Image image(juce::Image::RGB, 900, 40 * candidates.size() + 20, true);
        {
        juce::Graphics g(image);
        g.fillAll(juce::Colour::fromRGB(30, 41, 55));
        int y = 10;
        for (const auto& c : candidates)
        {
            juce::Font font(juce::FontOptions(c.upToFirstOccurrenceOf("|", false, false), c.fromFirstOccurrenceOf("|", false, false), 20.0f));
            g.setFont(font);
            g.setColour(juce::Colours::white);
            g.drawText(c + "  ->  " + font.getTypefaceName() + " / " + font.getTypefaceStyle() + "   HPDG Instrument Rack BHH3 Generate Pattern 128 BPM",
                       10, y, 880, 34, juce::Justification::centredLeft);
            y += 40;
        }
        }
        juce::FileOutputStream stream(out.getChildFile("fonts.png"));
        juce::PNGImageFormat().writeImageToStream(image, stream);
        return 0;
    }

    if (argc > 3 && juce::String(argv[2]) == "trim")
    {
        // HPDG_UiSnapshot <out folder> trim <audio file> [bars]: the Trim dialog for that file.
        auto source = std::make_shared<SampleSourceAudio>();
        if (!source->load(juce::File(juce::String(argv[3]))))
            return 1;
        SampleTrimEditorComponent trim(source, { 0.0, juce::jmin(7.12, source->getDurationSeconds()) }, false);
        trim.setLookAndFeel(nullptr);
        if (argc > 4)
            trim.setSelectionBars(juce::String(argv[4]).getIntValue());
        trim.setPlayCursor(2.0);
        const auto image = trim.createComponentSnapshot(trim.getLocalBounds(), true, 1.5f);
        juce::FileOutputStream stream(out.getChildFile("trim.png"));
        juce::PNGImageFormat().writeImageToStream(image, stream);
        return 0;
    }

    for (const auto skin : sketch::allSkins())
    {
        sketch::setActiveSkin(skin);
        BoomBapGeneratorAudioProcessor processor;
        processor.generatePattern();
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createVst3EditorForTests());
        editor->setSize(width, height);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile(juce::String(sketch::skinName(skin)) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat().writeImageToStream(image, stream);
        std::cout << file.getFullPathName() << "\n";
        editor.reset();
    }
    return 0;
}
