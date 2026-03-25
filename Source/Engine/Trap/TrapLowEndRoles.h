#pragma once

#include <algorithm>

#include "TrapPhrasePlanner.h"
#include "../../Core/NoteEvent.h"

namespace bbg
{
enum class TrapKickRole
{
    Anchor = 0,
    Support,
    Pickup,
    Tail,
    GhostLike
};

inline bool isPrimaryKickRole(TrapKickRole role)
{
    return role == TrapKickRole::Anchor || role == TrapKickRole::Tail;
}

inline TrapKickRole classifyKickRoleFromNote(const NoteEvent& note, TrapPhraseRole phraseRole)
{
    const int stepInBar = ((note.step % 16) + 16) % 16;

    if (note.isGhost || note.velocity <= 78)
        return TrapKickRole::GhostLike;

    if (stepInBar == 0 || stepInBar == 8 || stepInBar == 10)
        return TrapKickRole::Anchor;

    if (stepInBar >= 14 || (phraseRole == TrapPhraseRole::Ending && stepInBar >= 12))
        return TrapKickRole::Tail;

    if (stepInBar == 6 || stepInBar == 7 || stepInBar == 11 || stepInBar == 12)
        return TrapKickRole::Pickup;

    return TrapKickRole::Support;
}
} // namespace bbg
