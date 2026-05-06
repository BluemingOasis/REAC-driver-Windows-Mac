#include "reac_ring_buffer.h"

#include <algorithm>

ReacRingBuffer::ReacRingBuffer(size_t capacity_frames)
    : frames_(capacity_frames)
{
}

void ReacRingBuffer::push(const reac::DecodedMultichannelPacket& packet)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (int sample = 0; sample < reac::kSamplesPerPacket; ++sample) {
        for (int channel = 0; channel < reac::kChannelCount; ++channel) {
            frames_[write_index_][channel] = packet.channels[channel][sample];
        }

        write_index_ = (write_index_ + 1) % frames_.size();
        if (available_ < frames_.size()) {
            ++available_;
        } else {
            read_index_ = (read_index_ + 1) % frames_.size();
        }
    }
}

void ReacRingBuffer::read(float** output_channels, int channel_count, size_t frame_count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t frame = 0; frame < frame_count; ++frame) {
        if (available_ == 0) {
            for (int channel = 0; channel < channel_count; ++channel) {
                output_channels[channel][frame] = 0.0f;
            }
            continue;
        }

        for (int channel = 0; channel < channel_count; ++channel) {
            output_channels[channel][frame] = frames_[read_index_][channel];
        }
        read_index_ = (read_index_ + 1) % frames_.size();
        --available_;
    }
}

size_t ReacRingBuffer::available() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return available_;
}
