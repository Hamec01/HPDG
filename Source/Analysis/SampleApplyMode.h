#pragma once

#include <juce_core/juce_core.h>

namespace bbg
{
enum class SampleApplyMode
{
    GenreFirst = 0,
    Blend,
    SampleFirst,
    ExactCopy
};

inline SampleApplyMode sampleApplyModeFromChoiceIndex(int choice)
{
    switch (choice)
    {
        case 0: return SampleApplyMode::GenreFirst;
        case 2: return SampleApplyMode::SampleFirst;
        case 3: return SampleApplyMode::ExactCopy;
        case 1:
        default: return SampleApplyMode::Blend;
    }
}

inline int choiceIndexFromSampleApplyMode(SampleApplyMode mode)
{
    switch (mode)
    {
        case SampleApplyMode::GenreFirst: return 0;
        case SampleApplyMode::SampleFirst: return 2;
        case SampleApplyMode::ExactCopy: return 3;
        case SampleApplyMode::Blend:
        default: return 1;
    }
}

inline juce::String sampleApplyModeDisplayName(SampleApplyMode mode)
{
    switch (mode)
    {
        case SampleApplyMode::GenreFirst: return "Genre First";
        case SampleApplyMode::SampleFirst: return "Sample First";
        case SampleApplyMode::ExactCopy: return "Exact Copy";
        case SampleApplyMode::Blend:
        default: return "Blend";
    }
}

inline juce::StringArray getSampleApplyModeNames()
{
    return { "Genre First", "Blend", "Sample First", "Exact Copy" };
}
} // namespace bbg