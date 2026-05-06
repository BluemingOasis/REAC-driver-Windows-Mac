#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace reac {

constexpr int kSampleRate = 48000;
constexpr int kChannelCount = 40;
constexpr int kSamplesPerPacket = 12;
constexpr int kEtherHeaderBytes = 14;
constexpr int kPayloadOffset = kEtherHeaderBytes + 2 + 2 + 32;
constexpr int kBytesPerSample = 3;
constexpr int kFramePayloadBytes = kSamplesPerPacket * kChannelCount * kBytesPerSample;
constexpr int kMinimumPacketBytes = kPayloadOffset + kFramePayloadBytes + 2;

struct DecodedPacket {
    std::array<float, kSamplesPerPacket> left{};
    std::array<float, kSamplesPerPacket> right{};
    uint16_t counter = 0;
};

struct DecodedMultichannelPacket {
    std::array<std::array<float, kSamplesPerPacket>, kChannelCount> channels{};
    uint16_t counter = 0;
};

enum class DecodeStatus {
    ok,
    too_short,
    wrong_ethertype,
    bad_trailer,
    invalid_channel,
};

class Decoder {
public:
    DecodeStatus decode_stereo(const uint8_t* packet,
                               size_t packet_size,
                               int left_channel,
                               int right_channel,
                               DecodedPacket& out) const;

    DecodeStatus decode_all(const uint8_t* packet,
                            size_t packet_size,
                            DecodedMultichannelPacket& out) const;

private:
    static DecodeStatus validate_packet(const uint8_t* packet, size_t packet_size);
    static float decode_sample(const uint8_t* payload, int channel, int sample_index);
};

const char* to_string(DecodeStatus status);

} // namespace reac
