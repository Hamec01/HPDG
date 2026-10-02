#pragma once

#include <algorithm>

#include "../Core/GeneratorParams.h"

namespace bbg
{
enum class TempoBand
{
    Base = 0,
    Elevated,
    Fast
};

// The tempo a genre generates at over a loaded sample of `sampleBpm`. Drum & Bass runs at
// double time over a half-time loop (77 -> 154): one sample bar = two DnB bars, still in sync.
// Other genres take the sample tempo as it is.
inline float generationBpmForSample(float sampleBpm, GenreType genre)
{
    float bpm = std::clamp(sampleBpm, 40.0f, 240.0f);
    if (genre == GenreType::DnB)
        while (bpm < 120.0f)
            bpm *= 2.0f;
    return bpm;
}

inline float interpretedBpmForGenre(float bpm, GenreType genre)
{
    const float clamped = std::clamp(bpm, 40.0f, 240.0f);

    // Genre defaults:
    // - Trap/Drill are commonly authored/perceived in double-time at low host tempos (e.g. 70 -> 140).
    // - BoomBap is commonly authored/perceived in half-time at high host tempos (e.g. 140 -> 70).
    if ((genre == GenreType::Trap || genre == GenreType::Drill) && clamped <= 90.0f)
        return std::clamp(clamped * 2.0f, 40.0f, 240.0f);

    // - DnB is authored at ~174; a half-time host tempo (~87) means the same groove.
    if (genre == GenreType::DnB && clamped <= 100.0f)
        return std::clamp(clamped * 2.0f, 40.0f, 240.0f);

    if (genre == GenreType::BoomBap && clamped >= 120.0f)
        return std::clamp(clamped * 0.5f, 40.0f, 240.0f);

    return clamped;
}

inline TempoBand selectTempoBand(float bpm,
                                 const GeneratorParams& params,
                                 float autoElevatedThreshold,
                                 float autoFastThreshold,
                                 float forcedElevatedThreshold,
                                 float forcedFastThreshold)
{
    (void) forcedFastThreshold;
    const float clampedBpm = std::clamp(bpm, 40.0f, 240.0f);

    // 0=Auto, 1=Original, 2=Half-time Aware.
    if (params.tempoInterpretationMode == 1)
        return TempoBand::Base;

    // Auto: apply genre-aware folding before band selection.
    const float autoBpm = params.tempoInterpretationMode == 0
        ? interpretedBpmForGenre(clampedBpm, params.genre)
        : clampedBpm;

    if (params.tempoInterpretationMode == 2)
    {
        // "Half-time aware" is intended to interpret higher host tempos as half-time,
        // so we never escalate into a double-time "Fast" band here.
        return autoBpm >= forcedElevatedThreshold ? TempoBand::Elevated : TempoBand::Base;
    }

    if (autoBpm >= autoFastThreshold)
        return TempoBand::Fast;
    if (autoBpm >= autoElevatedThreshold)
        return TempoBand::Elevated;
    return TempoBand::Base;
}
} // namespace bbg
