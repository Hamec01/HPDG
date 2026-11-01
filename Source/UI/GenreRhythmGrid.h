#pragma once

#include "../Core/GeneratorParams.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
inline int defaultSnapForGenre(GenreType genre)
{
    return genre == GenreType::Trap || genre == GenreType::Drill ? 16 : 14; // 1/32 vs 1/16
}

inline int ticksForSnapId(int id)
{
    switch (id)
    {
        case 2: return 1;
        case 10: return kInternalPpq;
        case 11: return kInternalPpq * 2 / 3;
        case 12: return kInternalPpq / 2;
        case 13: return kInternalPpq / 3;
        case 15: return kInternalPpq / 6;
        case 16: return kInternalPpq / 8;
        case 17: return kInternalPpq / 12;
        case 18: return kInternalPpq / 16;
        case 19: return kInternalPpq / 24;
        default: return kInternalPpq / 4;
    }
}
}
