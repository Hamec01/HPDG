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

inline TempoBand selectTempoBand(float bpm,
                                 const GeneratorParams& params,
                                 float autoElevatedThreshold,
                                 float autoFastThreshold,
                                 float forcedElevatedThreshold,
                                 float forcedFastThreshold)
{
    const float clampedBpm = std::clamp(bpm, 40.0f, 240.0f);

    // 0=Auto, 1=Original, 2=Half-time Aware.
    if (params.tempoInterpretationMode == 1)
        return TempoBand::Base;

    if (params.tempoInterpretationMode == 2)
    {
        if (clampedBpm >= forcedFastThreshold)
            return TempoBand::Fast;
        if (clampedBpm >= forcedElevatedThreshold)
            return TempoBand::Elevated;
        return TempoBand::Elevated;
    }

    if (clampedBpm >= autoFastThreshold)
        return TempoBand::Fast;
    if (clampedBpm >= autoElevatedThreshold)
        return TempoBand::Elevated;
    return TempoBand::Base;
}
} // namespace bbg
