#pragma once

#include "DnBTypes.h"

namespace bbg
{
// "Is this good DnB?" - features with style targets (inverted-U, not "more is better"),
// critical gates (a bad backbone is never compensated by nice hats), a geometric-mean core,
// a secondary sum and penalties.
class DnBScorer
{
public:
    static DnBScore score(const DnBPattern& pattern, const DnBStyleProfile& style);

    // A/B test for secondary notes: a note whose removal does not lower the score is not
    // earning its place and is removed. Returns the number of removed notes.
    static int pruneSecondary(DnBPattern& pattern, const DnBStyleProfile& style);
};
} // namespace bbg
