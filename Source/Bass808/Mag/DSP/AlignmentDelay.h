#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "DSP/MagSettings.h"

namespace hamlo::dsp
{
// Integer-sample delay used for latency alignment (PDC). Memory sized in prepare(); a delay of 0
// copies the input exactly.
class AlignmentDelay
{
public:
    void prepare(int capacity)
    {
        for (auto& b : buffers)
            b.assign(static_cast<size_t>(capacity + 1), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buffers)
            std::fill(b.begin(), b.end(), 0.0f);
        writePos = 0;
    }

    int capacity() const noexcept { return static_cast<int>(buffers[0].size()) - 1; }

    // out may alias in.
    void process(const float* const* in, float* const* out, int numChannels, int numSamples, int delay) noexcept
    {
        const int size = static_cast<int>(buffers[0].size());
        delay = std::min(delay, size - 1);
        int pos = writePos;
        for (int i = 0; i < numSamples; ++i)
        {
            int read = pos - delay;
            if (read < 0)
                read += size;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& b = buffers[static_cast<size_t>(ch)];
                b[static_cast<size_t>(pos)] = in[ch][i];
                out[ch][i] = b[static_cast<size_t>(read)];
            }
            if (++pos == size)
                pos = 0;
        }
        writePos = pos;
    }

private:
    std::array<std::vector<float>, kMaxChannels> buffers;
    int writePos = 0;
};
} // namespace hamlo::dsp
