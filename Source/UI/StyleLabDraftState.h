#pragma once

#include "../Core/PatternProject.h"

namespace bbg
{
struct StyleLabDraftState
{
    PatternProject draftPattern = createDefaultProject();
    bool isDirty = false;
    bool hasCapturedPattern = false;
};
} // namespace bbg