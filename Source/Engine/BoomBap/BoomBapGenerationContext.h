#pragma once

#include <cstdint>
#include <optional>

#include "BoomBapStyleProfile.h"

namespace bbg
{
enum class BoomBapGrooveArchetype { Pocket, BreakHeavy, Sparse, Syncopated, Turnaround };
enum class RarePhraseEvent { None, SnareFill, KickTurnaround, HatDropout, GhostPickup, OpenHatLift, PercEnding, BreakStop };
enum class BoomBapFillType { None, PostSnareGhosts, SnareAcceleration, KickSnareExchange, DropoutFinalHit, BreakStyleTurnaround };

struct BoomBapGenerationContext
{
    BoomBapSubstyle style = BoomBapSubstyle::Classic;
    BoomBapGrooveArchetype archetype = BoomBapGrooveArchetype::Pocket;
    RarePhraseEvent requestedEvent = RarePhraseEvent::None;
    uint64_t generationSeed = 0;
    uint64_t motifSeed = 0;
    uint64_t timingSeed = 0;
    uint64_t selectionSeed = 0;
};

inline uint64_t boomBapHash(uint64_t value, uint64_t domain)
{
    value ^= domain + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
    value ^= value >> 30U; value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U; value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

inline float hashToUnit(uint64_t value)
{
    return static_cast<float>((value >> 11U) * (1.0 / 9007199254740992.0));
}

const char* toString(BoomBapGrooveArchetype value);
const char* toString(RarePhraseEvent value);
const char* toString(BoomBapFillType value);
BoomBapGenerationContext makeBoomBapGenerationContext(int seed, int bars, const BoomBapStyleProfile& profile);
} // namespace bbg
