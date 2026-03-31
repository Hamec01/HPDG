#pragma once

namespace bbg
{
struct SoundLayerState
{
    float pan = 0.0f;
    float width = 1.0f;
    float eqTone = 0.0f;
    float compression = 0.0f;
    float reverb = 0.0f;
    float gate = 0.0f;
    float transient = 0.0f;
    float drive = 0.0f;
};
} // namespace bbg
