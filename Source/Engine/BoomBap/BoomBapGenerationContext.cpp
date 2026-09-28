#include "BoomBapGenerationContext.h"

#include <algorithm>

namespace bbg
{
const char* toString(BoomBapGrooveArchetype v) { switch(v) { case BoomBapGrooveArchetype::Pocket:return "Pocket"; case BoomBapGrooveArchetype::BreakHeavy:return "BreakHeavy"; case BoomBapGrooveArchetype::Sparse:return "Sparse"; case BoomBapGrooveArchetype::Syncopated:return "Syncopated"; case BoomBapGrooveArchetype::Turnaround:return "Turnaround"; } return "Pocket"; }
const char* toString(RarePhraseEvent v) { switch(v) { case RarePhraseEvent::None:return "None"; case RarePhraseEvent::SnareFill:return "SnareFill"; case RarePhraseEvent::KickTurnaround:return "KickTurnaround"; case RarePhraseEvent::HatDropout:return "HatDropout"; case RarePhraseEvent::GhostPickup:return "GhostPickup"; case RarePhraseEvent::OpenHatLift:return "OpenHatLift"; case RarePhraseEvent::PercEnding:return "PercEnding"; case RarePhraseEvent::BreakStop:return "BreakStop"; } return "None"; }
const char* toString(BoomBapFillType v) { switch(v) { case BoomBapFillType::None:return "None"; case BoomBapFillType::PostSnareGhosts:return "PostSnareGhosts"; case BoomBapFillType::SnareAcceleration:return "SnareAcceleration"; case BoomBapFillType::KickSnareExchange:return "KickSnareExchange"; case BoomBapFillType::DropoutFinalHit:return "DropoutFinalHit"; case BoomBapFillType::BreakStyleTurnaround:return "BreakStyleTurnaround"; } return "None"; }

BoomBapGenerationContext makeBoomBapGenerationContext(int seed, int bars, const BoomBapStyleProfile& profile)
{
    BoomBapGenerationContext c; c.style=profile.substyle;
    const auto base=boomBapHash(static_cast<uint64_t>(static_cast<uint32_t>(seed)),0x424247454eULL);
    c.generationSeed=base; c.motifSeed=boomBapHash(base,0x4d4f544946ULL); c.timingSeed=boomBapHash(base,0x54494d494e47ULL); c.selectionSeed=boomBapHash(base,0x53454c454354ULL);
    float u=hashToUnit(boomBapHash(base,0x4152434854595045ULL)), sum=0;
    for(size_t i=0;i<profile.archetypeWeights.size();++i){sum+=profile.archetypeWeights[i];if(u<=sum){c.archetype=static_cast<BoomBapGrooveArchetype>(i);break;}}
    float eventChance=bars>=4?profile.rareEventProbability:(bars==2?profile.shortPhraseEventProbability:0.0f);
    const std::array<float,5> mult{{.65f,1.15f,1.10f,1.05f,1.65f}};
    eventChance=std::clamp(eventChance*mult[static_cast<size_t>(c.archetype)],0.0f,profile.maxRareEventProbability);
    if(hashToUnit(boomBapHash(base,0x524152454556454eULL))<eventChance)
    {
        float e=hashToUnit(boomBapHash(base,0x4556454e54545950ULL)), acc=0;
        for(size_t i=0;i<profile.rareEventWeights.size();++i){acc+=profile.rareEventWeights[i];if(e<=acc){c.requestedEvent=static_cast<RarePhraseEvent>(i+1);break;}}
    }
    return c;
}
} // namespace bbg
