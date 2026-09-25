#include "Engine/BoomBap/BoomBapClassicAlgebraGenerator.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>

using namespace bbg;
int main(int argc, char** argv)
{
    const int seeds = argc > 1 ? std::clamp(std::atoi(argv[1]), 1, 100000) : 1000;
    std::ofstream csv;
    if (argc > 2) { csv.open(argv[2]); csv << "seed,quality,similarity12,similarity13,similarity14,trapConfidence,hardTrap,ms\n"; }
    BoomBapClassicAlgebraGenerator generator;
    std::vector<double> timings; timings.reserve(seeds);
    int hardTrap=0, missingBackbeats=0, deterministicFailures=0;
    double quality=0, trapConfidence=0;
    for (int seed=1; seed<=seeds; ++seed)
    {
        BoomBapClassicAlgebraParams p; p.seed=seed; p.bars=4; p.candidateCount=64; p.substyle=BoomBapSubstyle::Classic;
        const auto start=std::chrono::steady_clock::now(); const auto pattern=generator.generate(p);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); timings.push_back(ms);
        quality+=pattern.score.quality; trapConfidence+=pattern.score.trapLeakConfidence; hardTrap+=pattern.score.hardTrapLeak;
        for(int bar=0;bar<4;++bar)for(int t:{16,48})if(std::none_of(pattern.notesByLane[BoomBapClassicLanes::Snare].begin(),pattern.notesByLane[BoomBapClassicLanes::Snare].end(),[&](const auto&n){return n.barIndex==bar&&n.tick64%64==t;}))++missingBackbeats;
        if(seed<=10 && generator.generate(p).debugSummary!=pattern.debugSummary)++deterministicFailures;
        if(csv) csv<<seed<<','<<pattern.score.quality<<','<<pattern.score.similarity12<<','<<pattern.score.similarity13<<','<<pattern.score.similarity14<<','<<pattern.score.trapLeakConfidence<<','<<pattern.score.hardTrapLeak<<','<<ms<<'\n';
    }
    std::sort(timings.begin(),timings.end()); auto percentile=[&](double q){return timings[std::min(timings.size()-1,(size_t)std::lround(q*(timings.size()-1)))];};
    std::cout<<"seeds="<<seeds<<" avgQuality="<<quality/seeds<<" avgTrapConfidence="<<trapConfidence/seeds
             <<" hardTrap="<<hardTrap<<" missingBackbeats="<<missingBackbeats<<" deterministicFailures="<<deterministicFailures
             <<" p50Ms="<<percentile(.50)<<" p95Ms="<<percentile(.95)<<" worstMs="<<timings.back()<<'\n';
    return (hardTrap||missingBackbeats||deterministicFailures)?1:0;
}
