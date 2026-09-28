#include "Engine/BoomBap/BoomBapClassicAlgebraGenerator.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>

using namespace bbg;
int main(int argc,char**argv)
{
 const int seeds=argc>1?std::clamp(std::atoi(argv[1]),1,100000):1000;std::ofstream csv;
 if(argc>2){csv.open(argv[2]);csv<<"seed,archetype,requestedEvent,realizedEvent,fillType,quality,bestQuality,nearBestPool,ghosts,orphanGhosts,ghostVelocityViolations,kickGhosts,orphanKickGhosts,kickConversation,hatMotif,fillQuality,dropoutQuality,novelty,trapConfidence,hardTrap,ms\n";}
 BoomBapClassicAlgebraGenerator generator;std::vector<double>timings;timings.reserve(seeds);std::array<int,5>archetypes{};std::array<int,8>events{};
 int requested=0,realized=0,hardTrap=0,missingBackbeats=0,deterministicFailures=0,orphanGhosts=0,ghostVelocityViolations=0,orphanKickGhosts=0;double quality=0,trap=0,pool=0;
 for(int seed=1;seed<=seeds;++seed){BoomBapClassicAlgebraParams p;p.seed=seed;p.bars=4;p.candidateCount=64;p.substyle=BoomBapSubstyle::Classic;auto start=std::chrono::steady_clock::now();auto pattern=generator.generate(p);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();timings.push_back(ms);quality+=pattern.score.quality;trap+=pattern.score.trapLeakConfidence;pool+=pattern.nearBestPoolSize;hardTrap+=pattern.score.hardTrapLeak;requested+=pattern.context.requestedEvent!=RarePhraseEvent::None;realized+=pattern.realizedEvent!=RarePhraseEvent::None;++archetypes[(size_t)pattern.context.archetype];++events[(size_t)pattern.realizedEvent];
  for(int bar=0;bar<4;++bar)for(int t:{16,48})missingBackbeats+=std::none_of(pattern.notesByLane[BoomBapClassicLanes::Snare].begin(),pattern.notesByLane[BoomBapClassicLanes::Snare].end(),[&](const auto&n){return n.barIndex==bar&&n.tick64%64==t;});
  int ghostCount=0,kickGhostCount=0,localOrphan=0,localKickOrphan=0,localVelocityViolation=0;for(const auto&n:pattern.notesByLane[BoomBapClassicLanes::ClapGhost]){++ghostCount;if(n.role!=BoomBapClassicRole::FillSupport&&n.role!=BoomBapClassicRole::ClapLayer&&n.anchorLane!=BoomBapClassicLanes::Snare)++localOrphan;if(n.anchorLane==BoomBapClassicLanes::Snare){auto a=std::find_if(pattern.notesByLane[BoomBapClassicLanes::Snare].begin(),pattern.notesByLane[BoomBapClassicLanes::Snare].end(),[&](const auto&x){return x.tick64==n.anchorTick64;});if(a==pattern.notesByLane[BoomBapClassicLanes::Snare].end()||n.velocity>=a->velocity)++localVelocityViolation;}}
  for(const auto&n:pattern.notesByLane[BoomBapClassicLanes::KickGhost]){++kickGhostCount;if(n.role!=BoomBapClassicRole::FillKick&&n.anchorLane!=BoomBapClassicLanes::Kick)++localKickOrphan;}orphanGhosts+=localOrphan;orphanKickGhosts+=localKickOrphan;ghostVelocityViolations+=localVelocityViolation;
  if(seed<=10&&generator.generate(p).debugSummary!=pattern.debugSummary)++deterministicFailures;
  if(csv)csv<<seed<<','<<toString(pattern.context.archetype)<<','<<toString(pattern.context.requestedEvent)<<','<<toString(pattern.realizedEvent)<<','<<toString(pattern.fillType)<<','<<pattern.score.quality<<','<<pattern.bestCandidateQuality<<','<<pattern.nearBestPoolSize<<','<<ghostCount<<','<<localOrphan<<','<<localVelocityViolation<<','<<kickGhostCount<<','<<localKickOrphan<<','<<pattern.score.kickConversationQuality<<','<<pattern.score.hatMotifCoherence<<','<<pattern.score.fillQuality<<','<<pattern.score.dropoutQuality<<','<<pattern.score.novelty<<','<<pattern.score.trapLeakConfidence<<','<<pattern.score.hardTrapLeak<<','<<ms<<'\n';}
 std::sort(timings.begin(),timings.end());auto pct=[&](double q){return timings[std::min(timings.size()-1,(size_t)std::lround(q*(timings.size()-1)))];};
 std::cout<<"seeds="<<seeds<<" avgQuality="<<quality/seeds<<" avgTrapConfidence="<<trap/seeds<<" requestedRare="<<requested<<" realizedRare="<<realized<<" rareRate="<<realized/(double)seeds<<" archetypes="<<archetypes[0]<<'/'<<archetypes[1]<<'/'<<archetypes[2]<<'/'<<archetypes[3]<<'/'<<archetypes[4]<<" hardTrap="<<hardTrap<<" missingBackbeats="<<missingBackbeats<<" orphanGhosts="<<orphanGhosts<<" ghostVelocityViolations="<<ghostVelocityViolations<<" orphanKickGhosts="<<orphanKickGhosts<<" deterministicFailures="<<deterministicFailures<<" avgNearBestPool="<<pool/seeds<<" p50Ms="<<pct(.50)<<" p95Ms="<<pct(.95)<<" worstMs="<<timings.back()<<'\n';
 return(hardTrap||missingBackbeats||orphanGhosts||ghostVelocityViolations||orphanKickGhosts||deterministicFailures)?1:0;
}
