#pragma once

#include "reac_decoder.h"

#include <array>
#include <cstddef>
#include <mutex>
#include <vector>

class ReacRingBuffer {
public:
    explicit ReacRingBuffer(size_t capacity_frames = 48000 * 4);

    void clear();
    void push(const reac::DecodedMultichannelPacket& packet);
    void read(float** output_channels, int channel_count, size_t frame_count);
    void read_interleaved(float* output, int channel_count, size_t frame_count);
    size_t available() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::array<float, reac::kChannelCount>> frames_;
    size_t write_index_ = 0;
    size_t read_index_ = 0;
    size_t available_ = 0;
};
