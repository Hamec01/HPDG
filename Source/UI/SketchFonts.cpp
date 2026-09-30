#include "SketchFonts.h"

#include <BinaryData.h>

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
    return NotebookTypefaceHolder::getInstance()->face;
}

juce::Font notebookFont(float height, bool bold)
{
    juce::Font font { juce::FontOptions(notebookTypeface()) };
    font.setHeight(height);
    juce::ignoreUnused(bold); // Neucha has one native weight; synthetic bold damages its handwritten shapes.
    return font;
}
}
