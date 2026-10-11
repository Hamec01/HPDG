#include "SketchFonts.h"

#include <BinaryData.h>

#include "SketchTheme.h"

namespace bbg::sketch
{
namespace
{
// The embedded font lives in a JUCE DeletedAtShutdown singleton, not a function-local static:
// JUCE deletes it when the last plugin instance shuts JUCE down, whereas a static would be
// destroyed during DLL unload (inside DllMain), where releasing Windows font objects can
// deadlock the host on plugin removal.
class NotebookTypefaceHolder final : private juce::DeletedAtShutdown
{
public:
    NotebookTypefaceHolder()
        : face(juce::Typeface::createSystemTypefaceFor(BinaryData::Neucha_ttf, BinaryData::Neucha_ttfSize))
    {
    }

    ~NotebookTypefaceHolder() override { clearSingletonInstance(); }

    juce::Typeface::Ptr face;

    JUCE_DECLARE_SINGLETON_SINGLETHREADED_INLINE(NotebookTypefaceHolder, false)
};
}

juce::Typeface::Ptr notebookTypeface()
{
    // Steel / Dark: Windows' own DIN-like Bahnschrift (installed with Windows 10, not embedded);
    // other systems fall back to their default sans.
    if (activeSkin() == Skin::Steel)
        return juce::Font(juce::FontOptions("Bahnschrift", "SemiBold", 14.0f)).getTypefacePtr();
    if (activeSkin() == Skin::Dark)
        return juce::Font(juce::FontOptions("Bahnschrift", "Light", 14.0f)).getTypefacePtr();
    return NotebookTypefaceHolder::getInstance()->face;
}

juce::Font notebookFont(float height, bool bold)
{
    if (!isHandDrawn())
    {
        // Bahnschrift is taller and wider than Neucha at the same height: a little smaller keeps every
        // layout. (Its Condensed instances are mis-measured by JUCE: letters spread apart.)
        juce::Font font { juce::FontOptions(notebookTypeface()) };
        font.setHeight(height * 0.86f);
        juce::ignoreUnused(bold);
        return font;
    }
    juce::Font font { juce::FontOptions(notebookTypeface()) };
    font.setHeight(height);
    juce::ignoreUnused(bold); // Neucha has one native weight; synthetic bold damages its handwritten shapes.
    return font;
}
}
