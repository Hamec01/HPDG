#pragma once

namespace bbg
{
// Musical grid/quantize division, independent of any particular editor or engine.
// Shared by TimingGrid helpers, the editor's Edit Grid/Snap, and (from Stage 3 onward)
// GeneratorParams::generationGrid. Do not create a second enum for the same musical values.
enum class GridDivision
{
    Auto,   // Context-driven (editor: adaptive-by-zoom; generator: genre-driven skeleton).
    Free,   // No quantization; any tick is valid.

    Quarter,
    QuarterTriplet,

    Eighth,
    EighthTriplet,

    Sixteenth,
    SixteenthTriplet,

    ThirtySecond,
    ThirtySecondTriplet,

    SixtyFourth,
    SixtyFourthTriplet
};
} // namespace bbg
