#pragma once

#include <vector>

namespace bbg
{
struct ReferenceHatNote
{
    int tickInBar = 0;
    int velocity = 96;
};

struct ReferenceHatCluster
{
    int barIndex = 0;
    int startStep32 = 0;
    int endStep32 = 0;
    int noteCount = 0;
    bool triplet = false;
};

struct ReferenceHatBarSkeleton
{
    int barIndex = 0;
    bool hasBarStartAnchor = false;
    std::vector<ReferenceHatNote> notes;
    std::vector<int> backboneSteps16;
    std::vector<int> motionSteps32;
    std::vector<int> phraseAnchorSteps32;
    std::vector<int> preSnareZoneSteps32;
};

struct ReferenceHatSkeleton
{
    bool available = false;
    int sourceBars = 0;
    std::vector<ReferenceHatBarSkeleton> barMaps;
    std::vector<ReferenceHatCluster> rollClusters;
    std::vector<ReferenceHatCluster> tripletClusters;
};

struct ReferenceHatCorpus
{
    bool available = false;
    int sourceReferenceCount = 0;
    std::vector<ReferenceHatSkeleton> variants;
};

struct ReferenceKickNote
{
    int step16 = 0;
    int velocity = 110;
};

struct ReferenceKickBarPattern
{
    int barIndex = 0;
    std::vector<ReferenceKickNote> notes;
};

struct ReferenceKickPattern
{
    bool available = false;
    int sourceBars = 0;
    std::vector<ReferenceKickBarPattern> barPatterns;
};

struct ReferenceKickCorpus
{
    bool available = false;
    int sourceReferenceCount = 0;
    std::vector<ReferenceKickPattern> variants;
};
} // namespace bbg